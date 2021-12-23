The upstream generally discovers all downstream nodes through a naming service, and distributes traffic to the downstream nodes through a variety of load balancing methods. When a downstream node has a problem, it may be isolated to improve the efficiency of load balancing. The quarantined node is periodically checked for health, and rejoins the normal node after success.

# Naming Service

In brpc, [NamingService](https://github.com/brpc/brpc/blob/master/src/brpc/naming_service.h) is used to obtain all nodes corresponding to the service name. An intuitive approach is to call a function periodically to get the latest node list. But this will bring a certain delay (the period of regular call is generally about a few seconds), which is not suitable as a general interface. Especially when the naming service provides event notification (such as zk), this feature is not used. So we reverse the control: it is not that we call the user function, but the user calls our interface after getting the list, corresponding to [NamingServiceActions](https://github.com/brpc/brpc/blob/master/src/brpc /naming_service.h). Of course, we still have to start the function for this process, corresponding to NamingService::RunNamingService. The following three implementations explain this method:

-bns: There is no event notification, so we can only get the latest list regularly. The default interval is [5 seconds](http://brpc.baidu.com:8765/flags/ns_access_interval). In order to simplify this kind of regular access logic, brpc provides [PeriodicNamingService](https://github.com/brpc/brpc/blob/master/src/brpc/periodic_naming_service.h) for users to inherit, and users only need to implement a single How to get (GetServers). After obtaining, call NamingServiceActions::ResetServers to tell the framework. The framework will de-duplicate the list, compare it with the previous list, and notify the observers (NamingServiceWatcher) who are interested in the list. This set of logic will run in a separate bthread, NamingServiceThread. A NamingServiceThread may be shared by multiple Channels, and ownership is managed through intrusive_ptr.
-file: The list is the file. A reasonable way is to re-read the file after it is updated. [The implementation](https://github.com/brpc/brpc/blob/master/src/brpc/policy/file_naming_service.cpp) Use [FileWatcher](https://github.com/brpc/brpc/blob/ master/src/butil/files/file_watcher.h) pay attention to the modification time of the file. When the file is modified, read and call NamingServiceActions::ResetServers to tell the framework.
-list: The list is in the service name (separated by commas). After reading it once and calling NamingServiceActions::ResetServers, it exits because the list will never change.

If users need to create these objects, it is still not convenient enough, because some factory code is always needed to create different objects according to the configuration items. In view of this, we have built the factory class into the framework, and it is a very convenient form:

``
"protocol://service-name"

eg
bns: // <node-name> # baidu naming service
file://<file-path>           # load addresses from the file
list://addr1,addr2,...       # use the addresses separated by comma
http://<url>                 # Domain Naming Service, aka DNS.
``

This set of methods is extensible. After the new NamingService is implemented, it can be drawn in [global.cpp](https://github.com/brpc/brpc/blob/master/src/brpc/global.cpp) Just register, as shown in the figure below:

![img](../images/register_ns.png)

Seeing these familiar string formats, it is easy to think that ftp:// zk:// galileo:// and so on are all supported. When creating a new Channel, the user passes in this NamingService description, and can write these descriptions in various configuration files.

# Load balancing

[LoadBalancer](https://github.com/brpc/brpc/blob/master/src/brpc/load_balancer.h) in brpc selects a node from multiple service nodes. For the current implementation, see [Load Balance](client .md#Load balancing).

The most important thing about load balancer is how to make load balancing in different threads not mutually exclusive. The technology to solve this problem is [DoublyBufferedData](lalb.md#doublybuffereddata).

Similar to NamingService, we use a string to refer to a load balancer, which is registered in global.cpp:

![img](../images/register_lb.png)

# health examination

For those nodes that cannot be connected but are still in NamingService, brpc will periodically connect to them. After success, the corresponding Socket will be "resurrected" and may be selected by LoadBalancer. This process is a health check. Note: The node being checked by health or in LoadBalancer must be in NamingService. In other words, as long as a node is not deleted from NamingService, it is either normal (it will be selected by LoadBalancer) or is undergoing a health check.

The traditional approach is to use one thread to perform health checks on all connections. brpc simplifies this process: dynamically create a bthread for the required connections to do health checks (Socket::HealthCheckThread). The life cycle of this thread is managed by the corresponding connection. Specifically, when the Socket is SetFailed, the health check thread may start (if SocketOptions.health_check_interval is a positive number):

-The health check thread first closes the connection after ensuring that no one else is using the Socket. It is currently judged by the reference count of the Socket. The reason why this method is effective is that the Socket cannot be Addressed after being SetFailed, so the reference count is only decreased but not increased.
-Periodically connect until the remote machine is connected. During this process, if the Socket is destroyed, the thread will exit accordingly.
-Resurrect the Socket (Socket::Revive) after connecting, so that the Socket can be accessed by other places, including LoadBalancer (via Socket::Address).