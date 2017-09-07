```
r35243 zhu  Add feature that controller and server can parse json string to pb bytes and vice versa
r35222 czy  Fix the bug that bthreads created before any worker threads would not be stolen
r35221 jge  fix comment
r35214 jge  fix an atexit issue in test/test_iobuf.cpp
r35213 jge  Remove the arg to cleanup_pthread in bthread_key.cpp
r35212 jge  Fix destructed-too-early stringstream in test/test_thread_local.cpp
r35208 jge  Change a EINVAL in bthread_id_lock to EPERM
r35206 jge  Replace copyrights in tests
r35206 jge  Add LICENSE
r35205 jge  Rename example/redis_c++/client.cpp to redis_press.cpp as in opensource version
r35203 jge  Modify beginning descriptions of examples
r35202 jge  Remove empty lines at beginning and end of source files
r35200 jge  Add a missing FATAL-to-ERROR change
r35199 jge  Solve the conflict between bthread_id_about_to_destroy and selective channel
r35199 jge  Ignore EPERM in callsites of bthread_id_lock
r35190 jge  sync test_cond.cpp with opensource version
r35189 jge  TaskGroup::yield() does not return a value
r35189 jge  Polish test_cond.cpp
r35189 jge  Add TaskGroup::ReadyToRunArgs instead of converting bthread_t to void* directly
r35178 jge  fix a potential out-of-range issue in http_method.cpp
r35174 jge  Fix baidu-rpc-ub
r35171 zhu  Fix Rtmp UT
r35170 zhu  Make SubStream in RtmpRetryingClientStream more general and can be customized by user
r35169 jge  Add ComlogSinkOptions.min_log_level
r35154 jge  Polish stack related code
r35153 zhu  Fix UT in both BAIDU_INTERNAL and opensource
r35149 jge  Renamed baidu/rpc/config.h to baidu/rpc/log.h and not include it in COMAKE
r35145 jge  Add a missing change to UT
r35144 jge  Support clang 3.5-4.0
r35141 jge  Fix UT of bthread
r35119 zhu  Support gcc-4.8.2 and remove src/string_printf.h, src/string_printf.cpp
r35118 zhu  Add bytes_to_base64 and base64_to_bytes options
r35110 jge  include ssl.h in ssl_compat.h
r35109 jge  Remove BAIDU_INTERNAL guard for giano_authenticator
r35106 jge  Adapt openssl 0.9.7
r35104 jge  Support gcc 7, glibc 2.25, openssl 1.1
r35099 jge  Use FileEnumerator instead of readdir_r in get_fd_count
r35098 jge  Add compilation options for gcc7
r35095 jge  Fix incorrect swap declaration in base/iobuf.h
r35092 jge  Support openssl 1.1 which makes all structs opaque and does not need locking-callback. The fix is done by adding base/ssl_compat.h provide new functions in newer openssl
r35092 jge  Support glibc 2.25 which deprecated readdir_r
r35092 jge  Support gcc 7.1, including implicit-fallthrough fromat-truncat aligned-new issues
r35092 jge  berror does not pass NULL to strerror_r anymore
r35091 jge  Use fmix64 for pthread_self(which is 64-bit)
r35088 czy  Downgrade some FATAL to ERROR
r35084 jge  Add a ctor for BoundedQueue
r35076 jge  Add bvar::ScopedTimer
r35075 jge  Add stack_count in the right place
r35069 jge  Enable TaskGroup._last_lp_state by default
r35066 jge  Remove -include in BCLOUD (missed in previous CI)
r35065 jge  Not use -include in bthread's compilation
r35064 jge  Misc minor adjustments
r35064 jge  Fix incorrect port in -trackme_server
r35064 jge  Align MostCommonMessage by cacheline
r35061 jge  Use internal::FastPthreadMutex in butex/bthread_id/TimerThread to speed up uncontended locks slightly
r35061 jge  Polish comments in butex/TaskGroup
r35061 jge  Implement bthread creation from non-worker differently to lower the overhead of uncontended locks
r35061 jge  Fixed the issue that bthread_about_to_quit() should not increase _num_nosignal
r35061 jge  bthread_yield() does not increase _num_nosignal nor signal_task
r35046 czy  Fix the bug that invalid memory is probably accessed in CountdownEvent::signal & bthread_cond_signal/broadcast
r35031 jge  Add a macro flag BTHREAD_FAIR_WSQ
r35020 czy  Fix memory explosion of ExecutionQueueTest
r35019 jge  Fixed a compilation issue
r35012 jge  Use ObjectPool for allocating butex to fix the race between butex_wake and butex_destroy. As a correspondence, butex_construct, butex_destruct, butex_add_ref_before_wake, butex_remove_ref, butex_wake_and_remove_ref, butex_wake_all_and_remove_ref, BUTEX_MEMORY_SIZE are removed
r35012 jge  Removed static XXXPoolFreeChunkMaxItem and replace it with previous XXXPoolFreeChunkMaxItemDynamic
r35012 jge  Removed specialization of bthread::Mutex for unique_lock/lock_guard, according to changes to public/common
r35012 jge  Removed CountdownEvent.init()
r35012 jge  Polished WorkStealingQueue to reduce its overhead, especially those caused by seq_cst fences
r35012 jge  Polished bthread_mutex.cpp, making bthread_mutex useable before main(), removing extra spins, using exchange instead of CAS in unlock, sampling TIMEDOUT locks etc
r35012 jge  Not expose bthread_butex_waiter_count by default
r35012 jge  Half XXXPoolBlockMaxSize and XXXPoolBlockMaxItem
r35012 jge  Fixed potential memory explosion in ExecutionQueueTest.execute_urgent
r35012 jge  Add -show_per_worker_usage_in_vars which is turned on to show per-worker usage in /vars/bthread_per_worker_usage_<tid>
r35000 jge  Fix example/multi_threaded_echo_fns_c++
r34998 jge  Support pb 3.2x
r34997 jge  Fix a bug in Socket.SharedPart.UpdateStatsEverySecond() that Sampled.in_num_messages_s and out_size_s are assigned wrongly
r34997 jge  Clean up examples
r34988 jge  Remove Socket._epollout_butex_memory
r34988 jge  Remove rtmp debugging code
r34985 jge  make base::double_lock workable with more types
r34985 jge  Add default impl. of unique_lock/lock_guard in c++03, make fast paths simpler
r34975 jge  Fix UT of bvar
r34974 jge  Add space after colon in HTTP entity-header back
r34971 jge  Remove the space after colon in every HTTP entity-header
r34971 jge  Fix possibly duplicated Content-Length brought in r34950
r34971 jge  Add UT on http-serialization and Adaptive*Type
r34965 jge  Fix a bug of URI that "host?query" is not parsed correctly and enhance parsing speed slightly by using an ascii-char mapping.
r34964 jge  move bad_method_service.h/cpp into builtin, naming_service_thread.h/cpp load_balancer_with_naming.h/cpp into details, excluded_servers.h up
r34959 jge  Fix removed set_socket_correlation_id in baidu-rpc-ub
r34956 jge  Use base::Mutex as much as possible
r34955 jge  Not set -trackme_server when BAIDU_INTERNAL is absent
r34954 jge  make trackme_server configurable
r34953 jge  Add ServiceOptions and allow_http_body_to_pb to turn off the conversion to keep compatible with clients using (pretty old) baidu-rpc
r34950 jge  URI._query_map is lazily created since many apps do not check queries.
r34950 jge  Socket._parsing_context is thread-safe. Add Socket.initialze_parsing_context.
r34950 jge  Rewrite SetHttpURL() to boost its performance.
r34950 jge  Replaced HttpMessageSerializer with SerializeHttpRequest/Response in http_message.h
r34950 jge  Renamed HttpInputMessage to HttpContext which is more precise.
r34950 jge  Remove long-deprecated methods
r34950 jge  Removed URI._reason_pharse. reason_phrase() always returns literal description of the status code
r34950 jge  Polish code in http_rpc_protocol.cpp
r34950 jge  Http enables rpcz on-demand according to incoming requests (previously not)
r34950 jge  error texts returned by HTTP does not add an extra newline.
r34950 jge  Controller.request_protocol() works for client-side as well.
r34950 jge  Add tools/clean_all_makefile
r34950 jge  Add SocketMesasge.EstimatedByteSize() for tracing of SocketMessages
r34950 jge  Add IndentingOStream to print structured infos more pretty.
r34950 jge  Add HttpMethod2Str and Str2HttpMethod, which is used by http2
r34944 jge  Port rpc part of http://icode.baidu.com/myreview/changes/2256343
r34939 jge  Port bvar part of http://icode.baidu.com/myreview/changes/2256343
r34938 jge  Port http://icode.baidu.com/myreview/changes/2263487 and http://icode.baidu.com/myreview/changes/2265947
r34934 czy  Upgrade tcmalloc from 1.7 to 2.5 and compile from the source file since we fould a tricky bug that binary linked with third-64/tcmalloc might crash before main, which is caused by the race condition of malloc
r34934 czy  Not include <google/profiler.h> to resolve the issue that this header was deprecated in the later version of tcmalloc
r34929 zhu  Add Flv Reader
r34926 zhu  Fix potential race condition in RTMP
r34922 jge  Treat DoNothing specially in pthread mode
r34921 jge  RetryPolicy intercepts successful RPC as well
r34921 jge  Comment _app_connect->StopConnect() in SetFailed() temporarily to avoid race.
r34921 jge  Add StreamCreator.CleanupSocketForStream to ease management of stream-releated sockets.
r34915 zhu  Fix _using_sub_stream not destroying bug in RtmpRetryingClientStream
r34901 czy  Some minor changes
r34901 czy  Add bvar to expose usage of the sampling thread
r34894 liu  Add options for protobuf -> map
r34893 jge  Add Controller.ignore_eovercrowded() and remove FLAGS_SSL
r34892 jge  Merge all boolean fields in Controller into _flags
r34892 jge  Make assignment-order in Socket.Create same with field declaration order.
r34891 jge  Replace MinuteUpdater with SparseMinuteCounter to reduce memory when #connections are huge
r34891 jge  Fix a potential bug in KeepWrite that req may be NULL before DoWrite
r34891 jge  Add debugging log for RTMP temporarily
r34886 zhu  add some vlogs to debug RTMP
r34883 czy  Fix compile errors by gcc345
r34882 czy  New algorithm to merge PercentileSamplers which reduce overhead to combine mulitple samplers into one
r34877 jge  Make RtmpUnsentMessage.next be SocketMessagePtr as well
r34874 jge  Wrap SocketMessage into SocketMessagePtr so that AppendAndDestroySelf is always called even if Socket.Write fails
r34873 zhu  Change _rpc_state in progressive attachment from enum to static const to fix bcloud build
r34872 zhu  Fix a bug that controller and progressive attachment can write into a socket simultaneously
r34871 jge  Client-side does not try other protocols.
r34871 jge  Add ServerOptions.enabled_protocols to only serve protocols in the whitelist
r34867 jge  SocketMessage.AppendAndDestroySelf can fail, as a result, batched Socket.Write is removed since messages submitted in one batch can't be guaranteed to be successful or failed together. 
r34858 czy  Revert the wrong COMAKE brought in r34855
r34855 czy  Not limit backlog of listen() with a hard code value, which is limited by the value defined in /proc/sys/net/core/somaxconn since kernel-2.4.25
r34850 zhu  fix a bug that sub_stream in RtmpRetryingClientStream::Recreate may be destructed unexpectedly
r34845 zhu  Implement simplified rtmp protocol reducing process of handshaking and connection to 0 RTT
r34836 jge  fix compilation issue in UT
r34835 jge  Set user-info in Authorization according to basic access authentication
r34835 jge  Remove g_header_reflectors in http_message.cpp
r34835 jge  Move some code from PackHttpRequest to SerializeHttpRequest to simplify code in PackHttp2Request
r34835 jge  Fix an issue in http that if content-type of request is wrong, content-type of response will always be wrong
r34831 jge  Revert wrongly changed hulu_pbrpc_meta.pb.cc in previous CI
r34826 jge  Add SocketMessage** to all pack_request implementations.
r34823 jge  Write fails when pipelined_count is too large (overflowed previously)
r34823 jge  Replace SocketConnection usages in RTMP with SocketMessage
r34823 jge  Renamed Socket.UserMessage to SocketMessage, Socket.MessageUnion to MessageUnion
r34823 jge  Fix the issue that WriteRequest.Setup must be called after connection(including app_connect) is done.
r34823 jge  Fix a bug of SocketMessage that a SocketMessage appends nothing may make Write stop before all WriteRequests are written.
r34823 jge  Extend Protocol.pack_request with SocketMessage
r34795 jge  Support Socket.Write in batch which is suitable for writing several small messages in one call, say in RTMP
r34795 jge  Share error handling code between different Socket.Write()
r34795 jge  Fix an issue that WriteRequest.Setup is not called in the same sequence of submission(brought in r34776), which breaks the redis/memcache protocol.
r34795 jge  Add SocketOptions.initial_parsing_context to initialize Socket._parsing_context (generally in client-side)
r34789 jge  Replaced Socket._input_message with more general Socket._context
r34779 jge  Move http_parser.cpp and http_message.cpp into details
r34776 jge  server's concurrency is removed after sending responses (or errors)
r34776 jge  Rearrange fields of Socket to make false-sharing less(hopefully...)
r34776 jge  Print http header before receiving all body for progressively-read sockets when -http_verbose is on.
r34776 jge  Not remove tailing slashes in tcurl of RTMP.
r34776 jge  Ignore EOVERCROWDED when server sends responses.
r34776 jge  Fixed an issue on parsing progressively-read messages that created threads may not be flushed before blocking on next parsing.
r34776 jge  Extend Socket.Write with Socket.UserMessage which is serialized sequentially. The limitation on unwritten_bytes of sockets is implemented differently to work both for IOBuf and Socket.UserMesage
r34776 jge  Add BRPC_HANDLE_EOVERCROWDED and BRPC_HANDLE_EOVERCROWDED_N to simplify sleep-wait on EOVERCROWDED.
r34773 jge  Optimize IOBuf.append(Movable) on empty bufs
r34773 jge  Fix a bug on IOBufAsZeroCopyInputStream.ByteCount
r34762 jge  limit max body printed when -http_verbose is on
r34762 jge  fix a bug that a branch is wrongly break when handling non-progressively-read http requests with connection:close
r34759 jge  Unify params to Socket.Write in Socket.WriteOptions
r34759 jge  ProgressiveReader handles connection:close correctly
r34741 jge  sequence header of H264/265 must be keyframe
r34740 czy  HTTP protocol supports application/proto as well
r34734 jge  Simplify code on -log_error_text with LogErrorTextAndDelete which is a deleter to unique_ptr
r34734 jge  Fix the issue that some errors(fail to write into socket) are not logged by rpcz.
r34734 jge  Fix the issue that Controller.log_id() is not set into nshead.log_id in all nshead_protocols.
r34734 jge  Better UI for rpcz: more accurate descriptions, more fields.
r34731 jge  Solve the issue that samples may not be collected fully at the beginning when qps is small
r34730 jge  global variable `span_db' is changed to primitive type to avoid dtor-sequence issues
r34717 jge  tools/switch_trunk updates baidu-rpc as well
r34717 jge  add a flag to controller to mark if a backup request was sent
r34704 czy  Supress warnings of cast-qual
r34697 jge  server count is in server.cpp directly
r34697 jge  Moves bvar of iobuf into thread GlobalUpdate
r34697 jge  log_id is only set to controller when it's present in request
r34697 jge  Add baidu::rpc::DoNothing() which is a closure does nothing to simplify code on semi-synchronous RPC.
r34694 jge  Put hostname just before filename instead of being a suffix when -log_hostname is on
r34694 jge  noflush supports bthread when bthread is linked
r34693 jrj  Revert server's status (and resource) when start filed
r34693 jrj  Fix a bug that server's status is not RUNNING the moment it begins to accept
r34693 jrj  Append user SNI filters to those inside certificate
r34691 czy  Implement HPACK 
r34688 czy  Add IOBufBytesIterator::copy_and_forward
r34677 jge  NsheadMessage.Clear() zeroizes head.
r34677 jge  Make total bytes limit in pb consistent with -max_body_size
r34655 jge  Add process_disk_read/write_bytes_second
r34645 jge  Use controller.response() instead of private_accessor's
r34642 jge  Add Controller.response()
r34637 jge  Use newly added StringPiece.back() & back_char(std::string) in baidu-rpc
r34634 jge  Use newly added back_char(std::string) in bvar
r34631 jge  Rename xxx_nothrow in string_piece.h to xxx_or_0 which is shorter and more comprehensive
r34628 jge  Add front()/back() for StringPiece & front_char()/back_char() for std::string
r34623 zhu  add a StringPiece constructor to QuerySplitter and its corresponding UT
r34622 jge  Replace all explicit signal handling with baidu::rpc::IsAskedToQuit()
r34609 zhu  Add QuerySplitter and QueryRemover
r34602 jge  Make sure Controller.max_retry is always non-negative
r34599 jge  refine UT
r34596 jge  add missing break in the loop of find valid port inside port_range
r34591 jge  Support method-level max_concurrency, which is set by server.MaxConcurrencyOf("xxx") = y
r34591 jge  Show max_concurrency of server at /status when it's been set.
r34591 jge  Implement Start() with PortRange better.
r34580 czy  Fix the bug that 2 free block is not enough to OutputStream::reserve
r34573 czy  Fix the bug that ParseNamingServiceUrl access invalid memory when the length of the invalid given ns_url is larger than MAX_PROTOCOL_LEN
r34556 zhu  change the value of FLV_VIDEO_HEVC STORY=INF-BCE-MM-5716
r34545 zhu  add H265 rtmp support STORY=INF-BCE-MM-5716
r34538 jge  Fix a dtor-sequence issue in dumping_thread: command_name was dtor before dumping_thread quits
r34532 jge  remove debugging bvar
r34532 jge  Add -pb_enum_as_number
r34531 zhu  add user-defined metadata in RTMP protocol
r34522 czy  Add AddNonProtocolHandler which allows users to use Socket without implementing Protocols
r34508 jge  Not warn strict aliasing in public/iobuf
r34505 jge  Slightly adjust code on managing SharedPart and add rpc_socket_sharedpart_count and rpc_socket_pool_count in socket.cpp to debug a memory-leaking issues related with pooled connections.
r34505 jge  RTMP supports reading multiple SPS and PPS
r34505 jge  Fix an issue in Controller.sub_channel_controller that boundary checking is not checked when _ndone != _nchan
r34502 jge  Implement PrintedAsBinary differently
r34499 wan  add esp protocol unit test
r34498 wan  esp response head msg error
r34494 wan  log error
r34493 wan  add esp protocol
r34466 czy  Reserve protocol type for cds-agent
r34458 czy  Catch ChannelExecption in start() and check members in stop()
r34444 jge  Fix an error in Socket.ReleaseAllFailedWriteRequests that error_code may be 0
r34437 czy  Add HuluController to allow users to fill in hulu-pbrpc specific meta data
r34430 jge  Replace some usages of EFAILEDSOCKET with EEOF and EUNUSED.
r34430 jge  Replaced Socket::Duplicate(static method) with Socket::ReAddress()
r34430 jge  Add Socket::description() and elaborate socket-related logging with the method.
r34430 jge  Add a version of Socket::SetFailed accepting error_code/error_text so that socket-related error reporting is more comprehensive.
r34429 jge  Fix a bug in http that a progressively-read http response may be destroyed too early
r34423 jge  Worker steals self's running queue first after wake-up
r34423 jge  Split API on bthread_id into bthread_id.h
r34423 jge  Add c++-specific API on bthread_id to pass an extra error_text to make error reporting more comprehensive
r34422 jge  Fix a potential deadlock in GetNamingServiceThread when the entry in g_nsthread_map is being destructed
r34418 jge  fix a bug in nshead_mcpack that EOF is checked too early
r34403 jge  Make some warnings on hot path of RTMP less frequent
r34398 jge  Fix a bug in PackXXXHeader that header.append() misses size
r34397 jrj  Fix a compiling warning under gcc4.4
r34392 jge  Include coded_stream.h in some files to avoid compilation issues on pb 2.6
r34389 jge  Modify ProcessXXX in baidu-rpc-ub to adapt new changes to baidu-rpc
r34386 jge  SocketMapXXX handles memory fence correctly.
r34386 jge  Slightly optimize serializations of header&meta in baidu_rpc/hulu_pbrpc/sofa_pbrpc
r34386 jge  Protocol.process_request/Protocol.process_response/InputMessageHandler.process own the InputMessageBase* argument which is not destroyed by InputMessenger right now. This helps us to encapsulate the EOF handling code inside InputMessageBase and simplify protocol implementation code.
r34386 jge  Move the code on global client-side InputMessenger into input_messenger.cpp, and replace AddClientSideHandler/GetProtocolIndex with methods to the global messenger.
r34381 jge  Revert to old-style overriding of expose_xxx functions due to weird issues reported by user
r34374 jge  Change leveldb in BCLOUD to third-64 as well
r34367 jge  fix python/setup.py
r34364 jge  Disable monotonic clock in base::ConditionVariable
r34357 jge  Make init of usercode pool robust to initialization order
r34357 jge  Change deps on leveldb back to the one under third-64 to avoid compilation issues
r34354 czy  Fix the issue that gcc renamed cstdatomic to atomic since gcc4.5
r34343 czy  Workarounds for gcc < 4.8, which all don't support thread_fence
r34343 czy  Fix compile issue when using c++0x with gcc4.4
r34329 jge  Suppress false strict-aliasing warning in gcc 4.4
r34324 jge  Replace atomics in public/baidu-rpc with base::atomic
r34321 jge  Replace atomics in public/bthread with base::atomic
r34318 jge  Replace atomics in public/iobuf with base::atomic
r34315 jge  Variable::list_exposed may release lock occasionally
r34315 jge  Replace atomics in public/bvar with base::atomic
r34312 jge  Add BASE_DELETE_FUNCTION
r34312 jge  Add base::atomic and base::static_atomic
r34309 jge  Fix the crash when restful path is a just a filename containing dot
r34304 jge  Handle ERANGE error returned by gethostbyname_r in DNS
r34296 jge  fix a warning of py_service.cpp in gcc 5.4
r34289 czy  Fix format errors when constructing base::Status
r34286 czy  Add format checking for Status
r34277 czy  Fix compile errors with gcc345
r34276 czy  Compile errors fixes
r34275 czy  Move some impls of execq into .cpp
r34275 czy  All execq and TaskNode shares the same ResourcePool and ObjectPool regardless of template arguments
r34272 jge  Show different vars for html and plain-text
r34267 jge  Remove deps on boost::any
r34267 jge  Avoid calling virtual functions inside ctors
r34267 jge  Add DisplayFilter to show some vars only on html or plain-text
r34259 jge  Make add_reference work with void
r34258 jge  add bound-checking in bthread/test/COMAKE
r34255 jge  Fix warnings in gcc 5.4 (public/baidu-rpc)
r34252 jge  Fix warnings in gcc 5.4 (public/bthread)
r34249 jge  Fix warnings in gcc 5.4 (public/bvar)
r34246 jge  Fix warnings in gcc 5.4 (public/common)
r34246 jge  Fix incorrect test on BASE_CXX11_ENABLED in last CI
r34244 jge  Remove deps on boost from base/unique_ptr.h, removed base/memory/unique_ptr.h
r34233 jge  Use base::intrusive_ptr instead of boost::intrusive_ptr
r34233 jge  Remove explicit deps on boost from public/baidu-rpc because of changes on public/common
r34230 jge  Remove explicit deps on boost from UT of public/bthread because of changes on public/common
r34227 jge  Remove explicit deps on boost from UT of public/iobuf because of changes on public/common
r34224 jge  Remove explicit deps on boost because of changes on public/common
r34221 jge  Not link libs of boost in public/common
r34218 jge  publish base/*.hpp in BCLOUD
r34217 jge  UserCodeBackupPool uses bthread::run_worker_startfn() instead of g_worker_startfn
r34217 jge  Use intrusive_ptr instead of shared_ptr on MongoContext
r34217 jge  DomainNamingService use 80 as default port
r34214 jge  Removed code branched by BTHREAD_SIGNAL_TYPE
r34214 jge  Removed bthread/log.h and baidu/bthread/log.h
r34214 jge  Removed begin_fn/end_fn from TimerThreadOptions. TimerThread is affected by bthread_set_worker_startfn()
r34214 jge  non-worker pthreads do not create TaskGroup, which reduces overhead of steal_task
r34213 jge  Replace boost.context with a stripped-down version supporting less platforms
r34210 jge  Add base::intrusive_ptr which is stripped-down from intrusive_ptr in boost 1.56
r34209 jge  Remove boost::intrusive_list from UT of bthread
r34204 jge  Wrap base::Lock over base::Mutex and remove lock_impl related code
r34204 jge  Move content of base/mutex.h into base/synchronization/lock.h since the latter one is likely to be conflict with other modules
r34195 czy  Fix compile errors
r34186 jge  Add base::Mutex
r34171 jge  Compilation fixes for c++11
r34166 jge  Update bugs
r34163 jge  fix memory leak in IOBuf.append(Movable)
r34147 czy  Not compile shared_memory_nacl.cc
r34138 jge  Move an error checking into LB respectively because LA does not need it
r34134 jge  Show vars iobuf_newbigview_second instead of ibouf_newbigview_count
r34134 jge  Shown connections on /connections have a upper limit
r34134 jge  ReleaseReferenceIfIdle ends socket according to Socket.fail_me_at_server_stop()
r34134 jge  Make critical section of Acceptor.ListConnections smaller with PositionHint newly added to FlatMap
r34134 jge  Fix tools/trackme_server/getstats.sh for format changes
r34134 jge  Errors counted by non_service_error and service.error do not intersect
r34134 jge  CreateProgressiveAttachment adds a parameter stop_style to control how underlying socket should be ended when the socket becomes idle or the server is stopped.
r34131 jge  iterator of FlatMap can be saved and restored after modifications
r34130 jrj  Use system openssl to keep consistent with COMAKE of baidu-rpc
r34123 jge  Use newly-added IOBuf.append(Movable) in baidu-rpc
r34120 jge  Minor changes to bthread
r34117 jge  Change gtest version in bvar/test
r34114 jge  Add movable append
r34111 jge  Remove deps on gtest from public/common
r34111 jge  public/common does not rely on boost.atomic
r34108 jge  Return IOBuf.Blocks cached by Socket._read_buf(IOPortal) at proper timing to reduce memory consumption greatly when most connections are idle.
r34108 jge  Compile baidu-rpc as well in tools/switch_trunk
r34108 jge  Add more bvar on IOBuf
r34105 jge  fix warning in gcc 3.4
r34104 jge  Use Atomic in base instead of boost::atomic
r34104 jge  Optimize copy-ctor/assignment related functions slightly
r34104 jge  IOBuf is comparable.
r34104 jge  Blocks cached by IOPortal can be returned to TLS by calling return_cached_blocks()
r34104 jge  BlockRefs can be cached thread-locally (not turned on)
r34101 czy  Add Server::ResetMaxConcurrency which allows user to change max_concurrency when the very Server is running
r34098 czy  Fix the bug that FlatMap::operator= doesn't copy _load_factor when |this| is not initialized, which makes |this| always too crowded ever after
r34083 jge  not match end-of-line in idl2proto which does not work correctly for lines commented by chinese in some environments
r34083 jge  Make all generated functions global to avoid cross-reference issues.
r34083 jge  fix a wrong type in map conversion
r34083 jge  convert "#include *.idl" to "import *.proto;"
r34073 jge  http can read user ip from header
r34063 czy  Redis Client supports bakcup request as well
r34058 czy  Fix the bug that connection of _unfinished_call returnes to the pool even if there's no response received thought it
r34050 jge  baidu-rpc uses base/third_party/murmurhash3 instead
r34045 czy  Release murmurhash3.h to output/include
r34040 jge  bthread uses base/third_party/murmurhash3 instead
r34037 jge  compile murmurhash in test/COMAKE
r34036 jge  Move murmurhash into base/third_party
r34033 jrj  Fix a warning of assigning negative to unsigned
r34032 jrj  Reloadable certificate
r34029 jge  Support modifying stream_name before each retry by specifying stream_name_manipulator
r34029 jge  Prefer simple handshake in rtmp client connections
r34029 jge  Fix the race between a failed OnStatus() and creation of stream
r34029 jge  createStream can carry play/publish command on demand to save a RTT
r34020 jge  Support fast_retry_count for RtmpRetryingClientStream
r34020 jge  Replace RtmpClientStream.CanSendMoreCommands with is_server_accepted which is set to true after receiving onStatus command and being more reasonable.
r34020 jge  MD5Hash32V and MurmurHash32V accept StringPiece instead of const_iovec.
r34002 jge  Check protocol against null in Channel.Init
r34001 czy  Suppress warning of unused variable when the file using BAIDU_RPC_VALIDATE_FLAGS fails to compile
r34000 jge  Fix scripts in tools/trackme_server
r34000 jge  Create rtmp bvars on demand
r33997 jge  Add format macro in test COMAKE
r33996 jge  Include inttypes.h in config.h (included by every source file)
r33993 jge  Make BCLOUD consistent with COMAKE
r33993 jge  Define macro for PRIu64/PRId64
r33992 jge  Simplify verbose logging in HttpMessage with IOBufBuilder
r33992 jge  log_error_text works for more cases in RTMP
r33992 jge  Add format checking to Controller.SetFailed and fix found issues.
r33982 jge  Fix wrong boolean test in URI.query
r33969 czy  Add create_parents options to CreateDirectory
r33964 czy  Fix the compile warning in gcc4.4 (offset outside bounds of constant string)
r33960 jge  Move user callback of NamingService outside lock of NamingServiceThread.
r33960 jge  misc changes on RTMP
r33960 jge  LB and NS are both deleted by Destroy() which is customizable by user.
r33960 jge  Fixed a bug in GetNamingServiceThread that a zero-referenced NamingServiceThread may be wrongly referenced again.
r33960 jge  Error texts are prefixed with error code.
r33960 jge  Each NamingServiceThread owns a separate instance of NamingService which can be stateful now.
r33960 jge  Add NamingService remotefile to customize ns via a http call.
r33951 jge  Make singleton usages in logging leaky so that they're immune to exiting-order issues.
r33951 jge  LogSink can(and should) customize log prefixes. As a result, performance of ComlogSink is slightly improved because of removal of generation of default log prefixes.
r33951 jge  Add -log_year to prefix year in datetime part in log prefixes
r33945 jge  Initialize some global variables in bthread with get_leaky_singleton
r33942 jge  Optimize get_leaky_singleton with Acquire_Load and add has_leaky_singleton
r33939 jge  Replace Singleton<> with base::get_leaky_singleton to avoid potential quiting issues and simplify code
r33936 jge  Fix crash of tcp_connect when libthread is not linked.
r33936 jge  Add get_leaky_singleton to simplify creations of never-deleted singletons
r33921 jge  Update bthread revision on make instead of comake2
r33918 jge  Update bvar revision on make instead of comake2
r33918 jge  Print base revision as well
r33914 jge  Add revision for public/common
r33910 jge  Bump version and add a bug in trackme_server
r33905 jge  Fix initialization of values in FlatMap
r33895 jge  Rewrite copy-ctor/operator= of FlatMap to fix a coredump caused by uninitialized _thumbnail and make assignments more efficient
r33877 jge  Solve initialization issues of global std::string in http_rpc_protocol.cpp
r33877 jge  RtmpClientStream.OnPublish is asynchronous and sends STREAM_NOT_FOUND on error (works for ffmpeg and OBS). Enrich UT on rtmp.
r33877 jge  http compresses body when request_comrpess_type is set to gzip at client-side and decompress body for json requests/responses properly.
r33870 czy  Implement CountdownEvent::timed_wait
r33864 jge  Load balancers except LA treat servers with same address but different tags as different servers. By using different tags for the same server, users can control proportion of traffic to the server.
r33864 jge  Change reporting interval of trackme requests from 60s to 300s
r33861 jge  Several method overloads in Server for std::string are removed.
r33861 jge  Replace several std::map with base::FlatMap or base::CaseIgnoredFlatMap, as a result, http headers are stored as they're (lowercased previously).
r33855 jge  Simplify SingleThreadedPool which does not rely on std::vector and is very lightweight before allocating something.
r33855 jge  Extensively fixed issues around FlatMap and make it more lightweight, namely: no memory allocation before init(); seek/erase works before init(); key/value are not constructed before insertion. Rewrite type-unsafe conversions inside iterators. seek/erase accepts different type from key.
r33855 jge  Add PooledMap which is a drop-in replacement of std::map to speed up insert/erase slightly.
r33855 jge  Added CaseIgnoredFlatMap which is a FlatMap with case-ignored std::string as key.
r33839 jge  Add RtmpRetryingClientStreamOptions.retry_if_first_stream_fails_with_no_data which is false by default.
r33836 jge  Rename all tests from test_xyz_suite to XyzTest.
r33836 jge  Fix the wrong usage of MurmurHash32 in RtmpClientStream::Init()
r33836 jge  Fix a bug in RtmpRetryingClientStream that _self_ref is reset too early.
r33836 jge  Add URI::RemoveQuery() and URI.query() will be refreshed after SetQuery/RemoveQuery was called.
r33835 jrj  Replace curl with /usr/bin/curl in UT as the former under /opt doesn't support HTTPS
r33834 jrj  Remove debug log
r33831 jrj  User defined ciphers
r33831 jrj  SNI extension
r33831 jrj  Reuse session configuration
r33831 jrj  Rename SSL_FALSE to SSL_OFF. Add SSL_CONNECTING and SSL_CONNECTED
r33831 jrj  Related changes on UT and examples
r33831 jrj  Fix SSL core bug due to race condition under multiple thread environment
r33831 jrj  DH key-exchange
r33831 jrj  Certificate chain
r33831 jrj  A few SSL security fixes such as turn off renegotiation, protect from heartbleed attack
r33831 jrj  Advanced SSL support at server side. Features include:
r33823 jge  Replace a piece of code on ABI-inconsistent HMAC_CTX with HMAC
r33818 jge  Removed RtmpClientStreamUniquePtr/RtmpRetryingClientStreamUniquePtr, always use DestroyingPtr<> instead.
r33818 jge  Destroy() of RtmpClientStream/RtmpRetryingClientStream can be called before/during/after Init(), and trigger OnStop() properly, provided the stream is enclosed in boost::intrusive_ptr<>
r33804 jge  RtmpClientStream supports consistent hashing.
r33804 jge  Replaced URI.AssembleURL with Print() which has simpler code and performs better. As a correspondence, HttpMessageSerializer serializes headers part using IOBufBuilder(inheriting ostream) instead of appending to IOBuf directly.
r33804 jge  ProgressiveAttachment.Write is thread-safe now. Chunks written before RPC's completion is flushed at RPC' completion (previously at next Write). Removed method Destroy() to force usage of intrusive_ptr on PA.
r33804 jge  "Connecton:XX" in http is handled more properly.
r33804 jge  Add Socket::Duplicate to re-address an already-addressed socket more simply
r33804 jge  Add MurmurHash32V/Md5HashV to hash a key with multiple separate parts more easily
r33796 jge  Solve the issue that average message size does not count NULL input messages (which do not need Process() stage)
r33796 jge  ParseResult can contain a user-defined (constant) description of the error. Add PARSE_ERROR_ABSOLUTE_WRONG to stop trying other prococols.
r33796 jge  Move DestroyingPtr into destroying_ptr.h to make it reuseable by other classes.
r33796 jge  Make error checkings in ParseFromArray and ParseFromIOBuf in HttpMessage consistent.
r33796 jge  Fix the issue in ProgressiveAttachment that Write() met EOVERCROWDED before destrunction of the controller will return EOVERCROWDED constantly.
r33796 jge  Fix an issue in http that an empty input message is created just after a complete message, which causes bugs in progressive readings
r33796 jge  Add ProgressiveReader which is for reading very long or infinitely long responses. Currently only http protocol is supported. User reads the responses progressively after end of RPC.
r33793 czy  Fix a typo in macro DISALLOW_COPY_AND_ASSIGN
r33788 czy  macro DISALLOW_COPY_ANS_ASIGN is not required to be private since c++11
r33771 czy  Invoke take_sample() in the ctor of ReducerSampler so that the value of the first second would be involved
r33753 czy  Revert Changes of snappy which didn't perform well in practice
r33748 jge  Use protoc-gen-mcpack.forbcloud2 instead of protoc-gen-mcpack.forbcloud to verify an issue
r33745 jge  Add protoc-gen-mcpack.forbcloud2 for debugging purpose
r33740 jge  Print absolute path in debugging log
r33737 jge  Revert r33677 and add debugging log for plugin
r33719 czy  Public missing headers to output
r33710 czy  Sync snappy with 1.1.3-14-g32d6d7d of master in githup, which performances much
r33710 czy  better in benchmakr
r33698 jrj  Fix dependency in BCLOUD
r33691 jge  Fix the warning reported in RPCHELP
r33688 jge  Fix a comparison warning in gcc 3.4
r33685 jge  fix warning in gcc 3.4
r33684 jge  Add IOBufBytesIterator to iterate bytes inside an IOBuf efficiently.
r33684 jge  Add IOBufAppender to append small data into IOBuf much faster than appending to IOBuf directly
r33683 jge  Support converting RTMP streams to MPEG2-TS
r33683 jge  ProgressiveAttachment is manageable by intrusive_ptr so that NotifyOnStopped can be used without race conditions
r33683 jge  Make some names in rtmp.h more unified and predictable
r33677 jge  Not update protoc-gen-mcpack.forbcloud in COMAKE to try to find the root cause of plugin failures
r33667 czy  Lazy creation of client related vars to avoid accessing the vars after destruction
r33664 czy  Implement add_count & reset to CountdownEvent
r33658 czy  Support sofa-pbrpc for java
r33647 jge  Fix incorrect usage of ignore_eovercrowded in Socket.Write brought in r33625
r33644 jge  Suppress warnings for empty messages (no fields)
r33625 jge  Fix the bug of ProgressiveAttachment that Write does not handle EOVERCROWDED correctly.
r33621 czy  Make ProgressAttachment work with HTTP/1.0 (which is the protocol of wget)
r33614 czy  Suppress compile errors with gcc4.4
r33600 czy  Fix the race when fd_count reaches the limit
r33591 jge  Make critical sections of bthread_id_list_reset small with new API
r33588 jge  Add bthread_id_list_swap/bthread_id_list_reset_pthreadsafe/bthread_id_list_reset_bthreadsafe to make critical sections on resetting lists small.
r33582 jge  Update generated files according to new protoc-gen-mcpack
r33582 jge  Rename ubrpc_protocol.h to ubrpc2pb_protocol.h to avoid confliction with the one in baidu-rpc-ub
r33570 czy  Add bthread_stack_count into vars
r33564 jge  Not do begin_xxx_array/end_array when the array is empty.
r33564 jge  Fix buggy OutputStream.backup which causes cancellation of empty array wrong.
r33564 jge  Fix a variable name confliction in loops (both use i)
r33563 jge  ListNamingService supports tag
r33563 jge  Fix a global registration issue in PartitionChannel and SelectiveChannel
r33560 jge  Move dependency on mcpack2pb up
r33559 jge  Polish comments on some classes.
r33559 jge  Make RegisterProtocol/FindProtocol/ListProtocol thread-safe.
r33559 jge  Loose check on id and order of fields in ubrpc, polish logs.
r33554 jge  Add protocol "ubrpc_mcpack2"
r33542 jge  Moved has_epollrdhup.* into details
r33542 jge  Merged DynamicPartitionChannel into partition_channel.h, partition_channel_base.* and dynamic_partition_channel.* were removed as a result.
r33542 jge  Limit frequency of logging of failed accept
r33542 jge  Elaborate comments on PartitionChannel and slightly adjusted the interfaces which is NOT compatible with previous versions (considering the users of partition channels are rare).
r33533 jge  Move lowercase code to media-server to make rtmp lib more general
r33524 jge  not lowercase query strings in rtmp urls
r33521 jge  Support postfix matching in restful mapping, including extensive changes to server.cpp/restful.cpp. Some subtle bugs on matching are fixed as well.
r33521 jge  Removed HttpMessageSerializer.set_chunked_mode() which is replaceable by not calling set_content()
r33521 jge  Fix an issue that failed http call that created progressive_attachment does not print warning. Body of failed http response is not compressed as well.
r33507 czy  Make size of RefCounted unchanged whether NDEBUG is on or not
r33499 jge  Not remove protoc-gen-mcpack.forbcloud at make clean
r33498 jge  Add -rtmp_server_case_sensitive which is false by default
r33462 czy  Add process_work_dir into default variables
r33447 jge  Fix warning in gcc 3.4
r33446 jge  Slightly changed impl. of OnVersionedRPCReturned so that retrying does not involve context switches and makes deadlock avoidance code for -usercode_in_pthread more unified.
r33446 jge  Print (partial) binary content of unknown messages.
r33446 jge  Call GlobalInitializeOrDie() inside Channel.Init instead of ctor to solve the issue that a globally declared Channel forces client-side initializes before gflags.
r33446 jge  Avoid deadlock when -usercode_in_pthread is turned on by running user callbacks in a backup thread pool when #concurrency reaches a threshold. bthread_set_worker_startfn applies to threads in the backup pool as well.
r33446 jge  Add an additional thread to default value of ServerOptions.num_threads, limit num_threads to be above minimum of bthread concurrency.
r33443 jge  signal num_task instead of 1 when BTHREAD_SIGNAL_TYPE = 1
r33443 jge  Fixed a bug that the upper bound in id_exists on bthread_id was wrong.
r33443 jge  Add bthread_id_about_to_destroy which stops useless contentions on a bthread_id which will be destroyed soon.
r33427 jge  AddClientStream before checking _destroy to suppress fatal logs
r33424 jge  Add gflag -log_hostname to add [host=...] after each log so that we know where logs came from when using aggregation tools like ELK
r33415 jge  Fix the race between Destroy and Create of client streams
r33415 jge  Close the connection when deleteStream was failed to send
r33409 jge  Send deleteStream in OnStopInternal instead of Destroy
r33401 jge  Change precision of retry_interval/max_retry_duration to milliseconds
r33393 jge  Add cut_size_megabytes and cut_interval_minutes to ComlogSinkOptions, elaborate comments.
r33389 czy  Do not set host and schema if there's not a full authority in URI
r33385 jge  Fix a racy issue in test_rtmp.cpp during dtor.
r33384 jge  switch_trunk updates to head regardless of code changes
r33374 jge  Slightly adjust checking of RTMP protocol
r33373 jge  Make PrintedAsBinary more general
r33365 jge  Fix a crash due to NULL sending_sock to OnStreamCreationDone
r33363 jge  Reuse chunk_stream_id to make space of RtmpContext._cstream_ctx small
r33363 jge  public/baidu-rpc and all examples compile with frame pointers
r33363 jge  limit #retry of writes for sending rtmp commands
r33363 jge  fix the leak of RtmpTransactionHandler when createStream was failed without receiving _result or _error
r33359 jge  public/bthread compiles with frame pointers
r33353 jge  public/bvar compiles with frame pointers
r33347 jge  public/common compiles with frame pointers
r33347 jge  Fix a issue in comlog_write that the length to %.*s is not int.
r33346 jge  public/protobuf-json compiles with frame pointers
r33341 jge  public/murmurhash compiles with frame pointers
r33338 jge  public/iobuf compiles with frame pointers
r33329 jge  Suppress getStreamLength and _checkbw warnings. (in r33314)
r33329 jge  Send StreamNotFound or close connection according to RtmpConnectRequest.stream_mulplexing and flag -rtmp_server_close_connection_on_error, the functionality is enclosed in RtmpStreamBase.SendStopMessage() (in r33314)
r33329 jge  RtmpRetryingClientStream supports RtmpClientSelector to use different RtmpClient for each retry, it also supports max_retry_duration to end up retrying.
r33329 jge  RtmpClientStream.Init is not blocking (by default) now.
r33329 jge  Retries writing when meeting EOVERCROWDED
r33329 jge  Replace RtmpClientStream.Create with Init which is easier to use. (in r33314)
r33329 jge  Removed example/media_server
r33329 jge  Make the assertions in ~SocketMap more accurate.
r33329 jge  Fix a bug that RtmpServerStream cannot be destroyed before the connection is broken.
r33329 jge  Calls RtmpClientStream.OnStop after receiving StreamNotFound
r33329 jge  Add RtmpStreamBase::OnFirstMessage() which is called in the same thread just before the first OnXXX() (in last CI)
r33329 jge  Add DestroyingPtr<T> to manage any streams inheriting from RtmpClientStream. (in r33314)
r33329 jge  Add connection_timeout_ms in RtmpConnectRequest
r33323 jge  make output/include in module_info as well
r33315 czy  Temporarily resolve the link issue of Applications by adding a environment variable to disable building tools/
r33314 jge  parallel_http trims spaces from inputs otherwise channel may fail to init
r33312 jge  fix compilation error for ccover gcc
r33309 czy  Just build libbdrpc.a as dependency
r33306 jge  parallel_http reads url instead of address+path and gets urls from stdin if -url_file is empty
r33306 jge  Add missing PROTOC in BCLOUD/COMAKE which break compilation otherwise(compiles on my local env)
r33303 jge  Build tools by default (so that they can be fetch from the product repository)
r33303 jge  Add tools/parallel_http(trackme_server/http_client previously) to access many server in parallel
r33291 czy  Fix the race between the dctor of Percentile and SamplerCollector
r33275 czy  Make url always the same with what the user inputs
r33264 czy  Fix the bug that _agents was not actually clears in the dctor of AgentCombiner, which would cause the next and prev pointer is not initialized in the resused Agent
r33256 jge  Fix inclusions of rpc headers moved into /details
r33253 jge  SetFailed instead of ReleaseAdditionalReference in ReleaseReferenceIfIdle
r33250 jge  Move some files that should never be used by users into /details
r33231 jge  Add http_client.cpp in trackme_server to speedup fetching of /version pages.
r33231 jge  Add an option to disable builtin services
r33213 czy  Make the mutex in VarMap recursive to fix the bug that fd_count() might be blocked by deadlock if the actual fd number of this process reaches the limits
r33194 jge  Add RtmpStreamBase::create_realtime_us()
r33194 jge  Add helper class PrintedAsDateTime(int64_t realtime) which can be sent to ostream.
r33186 jge  Expose RtmpStreamBase::is_stopped()
r33186 jge  Delete OnStreamCreated in its Run
r33176 jge  Show internal connections in /connections as well
r33168 jge  Support strict array of AMF
r33147 jge  Fix a bug that is_tabbed added to Server.MethodProperty is missed in RestfulMap.MethodInfo -This line, and those below, will be ignored-- M    src/baidu/rpc/restful.h M    src/baidu/rpc/restful.cpp M    src/baidu/rpc/server.cpp
r33143 jge  Tabbed services are not shown on default port as well when internal_port is set.
r33143 jge  Rename ERTMPRECREATED to ERTMPPUBLISHABLE, OnRecreated() to OnPlayable() which is triggered at first creation as well.
r33143 jge  Removed user callbacks on StreamBegin/StreamEOF/StreamDry/StreamIsRecorded, which are rarely needed.
r33143 jge  binary data of sequence headers are printed.
r33113 jge  IOBuf with binary data can be printed to std::ostream
r33109 jge  Try other protocols when magic_num unmatches even if header is incomplete
r33102 jge  Overwrite mutable_nshead()->log_id if controller's log_id is set
r33099 jge  Remove callbacks on fmle of RTMP
r33099 jge  Expose Controller.has_log_id()
r33093 jge  Compress http response according to compress_type
r33073 jge  Support vhost in RTMP url
r33067 jge  Fix the issue that ERTMPRECREATED was returned at wrong timing
r33066 jge  Fix memory fence when iterating VLogSite
r33066 jge  Add BoundedQueue::pop_bottom
r33061 jge  SSL usages in RTMP support multiple versions of SSL (ABI-incompatible)
r33053 jge  Rename a function in RTMP handshaking for testing
r33038 jge  Some methods in RtmpServerStream remove async support (which is rarely needed).
r33038 jge  Socket::ShareStats never fails. Add fail_me_at_server_stop() to make RTMP/FLV streaming connections to break at server's stop.
r33038 jge  SocketMap::Insert may replace failed sockets without HC. As a correspondence, SocketMap::Remove requires expected_id to compare. Add a debugging flag -show_socketmap_in_vars.
r33038 jge  RTMP support separate connections for different streams (to be compatible with SRS and improve throughput of nginx-rtmp)
r33038 jge  RtmpStreamBase::OnStop will not run simultaneously with OnXXX to help users to write correct cleaning code much easier.
r33038 jge  Interfaces of AppConnect are better named. StreamCreator is better designed to enable simpler user impl.
r33038 jge  Fix all known referential/memory/lifetime issues around RTMP.
r33038 jge  Add RtmpRetryingClientStream to enable general retrying of client-side streams. RtmpClientStream is also destroyed by Destroy().
r33032 czy  Remove CHECK in SendFeedBack
r33022 jge  TrackMe channel uses short connections to make #connections of trackme_server low.
r33022 jge  Split _last_readtime_us and _last_writetime_us in Socket (_last_active_us previously)
r33022 jge  Improve display of /connections
r33022 jge  Derivative sockets do not reference the main socket to fix the issue that HC of main socket is delayed by existing derivative sockets. To accumulate IO stats into main socket, shared fields are grouped into SharedPart and reference counted.
r33019 jge  add bvar_revision
r33010 jge  Remove set_version from most examples
r32994 jge  fix an invalid access after deletion in hotspots service
r32978 jge  Add --empty_nshead_body_is_failure for LALB to work correctly with some servers
r32973 jge  Not ignore empty isomorphic array (not loadable by idl)
r32967 jrj  Fix a fatal bug when allocating buffer for IDL, which may happen when IDL structures are larger than 16k
r32958 jge  Fix the race between returning keytable and ending server by leaking a little memory
r32955 jge  Fix a memory leak in bthread_key
r32940 jge  Fix invalid access in SimpleDataPool::Reset
r32927 jge  Set controller to be failed when status of ITP response is non-zero at server-side.
r32927 jge  Initialize ServerOptions.rtmp_service and try other protocols when rtmp_service is unset.
r32902 jge  Sole StartDummyServerAt(w/o channel/server) may link profiler correctly
r32897 jge  Renamed PROTOCOL_NSHEAD_SERVER to PROTOCOL_NSHEAD which supports client as well (sending/receiving NsheadMessage). Rewrite clients in nshead_extension_c++ and nshead_pb_extension_c++ with the protocol (instead of relying on baidu-rpc-ub). The server side processes requests just before EOF as well(for MIMO clients)
r32897 jge  Renamed PROTOCOL_MONGO_SERVER to PROTOCOL_MONGO
r32897 jge  NsheadMessage inherits google::protobuf::Message
r32889 jge  Close connections for errors in ITP and pack/parse response even if status is non-zero
r32884 jge  Make _fullsize always sync with _size in mcpack::OutputStream
r32882 jge  Make IOBufAsZeroCopyOutputStream.BackUp more general(also fix potentially failed BackUp that should be successful)
r32881 jge  Suppress strict-aliasing error in gcc 4.4
r32867 jge  Limit max scaning in get_fd_count and not update process_fd_count if it reached limit ever.
r32857 czy  Fix the race between ctor of TaskControl and its vars
r32846 jge  Add virtual dtor for RtmpService
r32844 jge  Support retrying of proxying client streams in media_server
r32844 jge  Support generic parsing of AMF(not relying on protobuf), which is used for parsing metadata
r32844 jge  Support FLV serializing and streaming over HTTP (converted from RTMP stream)
r32844 jge  Renamed StreamInitiator to StreamCreator and move the interfaces into stream_creator.h
r32844 jge  Partially support progressive downloading(implemented as chunked mode) at server-side.
r32844 jge  A lot of refinements to comments and interfaces around RTMP.
r32844 jge  Add RemoveByForce in SocketMap
r32842 czy  Set connectionTimeoutMillis of netty options to 0 so that netty would not start a timer
r32842 czy  Make implementation compatible with old netty with version before 3.5 (in runtime)
r32842 czy  Close channel when connection fails
r32839 jge  Not serialize empty array with only header to make idl happy.
r32839 jge  Add more complete tests on loading/serialization.
r32836 jge  size to IOBufAsZeroCopyOutputStream.BackUp can be as long as ByteCount()
r32832 czy  Get 'Last Changed Rev' instead of 'Revision' to identify the svn revision
r32831 jge  Set ServerOptions.idle_timeout_sec correctly in tools/trackme_server
r32831 jge  Minus 1 ref(the callsite) from PrintSocket to make the value more intuitive to users
r32830 czy  Make version a property as well since version-plugin doesn't work when we make artifactId a property
r32826 czy  Add missing mvn deploy commands
r32824 czy  Add releash.sh to release different version of baidu-rpc-java depending on different version of protobuf
r32822 czy  Implemented per connection timedout
r32821 czy  Roll back netty version
r32814 jge  Use a different port in test_streaming_rpc which conflicts with UT of raft.
r32814 jge  Fix a referencing issue of RtmpClientStream
r32814 jge  Check queries to /hotspots more strictly to avoid injection attack
r32812 czy  Make NUM_REPLICA in chash as a flag so that different users can set differenct num_replicas according to the size of server cluster.
r32809 czy  Make connection timeout a property
r32806 jge  User can set http attachment by him/herself even if the response is a non-empty pb message.
r32806 jge  Print capacity of _id_wait_list in socket
r32806 jge  Avoid repeatly getting prototype in rpc_press
r32803 jge  Refactor bthread_id_list_t and bthread_list_t with ListOfABAFreeId<> to support dynamically growing capacity which has close relationship with concurrency of apps.
r32794 czy  Add resize
r32788 jge  verbose_level to macro VLOG_STREAM can be an expression
r32787 jge  Move some definitions from bthread.h to bthread_types.h
r32769 czy  Remove the version check of protobuf in ersda.pb.h (brought in r32761)
r32767 jge  Remove wrongly added dep on tcmalloc (brought in r32723)
r32761 wan  STORY=#519
r32761 wan  Add ERSDA protocol support in baidu-rpc-ub.
r32760 wan  STORY=#519
r32760 wan  Add ERSDA protocol support in baidu-rpc.
r32759 jge  Unmatched fields during parsing AMF does not stop the parsing.
r32754 jge  Unify and refine logging in rtmp code, add more stats on rtmp.
r32754 jge  Support reading ecma array of AMF0 by using ReadAMFObject. Fix a crashing issue of the parsing code when types unmatch.
r32754 jge  Support proprietary handshaking of Adobe to talk with flashplayer (jwplayer & cyberplayer), pause/unpause are supported. Solved a unintended-seek issue of cyberplayer.
r32754 jge  Support more messages in rtmp (BufferEmpty/BufferReady/closeStream/|RtmpSampleAccess/onMetaData...)
r32754 jge  Add example/media_server which is a simple impl. to host or proxy live broadcasting.
r32750 czy  Make the dctors of the base classes of RTMP as virtual
r32748 czy  Try to flush_nosignal_tasks to when rq is full to avoid that the caller started too many nosignal threads
r32746 czy  Use CHECK instead of LOG(FATAL) to print the error message when id is invalid to get the call stack
r32745 jge  Create short sockets by calling Socket::GetShortSocket which sets _main_socket of the short socket. CreateShortSocket in socket_map.h is removed.
r32743 jge  Update pb.cc relying on mcpack2pb
r32743 jge  Unify SupportGzip
r32743 jge  Set method at server-side
r32743 jge  Compress /connections
r32731 jge  Suppress strict aliasing warning of newly added disp protocol
r32727 jge  Expose FieldMaps globally which may be referenced in parsing code of another CU
r32716 czy  Add bthread::CountdownEvent which provides a synchronization primitive that a threads would be block until the counter reaches 0
r32712 czy  Add butex_wake_all_and_remove_ref
r32711 jge  Minor changes to RTMP
r32708 czy  Revert temporary version of rpc with pb2.5 in r32702 (svn merge -r 32702:32668)
r32707 jge  Send sequence header to new player in UT
r32707 jge  AVC/AAC messsges can be sent directly
r32706 jge  Parse video and audio message in RTMP further
r32702 czy  Temporarily release a java rpc with the meta protobuf compiled with protoc2.5 for the Spark users who use table  API
r32695 jge  Still update servers for WEBFOOT_SERVICE_BEYOND_THRSHOLD error in bns
r32693 jge  Set chunk_size_out in the right order.
r32693 jge  Response back via the same numbered chunk stream of requestor to make flashplayer happy
r32693 jge  Add OnStop in server stream
r32692 czy  Remove unused codes
r32692 czy  Add some vars to help trace usage of butex and execq
r32683 czy  Replace maven-assembly-plugin with maven-shade-plugin in example
r32668 jge  Support RTMP (both client and server). AMF does not support strict/ecma array which is not used yet. Audio/Video formats are not decoded yet.
r32668 jge  SocketMap has a reuseable class now.
r32668 jge  Add stream_initiator to Controller to generalize the creation of streaming connections.
r32668 jge  Add a option `app_connect' to Socket to establish user-level connection after TCP connection.
r32654 czy  Remove test_lock_timer.cpp which was added by mistack in r31832
r32652 czy  Add scoped_refptr::release()
r32622 czy  Remove BAIDU_DERECATED in thread_local considering backward compatibility (a lot of users complain about it).
r32615 czy  Not clear data when the Socket is full
r32615 czy  Handle EOVERCROWDED raided by the host socket of Stream
r32612 jge  Not update process/system wide bvar when reading failed
r32611 jge  fix comment
r32602 czy  Use PLOG_EVERY_SECOND when failing to mmap stack for bthread
r32591 czy  Add back AddWatcher wihtout NamingServiceFilter back, which was removed 3617391, to make it compatible with the old version
r32589 czy  Complete the missing definition of the destructor of FunctionClosure0
r32583 jrj  Close connection when response has connection:close header
r32582 czy  Make the TimerTask of Controller as a static anonymous-class which would not hold a reference of Controller
r32560 jrj  Add NamingServiceFilter to filter ServerNode from NamingService The underlying NamingServiceThread will still be reused
r32536 jge  Add a set of options to run init fn in bthreads before server runs
r32533 jge  Not overwrite content-type if user already set
r32531 jge  Replace CFATAL_LOG in public/common with LOG()
r32529 jge  Fix thread-unsafe usage of vector.size in SocketPool.ListSockets
r32522 czy  Remove the unused SupportSSL interface of SocketConnection
r32519 czy  Replace thread_local with BAIDU_THREAD_LOCAL
r32516 czy  Replace thread_local with BAIDU_THREAD_LOCAL
r32514 czy  Deprecated thread_local definition before c++11 in case that a header file expose this kind of variables to the objs that are not compiled by the same c++ version, where there is likely a ABI incompatibility issue
r32514 czy  Define BAIDU_THREAD_LOCAL to replace thread_local, which is kept the same between c++03 and c++11
r32513 czy  Replace thread_local with __thread explicitly to make the the ABI is compatible between c++11 and c++03
r32509 czy  Fix the bug that a succesfully cancelled high priority task would make execute loop infinitely
r32508 czy  Print ERROR every second when rq is full
r32504 czy  Specialize std::swap for IOBuf
r32495 czy  Make changelog skip TBR= && TBR_REASON=
r32494 czy  Create HashWheelTimer explicitly so that the running thread is deamon which wouldn't block the whole process from shutting down
r32493 czy  Set timer thread as deamon && set name for each thread
r32490 czy  Correct the memory fence when modifing _join_butex to guarantee the visibility between the execute thread and join thread
r32489 czy  include bthread/mutex in bthread.h for the C++ users so that they can use bthread_mutex_lock/unlock in the RAII way without including bthread/mutex explicitly.
r32487 jge  Rename PARSE_ERROR_BAD_SCHEMA to PARSE_ERROR_TRY_OTHERS in ParseError and only try other protocols for the error, generalize the last error to PARSE_ERROR_NO_RESOURCE.
r32486 jge  Turn ChannelOptions.succeed_without_server on by default
r32486 jge  Set tailing size automatically in trackme_server scripts
r32485 czy  Minor refine the comments of ExecutionQueue to make them more consistent with newly changes
r32472 jge  Cover all *_EVERY_N and *_FIRST_N of glog and add much more variants.
r32469 jge  Fix out-of-range issue in tools/rpc_press
r32469 jge  Fix invalid path in PROTOC in tools/makeproj
r32467 jge  Suppress glog macros in base/logging.h
r32462 czy  Fix compile error in gcc345
r32460 jge  Remove FLAGS_dummy_server_port_file & fixes according to bthread changes
r32458 jge  Remove global boost::atomic in bthread
r32452 jge  Be ABI compatible with protobuf-json@1.0.0.0
r32437 jge  Try to convert container port of jpaas to host port before sending trackme requests
r32435 jge  Show loadavg as default vars.
r32435 jge  include missing boost/atomic.hpp in bvar/status.h
r32424 jge  Print log for unset required fields in parsing
r32420 jge  Support nshead_mcpack protocol at both client and server side
r32418 jge  Add a target to automatically update protoc-gen-mcpack.forbcloud
r32416 jge  Add back suppression of unused warning on format
r32413 jge  Update protoc-gen-mcpack.forbcloud
r32412 jge  Serialization of mcpack2pb supports mcpack_v2
r32410 czy  Changes of Stream according to the changes of ExecutionQueue
r32406 czy  Fix the bug that urgent and inplace task might make execq loop infinitely (which is used by nobody at present)
r32406 czy  Add TaskIterator and pass it to the execute function instead of a task array.
r32406 czy  Add exection_queue_cancel which allows user to cancel a previously submitted task.
r32406 czy  Add bvar `bthread_execq_running_task_count' which shows the count of running tasks from all the ExecutionQueue in the process
r32403 czy  Add gcc_version to default variables
r32401 jge  Rename base::Timer in base/timer/timer.h to base::TimedTask
r32401 jge  Add LOG_ONCE LOG_EVERY_N LOG_EVERY_SECOND for limited logging
r32399 jge  Add -immutable_flags to prevent gflags on /flags page from being modified.
r32398 jge  Show bthread_revision in vars
r32391 jge  Add RedisRequest.AddCommandByComponents which is immune to escaping issues.
r32386 jrj  Increase nargs when formating strings inside RedisCommand::AddCommand
r32384 jge  Pass request_code to sub channels in ParallelChannel
r32382 jge  fixed compilation error in r32381 (under gcc3.4)
r32381 jge  No formatting conversion specifiers for AddCommand w/o args
r32378 czy  Copy a fast crc32 implementation from RocksDB with minor changes to be compatible with c++03 and gcc3.4.5
r32378 czy  Add compile options -msse and -msse4 for gcc482 and gcc446
r32328 czy  Add Controller::latency_us() and use this interface to calculate latency instead of using base::Timer in examples 
r32318 jge  BCLOUD works for all protoc versions according to new bcloud
r32314 czy  Mark the threads in ThreadPool as daemon because ShutdownHook is not invoked when the main thread returns.
r32311 czy  Add backing_block_num() and backing_block() to allow users to construct a read-only buffer array.
r32305 jge  Add a flag for trackme
r32302 czy  Fix the bug that ExecutionQueue might loop infinitely when using TASK_OPTIONS_INPLACE with high concurrency
r32301 jge  Show rtt of tcp_info on /connections, show local port of client sockets on /connections, show tcp_info in /sockets
r32301 jge  ParallelChannel still succeeds when #failed-sub-channels does not reach fail_limit
r32295 czy  Fix the bug that bthread::Mutex is not initialized if NDEBUG is on
r32287 czy  Remove depereacted xml files
r32287 czy  Add version plugin of maven for releasing
r32285 jge  Fix plugin to protoc in BCLOUD
r32283 jge  Add protoc-gen-mcpack.forbcloud temporarily for test
r32282 jge  temporarily depend on local protoc-gen-mcpack
r32281 czy  Fix the bug that IOBuf.Output.flush touch a null buffer when no writing happens before flush is called
r32279 jge  The BCLOUD does not support plugin to protoc yet, src/baidu/rpc/itp_header.pb.h(cc) will not be updated when protoc changes
r32279 jge  Support ITP, added example/multi_threaded_itp_c++
r32279 jge  Replaced Server.unique_service() with first_service().
r32279 jge  Merge Zlib functions into gzip_compress.cpp/h
r32270 czy  Remove execution_queue.cpp, moving the general options definition into the header file so that it can be used before main()
r32270 czy  Add TASK_OPTIONS_INPLACE which hints execution_queue_execute to run user callback immediately instead of starting a bthread if possible
r32261 jge  Allow absent fields and add an option to disallow (for debugging)
r32258 czy  Restrict the sizeof TaskNode as mutiple of 64 instead of 64 exactly
r32257 czy  Call Global.init in ChannelOptions
r32256 jge  mcpack::MessageHandler.parse supports prefix parsing
r32256 jge  Fix namespace issues during linking. Fix one serialize_to_iobuf to serialize_to_string
r32254 jge  NsheadService is able to not send response by calling done->DoNotRespond()
r32252 jge  Parse log-id in http strictly with strtoull instead of atol which does not accept 64-bit integer
r32249 czy  Use addLast/pollFirst explicitly instead of push/pop to ensure the order as FIFO (which was LIFO as push behaviors as addFirst)
r32248 czy  Print fatal once after failing to mmap
r32243 jrj  Support old json format for protobuf map which is an array
r32228 czy  Sync the change of gtest to BCLOUD
r32226 czy  Replace third-64/gtest with thirdsrc/gtest because the former one might make some tests which are compiled with GCC4.4 in centos6u3 crash, although the root cause in unknown right now
r32225 czy  Make bvar compilable with GCC4.4 in centos6u3
r32224 czy  Make bthread compiled with GCC4.4 in centos6u3
r32215 jge  Sync BCLOUD of mcpack2pb with COMAKE
r32212 jge  Use new interfaces of mcpack2pb in ubrpc_compack_protocol
r32212 jge  Add a multi-threaded example of ubrpc_compack
r32210 jge  Refactored mcpack2pb to double performance of parsing and serializing. Previous impl uses IOBuf as the intermediate structure and costs significant time on ref/deref of IOBuf blocks, cutoff/concatenate of BlockRefs etc. Current impl manipulates ZeroCopyStream directly, combining with misc optimizations, the performance is doubled. Notice that this is a breaking CI that most interfaces are changed.
r32206 jge  Show concrete name of nshead_service in server-socket-section of /connections
r32206 jge  Remove SubSocketPool to simplify the code on pooled sockets
r32206 jge  Fixed the extra-minus-one bug on _count(http://gitlab.baidu.com/gejun/baidu-rpc-development/issues/217)
r32204 jge  move non-public functions of protobuf_json into namespace
r32202 jrj  Replace consecutive append with string_appendf
r32195 jge  Add -pb3 to convert maps in idl to maps in pb3.0
r32195 jge  Accept additional spaces in method declartions.
r32193 jrj  Revert ways of converting json to pb (use temporary DOM tree now) since the cost of a temporary DOM tree is less than that of finding fields in pb. Pb2Json remains unchanged
r32187 jrj  Use openssl/ossl_typ.h for some versions of openssl to include definitiion of SSL_*
r32183 jrj  Rename methods to use camel case in ub.pb.h so as to avoid confliction with idl-generated ones for class such as UBCompackRequest
r32170 jrj  Fix a bug that `count' has not been updated inside SubSocketPool::GetSocket
r32164 czy  Fix the bug that bthread_cond_broadcast may make bthread_mutex_unlock crashes when the corresponding bthread_cond_t or bthread_mutex_t are used by pthreads and bthreads
r32163 jrj  Support for protobuf-map struct to json object instead of objects inside array
r32163 jrj  Remove temporary DOM tree during serialize/parsing json. However, accordin to UT, this only improves perfomace by 5% mainly because the hotspot is inside the json parser i.e. ParseString
r32163 jrj  Performance improvements on ZeroCopyStreamReader which brings 5% increase
r32162 jge  Use remote_side in trackme_server instead since many reported ip are inaccessible
r32162 jge  /health page is customizable by inheriting HealthReporter.
r32162 jge  Change max unwritten bytes of socket to 8M
r32162 jge  Allow non-NULL response w/o any fields when content-type is not application/json
r32157 jge  rpc_press: Separate json inputs by tracking closed braces (no separator is needed), different threads try to send different requests simultaneously.
r32151 jge  Fix signature of GzipDecompress
r32141 jge  trackme_server prints logs with comlog so that the log file is deletable.
r32141 jge  Move pooled sockets' management into Socket so that contentions on the global socket map are reduced. Pooled sockets accumulate stats into their main sockets directly
r32136 czy  Add setuptools.egg to make the Python module compiled if users didn't install this util
r32130 jge  Enable accessing /Service/Method/Anything again
r32129 jge  Use SetHeader instead of AppendHeader in builtin services
r32127 jge  Rename own_rpc_dump_meta to reset_rpc_dump_meta which is more conventional
r32123 jge  Each thread in rpc_replay only sends requests at positions matching thread index.
r32123 jge  Controller owns RpcDumpMeta which is used in packing and should be along with controller.
r32119 jge  Remove return type of serialize/pack of ub protocol
r32118 jge  Show server's address after client-side error.
r32118 jge  Remove return value of SerializeRequest & PackRequest
r32118 jge  Limit unwritten bytes of each socket. If the limit is reached, Write fails with EOVERCROWDED
r32118 jge  Fix crashing rpc_press
r32115 jrj  Support protobuf 2.4+
r32111 jge  Improve layout of tool/trackme_server/list_names.sh
r32111 jge  Add Controller.retried_count()
r32109 czy  Complement source comments
r32109 czy  Add UT
r32106 jge  Still accept non-full service name in baidu_std protocol
r32101 jge  Clean up breaking issues in examples and make the code more readable
r32097 jge  Warn busy if GlobalUpdate thread did not sleep consecutively.
r32097 jge  trackme_server corrects 0.0.0.0(caused by bug in r32001) with remote ip. Add list_names.sh and list_versions.sh to tools/trackme_server
r32097 jge  /Service/Method/XXX is forbidden for accessing Service.Method
r32097 jge  Replace HttpHeader.method_path() (deprecated, not removed yet) with unresolved_path() which works for restful mapping as well and being normalized.
r32097 jge  Remove the "content-encoding: gzip" (set by user) before returning a textual error page, which can't be parsed by web browser before.
r32097 jge  Removed _service_mutex from server which is not thread-safe by design.
r32097 jge  MethodProperty.status can be NULL.
r32097 jge  Fix bugs in Server.RemoveService. Failed AddService restores server states.
r32097 jge  errorous SetHttpURL or operator= on uri() make RPC fail.
r32097 jge  AddService adds a parameter called `restful_mapping' for customizing the accessing URLs for methods. It includes extensive changes to service registration, http processing, builtin service displaying... Wiki: http://wiki.baidu.com/display/RPC/Server#Server-RestfulURL
r32095 jge  Add StringPiece.trim_spaces & Status.swap,Specialize hash for ptr
r32093 czy  Fix bugs around attachment
r32088 czy  Remove java/example, all examples are listed in exmaple
r32088 czy  Minor changes on IOBuf
r32087 czy  Complement multi_threaded_echo_java
r32086 czy  Add interfaces for retry (NOT implemented)
r32086 czy  Add Channel.close which is required to be called by the users where the very Channel is no longer in used
r32085 czy  Some random minor changes
r32085 czy  Implement HULU protocol for java
r32082 czy  Fix the bug that normal tasks of ExecutionQueue might be wrongly dropped when there are urgent tasks
r32076 jge  fix conversion issue of rpc_replay
r32075 jge  Remove deps on tcmalloc from tools/rpc_replay
r32067 jge  Changed some methods of base::Status(not used yet) to conduct more accurate semantics
r32063 jge  Assign FLAGS_output to PressOptions.output
r32055 jge  Change stack size according to sizeof(com_device_t) which is very large in ullib before 3.1.112.0
r32047 jge  Fix double to integral conversion warnings in rpc_press_impl.cpp
r32044 jge  Make RedisRequest printable
r32037 jge  Add -redis_verbose to print redis requests/responses while debugging
r32035 jge  Add tools/add_syntax_equal_proto2_to_all.sh to add syntax="proto2" & add the prefix to all proto files
r32034 jge  Support protobuf 3.0, namely protos are always re-generated after comake2; syntax="proto" is added to all protos; baidu::rpc::NewCallback replaces google::protobuf::NewCallback which is internal in 3.0; reflection is removed from RedisRequest/MemcacheRequest/SerializedRequest since the API is changed. Arena allocation at server-side is not supported yet.
r32032 jge  Add syntax="proto2" in proto files to support protobuf 3.0 (public/mcpack2pb)
r32030 jge  Easier compilation with C++11 (public/bthread)
r32027 jge  Check validity of request_code in ConsistentHashingLoadBalancer & Rename Md5Hash32 to MD5Hash32
r32025 jge  Add get_method_name=NULL for ubrpc_protocol
r32023 jge  Support C++11(public/baidu-rpc). To compile with C++11, export ENV_CXXFLAGS=-std=c++0x in your shell
r32021 jge  Support C++11 (public/bvar). To compile with C++11, export ENV_CXXFLAGS=-std=c++0x in your shell.
r32018 jge  Make sure public/protobuf-json can be compiled with C++11. To compile with C++11, export ENV_CXXFLAGS=-std=c++0x in your shell
r32018 jge  Fix UT of public/protobuf-json
r32017 jge  Fix warning under C++11 (public/common). To compile with c++11, export ENV_CXXFLAGS=-std=c++0x in your shell.
r32015 jge  Support accessing redis-server.
r32015 jge  Submit span of sync RPC after joining to get more accurate "resumed" timestamp
r32015 jge  Separate the mutex used by Socket._pipeline_q, Replaced PeekPipelinedInfo with GivebackPipelinedInfo to reduce contention in most cases
r32015 jge  Move received timestamp of rpcz before Read, and rephrased some output of rpcz
r32015 jge  Fixed the issue that if parent RPC is traced, current RPC may probably be not traced.
r32015 jge  Add GetMethodName callback in Protocol.
r32012 jge  Add Arena which is a proof-of-concept to make redis replies' allocations more efficient
r32011 jge  Add cut1 and fetch1 to IOBuf
r32009 jge  responses convertible to json must have fields. This requirement simplifies the code.
r32009 jge  Replaced Server::Start(ip_str, start_port, end_port, options) with Start(ip_str, PortRange, options) to avoid the possible confusion with multi-port Start() that we may add in future CI.
r32009 jge  Replaced -print_http_request with -http_verbose which prints sent/received requests/responses like curl -v.
r32009 jge  Replaced AppendHeader[IfAbsent] with SetHeader/RemoveHeader/AppendHeader(behavior changed). Set/appended headers are always getable. This is a must for copying HttpHeaders around as a whole.
r32009 jge  Point trackme server to brpc.baidu.com
r32009 jge  Added rpc_view which makes builtin services of server running outside [8000-8999] accessible from web browsers. ServerOptions.http_master_service is added as the prerequisite for rpc_view.
r32009 jge  Added RetryPolicy for customizing the retrying behavior. As a correspondence, HTTP_STATUS_FORBIDDEN is not retried by default.
r32007 jge  Remove bthread_about_to_quit & Review log levels
r32005 jge  Initialize PartitionChannelOptions::fail_limit and other minor fixes according to coverity
r32003 jge  Fix potential overflow in PercentileInterval::merge according to coverity
r32002 jge  Server sends my_ip() instead of listen_address() in trackme request since the latter one is often 0.0.0.0
r32002 jge  Not call SIG_IGN(crash program) in quit_handler in server.cpp
r32002 jge  Change all LOG(TRACE) to LOG(INFO) and review all LOG(FATAL), change some of them to ERROR or WARNING.
r32002 jge  Add maintaining scripts of tools/trackme_server and example/cascade_echo_c++
r32000 jge  Make the code for getting revision more robust
r31998 jge  Programs linked with baidu-rpc will send TrackMeRequest to trackme_server periodically. The pinging is carefully designed and with negligible performance impact to users' programs.
r31998 jge  Fix an issue in NamingServiceThread that a fail-to-start thread is not deleted.
r31998 jge  bthread_about_to_quit() calls are moved from protocols to controller.OnRPCReturned
r31992 jge  Add v1.pb.* and v2.pb.* in test
r31989 jge  Add missed proto files in test
r31987 jge  Rewrite comments on NsheadPbServiceAdaptor to make users more sensible about the behavior of the methods.
r31987 jge  Remove ambiguous hinting of methods, latter registered service overrides the former one. The ambiguous hinting may make services refuse to work when users accidentally add a same service in another package.
r31987 jge  Optimized some code in http processing to reduce memory allocations of strings.
r31987 jge  No matter if Content-type is set to applcation/json or body is empty, treat body as a json to make pb service always accessed with valid requests.
r31987 jge  Fix the issue that http error was not inlcuded in the error text, which was brought in r31923
r31986 jge  rpc_relay: Add -inc to include paths for proto, merge -service into -method, make logs more accurate.
r31984 jge  Add missing header inclusion in protobuf-json
r31981 jge  Load json from file or string separated by semicolon
r31977 jge  QPS of replaying is limitable
r31977 jge  Print stats during replaying
r31976 jge  Unify info thread of replay & press
r31966 czy  Change some log level (from FATAL/ERROR to ERROR/WARNING) so that users don't see FATAL when receiving packages from the scan of the secuirty group
r31965 czy  Specialize lock type for bthread_mutex_t
r31957 jge  Create dir with executable bit
r31956 jge  Remove tools/pbrpcpress (replaced by rpc_press in previous CI)
r31955 jge  Replace pbrpcpress with rpc_presss, which is much simplified and with more accurate qps and stats
r31954 czy  Minor fix
r31954 czy  Compile the java version of streaming_rpc_meta.proto
r31951 czy  Fix the bug that the order of InputMessengerHandler is not the same as ProtocolType
r31951 czy  Change some fatal to warning
r31940 jge  Add flag --ns to idl2proto to specify namespace of idl.
r31937 din  Start dumper thread if initially not
r31935 jge  Remove idl stuff from example/multi_threaded_echo_c++
r31935 jge  Fix ns issues in tools/pbrpcpress
r31935 jge  Add tools/build_closure
r31932 jge  Show rpcz when spandb exists even if rpcz is not enabled
r31932 jge  Replace alias with functions in scripts
r31930 jge  Simplify rpcz with bvar::Collector. Not-sampled spans are not created anymore which reduces overhead in large QPS programs.
r31928 jge  Add CollectorPreprocessor to process samples in batch before dumping
r31923 jge  Unify http error with EHTTP. ErrorCode==EHTTP means that http_response().status_code() should be checked.
r31923 jge  Set tracing info in http protocol
r31920 czy  Fix compile errors
r31918 czy  Remove using namespace base in namespace baidu to fix the ambiguous snappy issue
r31911 jge  fix an uninitializing warning in gcc 4.4
r31906 jge  fix test_builtin_service.connection of baidu-rpc
r31905 jge  Replace boost::intrusive_list with base::LinkedList (to reduce deps)
r31903 jge  Turn show_hostname_instead_of_ip off by default, which makes /connections generation very slow
r31901 jge  Simplify rpc_dump with bvar::Collector. Removed rpc_dump_ratio and rpc_dump_max_pending_requests.
r31901 jge  Show hostname instead of ip on /connections, configurable by show_hostname_instead_of_ip.
r31901 jge  Remove successful logs in naming services, print a brief log in NamingServiceThread instead.
r31901 jge  pprof.pl shows bigger-weight rectangle larger
r31901 jge  Fixed issues in /hotspots: changing options during profiling produces correct results, not generate results to closed connections.
r31901 jge  Don't round numbers on /vars plots.
r31901 jge  Add /hotspots/contention and /pprof/contention to view how much time is spent on waiting for locks and where're the callsites.
r31899 jge  Add missing deps in bcloud and fix warnings in gcc 3.4
r31898 jge  Add contention profiler to know how much time is spent on waiting for locks and where are the waits happening
r31894 czy  Start the execution thread in backgroud as a workaround of the deadlock issue when context-switch happens after aquiring a pthread_mutex
r31891 jge  Add bvar::Collector to unify sample collecting (used by rpc_dump, contention profiler, will be used by rpcz later)
r31885 jge  Added ip2hostname and endpoint2hostname
r31884 czy  Fix compile error with GCC3.4
r31883 czy  UT can be compiled by gcc3.4
r31883 czy  Add the correct test_lock_timer.cpp
r31871 czy  Supress warning
r31863 jge  Fix deps in BCLOUD of baidu-rpc
r31862 jge  Fix inaccurate compression perf on small messages
r31860 jge  fix linking in py
r31858 jge  Move code generation into generator.cpp(also replaced plugin_main.cpp) to remove deps on libprotoc.a
r31857 jge  Use thirdsrc/leveldb instead of third-64/leveldb
r31857 jge  Use base/third_party/snappy instead of third-64/snappy. Benefits: build from source, less deps, non-continuous memory support in uncompressing
r31855 jge  Clean up IOBufAsSnappySource/Sink inside baidu-rpc and move them into iobuf
r31853 jge  Replace some utility functions of snappy with ones in base.
r31851 jge  Publish headers of snappy in BCLOUD
r31849 jge  Add snappy 1.1.3 into base/third_party/snappy
r31839 czy  Add PROTOCL_DIDX_CLIENT for the client-side protocol requsting the DIDX server
r31837 czy  Fix a compile warning
r31835 czy  Make bthread_mutex_t measurable for LockTimer
r31832 czy  Refine LockTimer so that we can monitor pthread_mutex_t or bthread_mutex_t with LatencyRecorder
r31832 czy  Refine LockTimer so that we can monitor pthread_mutex_t or bthread_mutex_t with LatencyRecorder
r31832 czy  Add LatencyRecorder::latency_percentile(double ratio) to allow user get a special percetile value besides the builtin ones
r31832 czy  Add LatencyRecorder::latency_percentile(double ratio) to allow user get a special percetile value besides the builtin ones
r31830 jrj  Revert x-bd-log-id to log-id
r31829 jrj  Rename Log-Id to x-bd-log-id
r31829 jrj  Add header for x-bd-error-code
r31828 jge  Add the magical "wait for epollout on empty" back.
r31823 jge  Unify on-disk stuff of rpc into ./rpc_data
r31818 jge  Make merging of percentile intervals unbiased approximately.
r31818 jge  Change index mappings in get_interval_index(), which also fixes the out-of-bound bug for values > 2^31
r31818 jge  Add SeriesFrequency as the second template parameter to Window so that some window can get value by window(for bvar_dump?\194?\163?\194?\169 and get series by second(for ploting)
r31813 jge  Make fast_rand_in overloadable
r31809 jge  Rename fast_rand_between to fast_rand_in which is shorter and more precise
r31807 jge  Fix issues of domain_naming_service and UT
r31806 jge  Run on_cancel closure in new thread on failure branch to make sure Socket::SetFailed is not blocked.
r31806 jge  domain_naming_service allows and drops stuff after port.
r31806 jge  Channel::Init(ns, lb, options) treats ns as server_addr_and_port when lb is NULL or empty, which simplifies configuartions of generalized channels. Almost all examples have been added with FLAGS_load_balancer.
r31806 jge  Add tools/make_all_examples and tools/clean_all_examples
r31803 jrj  Add namespace to http_parser and move them into src/baidu/rpc
r31802 jge  dummy servers can be created easily by calling baidu::rpc::StartDummyServerAt(port).
r31802 jge  Add default timeout to ParallelChannel. fail_limit is also moved into ParallelChannelOptions.
r31798 jge  Warning busy of GlobalUpdate after consecutive 2 non-sleep cycles.
r31798 jge  Removed src/tinymt64.c(h) and src/baidu/rpc/random_number_seed.cpp(h) which are replaced by base::fast_rand
r31798 jge  Minor fixes for compilation warnings on linuxmint
r31798 jge  Fix connection_count on /status
r31796 jge  Set TLS _local_pool to NULL and decrement _nlocal at thread's exit.
r31796 jge  More options in TimerThreadOptions, including the prefix of vars and callbacks called at the begining/ending of TimerThread.
r31796 jge  Make timer thread global.
r31792 jge  Fix issues with empty isomorphic arrays. (The issue number is of the previous CI to bthread which wrongly used the number for this issue)
r31791 jge  Refactor TimerThread to be more scalable. The resulting impl. reduces 4% CPU and improve 10% qps for example/multi_threaded_echo_c++ with default options on 24-core machines. If the example uses 400 threads, qps is improved from 300k to 600k and the profiling result still does not show hotspots from TimerThread.
r31789 jge  warn busy after being busy for consecutive 2 times.
r31789 jge  Unify most types in interfaces of LatencyRecorder to int64_t
r31789 jge  Store agents in combiner with LinkedList instead of deque.
r31789 jge  Removed bvar/detail/rand.cpp(h) which is replaced by fast_rand
r31789 jge  Refactor percentile to reduce memory footprint and make samples collected truly unbiased.
r31789 jge  Add debug name for Recorder/Percentile to print more helpful log when input overflows. drop negative values to percentile
r31788 din  normalize appname
r31787 din  more info log
r31786 jrj  Degrade log level of SSL to warning
r31780 jge  Remove NDEBUG braches on linking InitLogging
r31780 jge  Add fast_rand based on xorshift128+
r31774 din  normalize names for OnDemandLatencyRecorderGroup.supply
r31773 din  Prepend appname to dump file
r31772 din  OnDemandLatencyRecorderGroup
r31760 din  Camel case conversion
r31757 czy  Add VLOG when failing to parse nshead
r31753 jge  Take second value for PerSecond
r31750 jge  Fix window_size in getting series data from window
r31737 din  GC uses window. Dump ignores null value.
r31736 jge  Fix the bug that sending backup request over a single-server channel addresses incorrect (SocketId)-1 and always fails.
r31736 jge  Fix client-side error code in rpcz that it's still EBACKUPREQUEST after a successful call.
r31736 jge  Add example/backup_request_c++ which may send backup requests to a server when it sleeps for a while for some requests.
r31734 jge  Add example/cancel_c++ which sends 2 RPC to server and accepts the first returned.
r31729 jge  Hide SSL headers included by iobuf.h from users
r31728 jge  Split InputMessageBase into input_message_base.h a and hide mongo_service_adaptor.h which exposes internal structures from users
r31728 jge  Rename num_xxx to xxx_count in baidu-rpc, add bvars: rpc_keepwrite_second, rpc_waitepollout, rpc_health_check_count
r31728 jge  Hide SSL structures (will publish IOBuf hiding openssl headers in next CI)
r31726 jge  Rearrange members of TaskGroup (to be more cache friendly)
r31726 jge  Batch signaling in butex_wake_all and butex_wake_except
r31726 jge  Add bthread_signal_count to monitor # of futex signaling.
r31724 jge  Replace num_xxx with xxx_count
r31720 jge  Export idl_options.pb.h in mcpack2pb
r31719 jge  ubrpc_compack services/requests can be declared within namespace.
r31719 jge  Fix a bug while reading the `error' section of ubrpc_compack
r31717 jge  Remove max_network_in/out in python
r31716 jge  Link profilers on demand with new techniques enclosed in profiler_linker.h. Dummy servers running in programs with only baidu::rpc::Channels can do profiling as well.
r31716 jge  Fix compilation issues in tools
r31715 jge  Add example on http service with dynamic path
r31714 din  publish source code
r31713 din  Programmable interface
r31711 jge  Not rewrite ERPCTIMEDOUT to ETIMEDOUT
r31704 jge  Remove several Makefile in examples
r31704 jge  Link libprotoc.a in python
r31702 jge  Not comake2 -UB in mcpack2pb/build.sh
r31700 jge  Add build.sh for mcpack2pb
r31699 jge  Link mcpack2pb in python
r31696 jge  Rename libmcpack.a to libmcpack2pb.a
r31696 jge  Convert idl method in form of "result fn(Request req, out Response res)" to pb
r31694 jge  Add bvar `bthread_switch_second' to watch switches between bthreads.
r31693 jge  Make example/echo_c++_compack_ubrpc mutually accessible with public/baidu-rpc/example/echo_c++_ubrpc_comack
r31688 jge  Publish headers differently in mcpack2pb
r31687 jge  Add public/mcpack2pb in BCLOUD
r31686 jge  User can access ubrpc(serialized with compack) services with protobuf directly, or write protobuf services accessible by ubrpc clients as well.
r31686 jge  Rename some functions in socket_map.h to be more readable. ReturnSocketToPool accepts Socket* directly to save an address.
r31686 jge  Add bvar "rpc_event_threads_second" to watch creation of event threads.
r31684 jge  Function adjustments for ubrpc adaption and fix the UT.
r31682 jge  IOBuf is comparable with string
r31675 czy  Fix UT
r31675 czy  Fix the bug that the fast path of bthread_mutex_lock was removed
r31673 czy  Let user address ExecutionQueue explicitly to imporve performance
r31670 jge  Tabs in builtin services can be customized by inheriting Tabbed.
r31670 jge  Some builtins services get server from controller rather than constructor.
r31670 jge  Graphs at /status are expanded when qps != 0, not previously count != 0.
r31670 jge  Add -DNDEBUG -Wno-unused-parameter to examples
r31669 din  trigger system info to dump 2
r31668 din  trigger system info to dump
r31666 czy  Fix the bug that pthread waiting on a butex may never wake up
r31666 czy  Fix the bug that a task might be missed when some thread quits
r31666 czy  Add some UT
r31663 din  some naming changes
r31658 jge  Support rpc dump & replay for baidu_std, hulu_pbrpc, sofa_pbrpc
r31658 jge  Make Controller::Reset() more readable and modifiable.
r31658 jge  flags can be reloaded in browser visually by clicking the (R) link.
r31658 jge  All rpcz flags start with "rpcz_"
r31658 jge  Add SerializedRequest which be used for building general proxy on top of baidu-rpc
r31655 czy  Add lock types and base::double_lock
r31654 jge  Fix UT on LinkNode
r31654 jge  Disable UT on SharedMemory and StatsTable which may need permission
r31652 jge  -Expose read_command_name()
r31651 jge  LinkNode points to itself after RemoveFromList
r31642 jrj  Set error code to EINVAL when request/response type doesn't match
r31636 din  dump file location according to noah
r31633 din  a ut failed on linux
r31630 din  java.lang.management mxbean
r31628 czy  Temporarily fix test_naming_service
r31626 czy  Fix the bug rpcz crashes when issuing rpcz in a dependent thread rather than in the server thread by replace bthread::tls_unique_user_ptr with bthread::tls_blk
r31624 czy  Add rpcz_parant_span in LocalStorage
r31620 czy  Fix the bug that consumers might never wake up
r31612 czy  Specialize std::unique_lock for pthread_mutex and pthread_spinlock
r31611 czy  Add the C++ wrappers for bthread_mutex bthread_cond
r31608 jge  server in example/http_c++ does not print log for each request.
r31608 jge  Replace baidu::rpc::RawPacker/Unpacker with base::RawPacker/Unpacker
r31606 jge  add raw_pack to base
r31605 din  window size +1
r31603 jge  Rename parallel read/write functions to avoid potential confusion and bugs
r31594 jge  Add linking options in UT of baidu-rpc-ub
r31592 jge  add linking options in baidu-rpc-ub examples
r31588 jge  idl2proto can be called from other dir.
r31588 jge  Generate StaticHandler<> for pb messages.
r31586 jge  Add linking options
r31585 jge  Fix linkage in gcc 4.8
r31578 czy  Remove openssl off dependencies
r31576 czy  Remove openssl off dependencies
r31575 czy  Remove openssl from dependencies
r31565 din  sample non reducer bvar
r31558 jge  Unify options setup in examples with the ones in baidu-rpc and make all examples compilable.
r31558 jge  the protocol is registered before main() or in ctor of first UBXXXRequest
r31557 jge  remove a frequently failed check in test_socket.cpp
r31552 jge  same errno can be registered more than once
r31537 din  timestamp while sampling
r31534 jge  fix overflowed reserve
r31532 jge  Set ChannelOptions for timeout/max_retry instead of controllers in examples.
r31532 jge  Replaced example/streaming_echo_c++ with Stream.
r31532 jge  Renamed max_messages_size to messages_in_batch.
r31500 jge  Remove mallinfo which is overflow in many online servers and may even stuck with glibc malloc.
r31500 jge  Minus 1 (caused by opendir) from num_fd
r31495 czy  Add cut_into_file_descriptor_at_offset
r31494 din  This line, and those below, will be ignored-- M    java/src/main/java/com/baidu/bvar/Config.java M    java/src/main/java/com/baidu/bvar/DumperService.java M    java/src/main/java/com/baidu/bvar/Variable.java M    java/src/test/java/com/baidu/bvar/func/DumpFileDefaultTest.java M    java/src/test/java/com/baidu/bvar/func/DumpFilePragmaticTest.java M    java/src/test/java/com/baidu/bvar/func/DumpFilePropertyTest.java M    java/src/test/java/com/baidu/bvar/func/LatencyRecorderFileDumpTest.java M    java/src/test/java/com/baidu/bvar/timing/WindowMaxerIntTest.java
r31494 din  dumper interval
r31488 czy  Update should-be-changed proto generated files
r31486 czy  Rename artifactId from bvar-java to bvar
r31485 czy  Add missing distributionManagement for deployment
r31484 czy  Add deploy configurations
r31483 din  com.baidu.bvar.func.DumpFileDefaultTest v2
r31476 din  Fixed failure of com.baidu.bvar.func.DumpFileDefaultTest
r31472 czy  Suppress gcc3.4.5 warnings
r31468 czy  Implement Streaming RPC
r31457 jge  Remove last_mtime which causes get_value malfunction when window_size changes
r31455 din  use buildnumber-maven-plugin
r31447 din  BVar Java initial implementation.
r31439 jrj  Fix ub dependency in example
r31438 jge  Use LOG(ERROR) instead of LOG(FATAL) in server.cpp
r31438 jge  EventDispatcher uses the same thread attribute of consumer threads (to avoid stack overflow caused by older comlog)
r31438 jge  Comment compression functions.
r31438 jge  Add tools/gen_changelog to compile changelog from all related modules
r31438 jge  Add status to Server and allow Server::Join() to be called before Stop(). Join() without Stop() does not print error log.
r31438 jge  Add SimpleDataPool::Reset() which is called in Server::Join() to make sure all session local data are destroyed before Join() returns, otherwise the data factory may be deleted by user after Join().
r31438 jge  Acceptor::ListConnections() clears input when server is not running/stopping.
r31432 jge  repeated primitive are marked with [packed=true]
r31432 jge  Parse/Serialize idl map with repeated <Message>.
r31432 jge  idl2proto does more strict replacing to reduce errors.
r31432 jge  function pointers in mcpack::MessageHandler are renamed.
r31432 jge  fields are parsed/serialized with the exact field name in idl (not lowercased ones in pb), and idl_name is supported
r31426 jge  Put idl constraints in comments
r31426 jge  Make idl2proto more strict
r31422 jge  fixes for bcloud and gcc 3.4
r31420 jge  Initialize mcpack2pb. The parsing and serializing code are more a proof-of-concept that a highly optimized one. Performance improvements will be done in future CI. Check doc at http://wiki.baidu.com/display/RPC/idl+%3C%3D%3E+protobuf
r31416 jrj  Add UT to check support of chunked encoding on server side
r31414 jge  Make buf in GetProcessName larger
r31413 jge  VLOG as separate LogStream. Add print_vlog_as_warning in ComlogSinkOptions.
r31413 jge  take basename of ProcessName
r31401 czy  Minor fix
r31387 czy  Revert former changes (svn merge -r 31385:31320 .)
r31385 czy  Replace impl of fd_guard with ScopedGeneric
r31385 czy  Disallow copy ctor of ScopedGeneric to fix the compile errors of UT
r31384 jge  Set Socket.local_side to 0 when the socket does not support the op.
r31384 jge  Make methods of ParseResult more readable
r31384 jge  fix mongo UT
r31382 czy  Add type cast to ScopedGeneric
r31378 jge  more fixes according to coverity
r31377 jge  add INVALID_BTHREAD
r31376 jge  some fixes according to coverity
r31371 jge  Fix issue in mongo protocols: return bad schema immediately when adaptor is NULL. Check op_code to reduce false reporting of "too big data"
r31371 jge  Add Server.RunUntilAskedToQuit() to ease nice stopping of server.
r31371 jge  Add -same_channel in example/parallel_echo_c++
r31361 jrj  Fix BCLOUD for protobuf-2.6
r31355 jge  /rpcz at server-side displays errno and stages correctly
r31355 jge  renamed "unresponded" to "processing"
r31355 jge  http should respond 400 for most bad http requests now.
r31353 jge  Unify reading on system metrics with CachedReader
r31351 jge  Allow duplicated sub channels in ParallelChannel
r31350 jrj  Generate Pb files during comake2 -P if protobuf-2.6 is used
r31345 czy  Add ExecutionQueue
r31343 jge  Shows rpc_revision in /vars, which is got from last changed revision of svn.
r31343 jge  Send 400 back to client and close the connection when http parsing fails at query-string, fragment and the following "HTTP/x.y"
r31343 jge  Fixed warnings in tools/pbrpcpress test/test_mongo_protocols.cpp
r31343 jge  Always loose checkings in some macros in http_parser.c
r31343 jge  Added benchmark_http to example/http_c++, which is a multi-threaded client for testing performance of http server.
r31335 jrj  Do not clear server list when NS::GetServers failed
r31335 jrj  Disable assertions related to LevelDB
r31328 jrj  Replace copy with copy_to in http example
r31328 jrj  Fix a bug when setting status of NsheadPbService
r31320 jge  Mark some functions in base/third_party/symbolize as weak symbols to avoid conflict with same functions in glog
r31286 czy  Fix the confliction of ScopedFILE
r31285 jrj  Disable SSL check if there is no SSL context
r31279 jrj  Update BCLOUD to make protobuf-2.6 work
r31269 jrj  Revert pb files
r31268 jrj  Remove protobuf auto-generated files
r31262 jge  Acceptor::ListConnections must clear input otherwise callsites are likely to leak memory
r31255 jge  add offset to copy_to
r31251 jge  Use int64 for mallinfo fields
r31247 czy  Make BCLOUD work
r31246 mix  support mongo protocol(https://docs.mongodb.org/manual/reference/mongodb-wire-protocol/)
r31246 mix  JIRA=NONE
r31244 czy  Fix the bug of iterative_murmurhash
r31229 czy  Add append_from_file_descriptor with off_t
r31226 jge  not check if bvar_dump_interval is reloaded (no way to check it correctly)
r31220 jge  information through internal_port is always kept as it is, namely ip/port are not hashed by md5, urls are not escaped etc.
r31220 jge  Expose WebEscape function.
r31215 jge  fix missing _server in http
r31210 jge  Make conversions between json and pb more reasonable.
r31210 jge  -log_connected and -log_connection_close are false by default.
r31210 jge  If user sets http-header "accept" or "user-agent", not replace it. If user does not set log_id (to controller), not set to header
r31210 jge  Add -log_error_text to print the error when server is about to respond the error to client.
r31206 jge  Minor fix on warning of lacking Stop() before Join()
r31205 jge  Set connection more correctly
r31203 jge  Add more debugging information to http
r31195 jge  Rename secure_port to internal_port which is more understandable
r31193 jge  Warn slow join so that users are more aware of what is going on when Join() hangs.
r31193 jge  Replace ServerOptions.security_mode with secure_port which is more useful
r31193 jge  Remove some duplicated make_non_blocking/make_no_delay
r31184 jge  Not override assert() in base/logging.h
r31182 jge  print (current part of) input when http parsing failed
r31180 jge  print warning for vlog since many online servers do not enable trace log
r31177 jge  use unique_ptr in example/asynchronous_echo
r31177 jge  not show default value when flag was changed but the value was not.
r31174 czy  Pass the local side of a connection to user
r31173 czy  -Add get_local_side/get_remote_side
r31171 jge  Added reserve() and unsafe_assign() to modify memory already appended.
r31171 jge  Added pop_back() to remove bytes from back side
r31171 jge  Added appendv to append multiple data in one call
r31164 jge  suppress warning in gcc 4.4
r31162 jge  suppress warning in gcc 4.4
r31127 jge  Use FILE instead of ifstream to avoid potential exceptions.
r31112 jge  Modify comment to emphasize bthread-local functions can be used in pthreads
r31111 czy  Fix COMAKE to make -UB work
r31109 jge  Rename "session data" to "session-local data" which is more consistent with "thread-local"
r31107 jge  Support thread_local_data() which is attached to current searching thread and a more convenient form of bthread-local at server-side. The feature is enabled by setting ServerOptions.thread_local_data_factory
r31107 jge  Support Controller::session_data() which is attached to current RPC session and reused between different RPC. The feature is enabled by setting ServerOptions.session_data_factory.
r31107 jge  Server threads reuse bthread::KeyTable via Server::_keytable_pool. bthread local functions are useable (efficiently) in server now.
r31107 jge  Add example/session_data_and_thread_local which is also an example of aysynchronous server.
r31107 jge  Added Controller::server() to know which server the RPC is in.
r31105 jge  Refactored bthread local related functions. A lot of features and bug fixes, namely: 1. bthread_key_t is reused, using versioning to detect ABA. 2. keytable becomes 2-tier array(deque) to reduce memory footprint. 3. keytables can be shared between bthreads via bthread_keytable_pool_t. This is critical for RPC which creates one bthread for each request. 4. Hacky support for baidu::rpc::thread_local_data() which is more convenient at manipulating TLS for most users.
r31092 jge  Enable utf8 in url so that url can include utf8 chinese
r31083 jge  Fix failed UT caused last CI
r31083 jge  Fix error in ccover g++
r31078 jge  Always set remote_side before packing
r31075 czy  Not output empty repeated fields
r31021 jge  Fix error in ccover g++
r31016 jge  Use VLOG instead of DEBUG in http_message
r31016 jge  Print more specific log in input_messenger
r31011 jge  /vars /flags are compressed if the response is not less than 512 bytes.
r31011 jge  fix UT
r31011 jge  cpu/heap/growth selects the generated profile automatically, when user refreshes the page, display the same profile rather than generating a new one.
r31011 jge  Add a search box in /vars to speed up locating of bvar
r31011 jge  Add a checkbox in cpu/heap/growth to output textual profile
r31010 jge  Support viewing history profiles and diff with another profile.
r31010 jge  /hotspots uses local pprof directly thus inline functions are supported.
r31010 jge  Add buttons in html mode to enable/disable /rpcz
r31008 jrj  Remove const descriptor of NsheadServer::Process*
r30999 jge  Not expand graphs in /status if the service was never accessed.
r30999 jge  Make blur of shadow to 2px so that it works correctly in firefox.
r30999 jge  Fix a typo in /hotspots
r30992 jge  Write logs when server starts. Changed default generation of version().
r30992 jge  Add navigation bars.
r30987 jge  Renamed request_http_header/response_http_header to http_request/http_response respectively.
r30987 jge  Line width produced by pprof --dot is limited to 0.5 minimum. Previously the connections between nodes are hard to see.
r30987 jge  All js are sent with "Last-Modified" in first visit and laterly chrome/firefox will send requests along with "If-Modified-Since", in which case server always responds with 304.
r30987 jge  All js are compressed by gzip if the browser supports it.
r30987 jge  Added /hotspots, /hotspots/heap, /hotspots/growth to profile and plot results. The server side profiles the process using embedded pprof.pl and send the result in dot format to user's browser which converts dot to svg using viz.js which is the graphviz compiled in js format.
r30983 jge  Fix a race in nshead_pb_service_adaptor
r30981 jge  /status and /vars show vars of nshead_service as well.
r30981 jge  Server owns ServerOptions.nshead_service. Default impl. is NULL now (previously nova_pbrpc)
r30981 jge  Reserved PROTOCOL_HADOOP_RPC/PROTOCOL_HADOOP_SERVER_RPC for DS
r30981 jge  Replaced NsheadServiceAdaptor with NsheadService which supports all builtin services and allocate memory more efficiently.
r30981 jge  Replaced CustomizedStatus with Describable.
r30981 jge  Replaced close_connection_rather_than_respond() with CloseConnection() which can specify a reason and make Controller Failed() as well.
r30981 jge  NsheadPbServiceAdaptor does not allocate its own memory due to new NsheadService. ParseNsheadMeta sets controller rather than return value, pb_res in SerializeResponseToIOBuf is changed to pointer again because it may be NULL.
r30981 jge  latencies at /status page are elapsed time of the whole RPC, not just user's callback.
r30981 jge  Fixed a typo bug in http_rpc_protocol.cpp
r30981 jge  Change all LOG(TRACE)/LOG(FATAL) to LOG(INFO)/LOG(ERROR) in examples. Some of them stop printing every request/response, instead server prints a log to tell user to check builtin services.
r30980 jge  Fix UT
r30980 jge  Fix calling of virtual functions in ctors.
r30980 jge  DumpOptions.wildcard_separator is deprecated. Always separate wildcards using ",;"
r30964 jrj  Add support for non-idl mcpack RPC
r30964 jrj  Add RawBuffer which wraps buffer and capacity
r30962 jrj  New interface of NsheadServiceAdaptor which allows user to call non-protobuf based service (by inheriting ProcessNsheadMessage) Replace former NsheadServiceAdaptor with NsheadPbServiceAdaptor which inherits the former one
r30962 jrj  Add/Modify related examples
r30958 jge  Make qps of cascade_echo_c++ higher (so that curves are more pretty)
r30958 jge  Call latency_percentiles instead of latency_stack
r30955 jge  Rename latency_stack to latency_percentiles
r30955 jge  Add latency_9999 as 99.99% percentile
r30954 jge  Upgrade version of bns to 1.0.32.0
r30954 jge  Support vector plotting
r30954 jge  Show latency_stack/latency_cdf in /status.
r30954 jge  Set expires for /js/xxx
r30954 jge  example/multi_threaded_echo_c++ uses LatencyRecorder.
r30954 jge  Compilation of proto depends on google/protobuf/message.h (to force recompilation when pb upgrades)
r30952 jge  Track bthread_creation delay when -show_bthread_creation_in_vars in on.
r30952 jge  Replace _nidle_workers with _worker_cumulated_cputime and _worker_usage.
r30950 jge  Remove "using Base::xxx" in series.h which is not supported in gcc 3.4
r30949 jge  Sort buckets in place provided the percentile is unchanging.
r30949 jge  SeriesOptions.fixed_length is deprecated. It's always true. Addition checking in Series is only done once.
r30949 jge  Renamed ElementContainer::modify_with_op to modify. load/exchange modifies parameter instead of returning the result to save extra copying (of non-trivial types). Replace check_with_op/modify_global with merge_global for agent combiner on non-atomic types
r30949 jge  Reducer does not require op to implement operator==
r30949 jge  Add latency_stack and latency_cdf in LatencyRecorder.
r30949 jge  Add bvar::Vector which contains multiple data that will be plotted in a same graph.
r30947 jge  Make created directory readable to everyone by default
r30943 czy  Add percetile
r30938 jge  Remove inclusion of strutil.h which is unused and renamed in pb 2.6
r30928 jge  Make ~CallMapper and ~ResponseMerger protected so that they're callable from subclasses.
r30927 jge  For performance issues, Op modifes first parameter instead of returning the result
r30927 jge  Added code to sample DiskStat. However it's not wrapped into bvar yet.
r30924 jge  Clean up files
r30924 jge  Add pretty_json into Pb2JsonOptions
r30920 jge  publish rapidjson in BCLOUD
r30920 jge  fix compilation error
r30915 czy  Handle missing required fileds when parsing protobuf
r30909 czy  Fix the bug that request_code is not passed to sub_cntl
r30909 czy  Copy remote_side from sub_cntl to main_cntl even if this call failed
r30905 jge  Removed error_minute from MethodStatus. ploting makes the item unuseful any more.
r30905 jge  Improved /vars and /status extensively, most items can be ploted on demand.
r30905 jge  /flags, /connections, /vlog uses gridtable as style.
r30905 jge  Added /protobufs to show DebugString of messages/services used.
r30905 jge  Added /js/{sorttable,jquery_min,flot_min} to provide sorttable.js, jquery.min.js, flot.min.js respectively.
r30903 jge  fixes for 3.4
r30901 jge  Reducer/Window/PassiveStatus can save values of last 60 seconds, last 60 minutes, last 24 hours and last 30 days for ploting
r30901 jge  Change separator in wildcards from , to ; which does not break shared links in Baidu HI
r30895 jrj  Remove memset in UBRawXXX::Clear since it's unnecessary and costful
r30893 jge  fixed a bug that Controller::_abstime_us is wrongly set
r30878 jge  Use backup request in schan example
r30878 jge  Make some Describe more pretty
r30876 jge  Use ERPCTIMEDOUT instead of ETIMEDOUT to cancel sub calls in schan and ETIMEDOUT/ERPCTIMEDOUT are both un-retryable.
r30876 jge  make example of schan more complex
r30871 jge  minor fixes
r30870 jge  Fix links in comments
r30870 jge  Add an exmample on schan
r30869 jge  Wrap all parameters to Socket::Create into SocketOptions
r30869 jge  Fixed bugs in PartitionChannel
r30869 jge  /dir and /threads are disabled by default.
r30869 jge  Controller::set_fail_limit does not work anymore.
r30869 jge  Channels can be health-checked. SelectiveChannel disables channels that definitely fail and adds them back when channels pass health-check.
r30869 jge  Added -Wno-unused-result for gcc >= 4.4
r30869 jge  Added Describable for customizing descriptions.
r30865 czy  Fix the bug that id is not added into CorrelationIdList
r30865 czy  Add example (for debugging usage)
r30857 jge  temporary hack to verify a bug
r30846 jge  Split ExcludedServers into separate file and make it thread-safe because it's shared in sub channels in SelectiveChannel.
r30846 jge  request_code is part of Controller instead of part of Controller::Call
r30846 jge  Replace google::protobuf::RpcChannel with ChannelBase as the base of all other channels inside baidu-rpc
r30846 jge  Removed NamingServiceActions::SetFailed(). To end RunNamingService, return non-zero value directly.
r30846 jge  Pass tags (and more fields) from naming services to load balancers.
r30846 jge  Options.connection_type and protocol_type is assignable by both enums and names.
r30846 jge  LoadBalancer::Describe may describe the lb briefly if `brief' is true.
r30846 jge  Generalized SharedLoadBalancer which is a wrapper over LoadBalancer to enable intrusive sharing, creation from name, and weighting etc.
r30846 jge  Fixed assertions on empty servers in consistent_hashing_load_balancer.cpp
r30846 jge  file_naming_service skips comments (beginning with #) and blank lines.
r30846 jge  Extension<T>::List skips private extensions whose names start with underscores.
r30846 jge  Count running servers and a baidu-rpc process with any running server does not read FLAGS_dummy_server_port_file
r30846 jge  CallMapper is intrusively shareable which makes the instance shareable between sub channels.
r30846 jge  Added SelectiveChannel which is a separate layer for load balancing between sub channels.
r30846 jge  Added policy/dynpart_load_balancer.cpp which is a temporary impl. of weighted load balancing. It's only used in DynamicPartitionChannel now as a proof of concept and will be refactored soon.
r30846 jge  Added ParititonChannel which is a specialized ParallelChannel building sub channels by tags associated with each server in the NamingService.
r30846 jge  Added DynamicPartitionChannel which creates sub partition channels for different partitioned servers on-the-fly to enable smooth transisition from one partitioning method to another.
r30846 jge  Added Controller::trace_id and span_id which are non-zero values when rpc call(server-side) is traced.
r30844 jge  make critical section of id_status smaller
r30844 jge  Fix a severe bug that bthread_id_lock_and_reset_range_verbose may be suspended indefinitely. The root cause is that the butex_wait in bthread_id_join waits on the same butex of id locking/unlocking and breaks the signaling chain between contended butex_wait.
r30842 jge  Remove top3 (useless) stack frames from logging
r30836 jrj  Print failed Sockets in /connections and /socket
r30836 jrj  Add snappy support for nova-pbrpc
r30836 jrj  Add gflag defer_close_second
r30830 czy  Not overwrite user defined headers
r30798 czy  Dump vars into different files
r30789 jge  Move setup of controller/span forward in ProcessXXXRequest to provide more information in potential error branches
r30789 jge  add ServerOptions.max_concurrency
r30786 jge  Add namespace and undef
r30784 czy  Fix COMAKE for generating the java protobuf class
r30780 czy  Fix COMAKE and java UT
r30775 czy  Rename java-tmp to java
r30775 czy  Remove the old pbrpc4j stuff
r30770 czy  Bug fixes
r30770 czy  Add URI::query() to get raw query string
r30766 jge  -fix for py
r30764 jge  remove -ldl
r30762 jge  minor changes
r30760 jge  fix BCLOUD
r30759 czy  Improve performance
r30758 jge  remove iobuf stuff
r30756 jge  Fix python linkage with Xlinker
r30756 jge  Depend on public/iobuf
r30754 jge  Split iobuf into separate lib
r30752 jge  Replace with base/iobuf
r30748 jrj  Use OnResponse
r30741 jrj  Fix warnings on GCC4.4
r30740 jge  fix warning in 3.4
r30739 jge  Retrying requests are sent to un-sent servers with best efforts.
r30739 jge  Refactored request-tracking in controller, fixed bugs around backup requests.
r30739 jge  Improved example/multi_threaded_echo_fns_c++ with more realistic latencies
r30739 jge  Fixed batch removal in LALB temporarily. Add comments. Calculate deviation and loosen the punishment on inflight requests
r30739 jge  Add more fields in rpcz and let rpcz display retries correctly.
r30736 jge  fix typo
r30735 jge  Make CHECK() print stacks
r30729 czy  Fix the content-type issue
r30723 jge  Fix COMAKE
r30723 jge  Add utility functions on bthread_id_t
r30718 czy  Add performance test and bug fixes
r30716 czy  Fix bugs and compile errors
r30712 czy  Add serveral implementation
r30702 jrj  Switch to trunk in local_build.sh
r30693 jge  Removed g_socket_buffer_size in input_messenger.cpp
r30693 jge  Print more fields in /sockets
r30693 jge  Fixed the incorrect protocol name in /connections, make state of SSL more pretty
r30693 jge  Fixed a bug that Socket does not clear _read_buf after HC.
r30691 jge  Sub controllers are accessible via Controller::sub(), #sub-channels is Controller::sub_count()
r30691 jge  Not setenv TCMALLOC_SAMPLE_PARAMETER in global, seems not working
r30671 jge  Split builtin services and move them into baidu/rpc/builtin, renamed builtin_service_helper.h to builtin/common.h
r30671 jge  Replace policy/register_all_extensions.cpp and global_update.cpp with global.cpp
r30671 jge  Rename PROTOCOL_BAIDU_RPC to PROTOCOL_BAIDU_STD, PROTOCOL_MEMCACHE_BINARY to PROTOCOL_MEMCACHE
r30671 jge  Renamed ChannelContext to LoadBalancerWithNaming
r30671 jge  Link socketid to /sockets/<socketid> in /connections, removed column BufferSize
r30671 jge  Fixed a bug in socket.cpp that declartion of g_messenger is wrong.
r30671 jge  Add StringToProtocolType and ProtocolTypeToString
r30668 jge  Move bit_cast and ignore_result into namespace base
r30666 czy  Fill Host field with remote side if accessing Http server through bns
r30664 czy  Fix a bug in chash
r30646 jge  Point to bthread at new namespace
r30646 jge  Change the way checking gcc version
r30641 czy  Not call watchers if server list doesn't change
r30640 jge  Fix warnings under gcc 4.4
r30640 jge  Fix -dumpversion issue in debian/ubuntu
r30631 czy  Keep Content-Length in http header map
r30622 jge  Move into namespace bthread
r30620 jrj  Move Controller::set_fail_limit to ParallelChannel::set_fail_limit
r30613 jrj  Remove undetermined assertion
r30612 jge  Remove "latency" suffix for LatencyRecorder
r30612 jge  Fixed a minor bug in to_underscored_name in variable.cpp
r30607 jge  Send log to comlog as default
r30601 jge  Remove some unuseful default variables
r30599 jge  Add ctor of LatencyRecorder of two prefixes
r30597 jge  Add -bvar_dump as the switch for all dumping features
r30595 jrj  Fix race condition on set_preferred_index
r30594 jrj  Set preferred index into socket if it doesn't have one or it's non single connection
r30594 jrj  Disallow single connection for public/pbrpc
r30589 jrj  Do NOT return -1 if BNS return empty server list
r30583 jge  replace get_result with get_value
r30581 jge  add HtmlReplace
r30577 jge  restore fill char at logging
r30576 jge  Window requires window_size now.
r30576 jge  Remove deprecated methods which is not required by baidu-rpc@ci-base
r30576 jge  Add GFlag to expose important gflags as bvar (gflags can be selectively(by wildcards) exported in dumping_thread, so the usefulness of this utility is still in doubt)
r30574 jge  Used base::DoublyBufferedData instead
r30574 jge  Changes according to latest bvar
r30572 jge  Remove most *_minute variables, re-implement *_second variables with PerSecond, the window_size uses bvar_dump_interval as default.
r30572 jge  Removed some overload of expose.
r30572 jge  quoting of strings are implemented differently so that describe_exposed() can be unquoted
r30572 jge  names of variables cannot conflict now.
r30572 jge  bvar_dump_file is set to "bvar.<app>.data" by default. As a result, the dumping thread will be on in applications linked bvar.
r30572 jge  Add PerSecond which divides duration time comparing to Window
r30572 jge  Add modify_with_op to ElementContainer to avoid a race between reset() and << on non atomical types
r30570 jge  support tellp in wrapped streambuf
r30568 czy  Fix compile errors in GCC3.4.5
r30567 czy  Print warning log if recording lantecy is negative and it is to be ingored
r30567 czy  `max_latency' is by default 0 if there's no recorded latency
r30563 czy  Add flag bvar_log_dumpped to log dumpped infomation
r30552 jge  normalize bvar_dump_prefix and make it reloadable
r30552 jge  Make to_underscored_name stricter
r30552 jge  Make strings quoted which is required by noah.
r30550 jrj  Add succeed_without_server in ChannelOptions to allow Channel::Init to succeed when NS return no available server
r30548 jrj  Add -Wno-unused-parameter
r30544 jrj  Update has_builtin_service to security_mode in python
r30543 jrj  Replace has_builtin_service with security_mode in ServerOptions If security_mode is true, 1. Builtin services are turned off 2. Server ip/port inside error-text will be hashed with MD5 3. URI will be escaped if method can't be found
r30540 jge  register stacks to valgrind when -has_valgrind is on.
r30539 czy  Link base after Xlinker
r30533 jge  fix warning in centos 6.3
r30532 jge  Remove /process and /memory, Display /dir in /index
r30532 jge  Polish /connections
r30528 jge  Add bvar_dump_prefix and mkdir for bvar_dump_file
r30527 czy  Add policy protos
r30525 jge  Use BasicPassiveStatus<std::string> instead of PassiveStatus to avoid break with new bvar
r30524 jge  Use base::Lock to replace mutex.
r30524 jge  status/reducer/combiner accept non-primitive types.
r30524 jge  PassiveStatus can be windowed
r30524 jge  add misc process-level variables as default
r30523 czy  Add SocketMap
r30523 czy  Add InputMessenger
r30521 jge  Remove debugging version of lock which is errorous as a standalone lib
r30521 jge  Add is_pod
r30503 jia  STORY=baidulib-900
r30503 jia  Add optimized_reader function to improve json_to_pb efficiency.
r30500 jge  Changes according to bvar::Window and bvar::LatencyRecorder
r30497 jge  Add Window<> to aggregate samples within a time window.
r30497 jge  Add LatencyRecorder, a specialized class including multiple bvar inside to record latencies.
r30495 jge  Add missing bounded_queue.h
r30492 jge  Minor fix
r30492 jge  add functions to append circular LinkNode
r30491 jrj  Implement Controller method: IsCanceled and NotifyOnCancel, where `Cancel' means the underlying connection was broken
r30491 jrj  Add related UT
r30488 jrj  Fix a race condition on FlatMap::init
r30486 czy  Fix the bug that WriteRequestList is not correctly udpated
r30482 jrj  Synchronize BCLOUD & remove Makefile
r30445 jge  Fix an inconsistent check
r30444 czy  Remove redundancies
r30443 czy  Remove redundancies
r30443 czy  Add Socket
r30443 czy  Add EventDispacher
r30443 czy  Add CorrelationIdList
r30439 jge  Use dumper in vars_service
r30439 jge  Fix a bug in CutInputMessage
r30438 jge  Disable a check on is_enum in gcc 3.x
r30437 jge  make SetLogSink thread-safe
r30437 jge  Make Lock working as part of a static library (previously the impl assumes that all code are compiled together which is not true in baidu)
r30437 jge  Add DoublyBufferedData
r30427 jge  Fix busy loop when filename is empty in dumping_thread
r30427 jge  Fix 3.4 warnings
r30426 jge  Merge description on confliction
r30426 jge  Add Variable::dump_exposed
r30426 jge  Add FLAGS_bvar_dump_file/include/exclude/interval
r30421 jrj  Remove -lcrypto and -lssl
r30416 jrj  Update BCLOUD
r30414 jia  Update writer string function with optimized_writer.h
r30414 jia  STORY=baidulib-900
r30411 jrj  Update BCLOUD
r30409 jrj  Publish baidu/bthread/offset_inl.list
r30403 jge  Use boost::any instead of GeneralValue
r30403 jge  Polish code
r30398 jrj  Update BCLOUD
r30396 jrj  Add openssl@1.0.1.1000 in both COMAKE & BCLOUD
r30393 jge  Rename some methods in rapidjson to avoid conflict
r30384 czy  Temporarily fix the linking issue of the Python module
r30383 jge  Add more cases on type_traits
r30383 jge  Add add_cr_non_integral
r30382 jge  Point to new bvar
r30380 jge  Point to new bvar
r30378 jge  Unify name of UT
r30378 jge  Removed variable_info stuff (empty)
r30378 jge  Move bvar into namespace bvar, baidu/bvar.h is still include-able
r30376 jge  Replace base/template_util.h with base/type_traits.h, adding missing templates
r30376 jge  Fix incorrect program name in ComlogSink::Setup
r30376 jge  Add base/memory/scoped_array.h (from toft)
r30370 jrj  Use third-64/openssl
r30368 jrj  Fix Makefile
r30366 jrj  Use third-64/openssl
r30361 jrj  Trigger agile
r30359 jrj  Add SSL support at server side
r30355 jrj  Add SSL operations in IOBuf
r30354 jge  Point to base/files/temp_file.h
r30352 jge  Fix baidu/temp_file.h
r30351 jge  Fix BCLOUD
r30350 jge  Move temp_file.cpp into base/files/
r30349 jrj  Test SCM
r30344 jrj  Use base::StringPiece
r30342 jrj  Fix nshead example
r30341 jrj  Usage changes on UBRawBufferRequest to: 1. Pass in a piece of buffer and its capacity 2. Fill in request data into it using UBRawBufferRequest::buf() 3. Set the request size by UBRawBufferRequest::set_size Also it can be copied, cleared
r30341 jrj  Minor fixes of renaming
r30341 jrj  `magic_num' will be automatically filled so that user don't have to set nshead struct when using nshead+idl type
r30341 jrj  Add example for raw buffer usage
r30340 jrj  Change nshead related protocol name
r30339 jge  Remove base/streaming_log.h
r30337 jge  Remove UNLIKELY which is conflict with Kylin
r30335 jge  Replace std::cout with LOG(INFO)
r30335 jge  hash EndPoint
r30331 jge  Fix dirname issues
r30330 jge  Fix dirname issues
r30329 jge  Suppress warning in gcc 3.4
r30328 jge  Use more strict macros
r30326 jge  Add meaningful BCLOUD
r30324 jge  Publish more headers
r30322 jge  Seperate ComlogSink into base/comlog_sink.cc
r30322 jge  Removed C support of berror()
r30322 jge  Publish headers.
r30317 jrj  The service and method name of UBRPC now should be generated by google::protobuf (by importing ub.proto), and then you can use exactly the same API to issue an UBRPC
r30317 jrj  Switch to use public/common/base
r30317 jrj  Relevant changes in UT/example 
r30317 jrj  Refine the user interface for wrapping IDL structs. Now we pass the IDL struct as template parameter and the rest API is almost exactly the same as IDL struct. For example: // Use bsl::mempool to construct request just like UBCompackRequest<idl::MyRequest> req(&pool);   req.set_message("hello");   // Set fields of idl::MyRequest
r30315 jge  Add missing test_iobuf stuff
r30314 jge  Not link iobuf in python
r30312 jge  Fix for cov gcc
r30311 jge  rpc changes according to new pubic/common
r30308 jge  Include base/logging.h in baidu/bthread/log.h
r30307 jge  Use LOG() instead of BT_XXX/printf
r30307 jge  Include headers in base/ rather than baidu/
r30306 jge  Use LOG() instead of cout/printf
r30306 jge  Include headers under base/ rather than baidu/
r30301 jge  Not define malloc functions
r30295 jge  Remove conflicting typedefs in base/basictypes.h
r30290 jge  Fix baidu/public_common.h
r30288 jge  Remove Werror and gcc related warning and add makefile back temporarily
r30282 jge  Remove Makefile from public/common
r30277 jge  Sync BCLOUD with COMAKE
r30276 jge  Change for cov change
r30275 jge  Fix local_build.sh
r30274 jge  Depend on gtest 1.7 (which is actually gmock)
r30258 jge  Make ut in gcc 3.4 pass
r30248 jge  fixes for gcc 3.4
r30241 jge  Fix COMAKE
r30240 czy  Fix UT
r30239 jge  Import Chromium/base before C++11 and merge existing code into it.
r30238 jia  STORY=baidulib-900
r30237 jge  Remove BaiduStreamingLogHandler temporarily to make new public/common not break baidu-rpc
r30236 czy  Upgrade java errno
r30235 czy  Fix the undefined behavior of http_parser when there's no host field
r30225 jrj  Remove Libraries inside boost
r30203 czy  Not overwrite non-null sighandler of SIG_PIPE
r30195 czy  Fix compile errors within gcc4.8
r30193 jge  Remove libbaidu_public_common.a on make clean to make sure previous lib is not linked
r30192 jge  Move files into namespace base.
r30190 jge  fix warning/test/style
r30189 jge  fix warning
r30184 jrj  Replace gethostbyname with gethostbyname_r
r30184 jrj  Minor fixes
r30183 jrj  Add gflag for protobuf2comlog
r30179 jge  Add dummy_server
r30176 jrj  Add related UT
r30176 jrj  Add DomainNamingService which can load multiple addresses from a single domain name. It's protocol prefix is "http://" and `Channel' should use the second way to `Init'. Now it's implemented by `gethostbyname_r' 
r30176 jrj  Add Controller::method()
r30169 jge  Add init_from_not_exist
r30168 czy  Remove redundant files
r30167 czy  Update CorrelationId
r30167 czy  Minor fix
r30167 czy  Add IOBuf
r30154 jge  Unify all authors to "The baidu-rpc authors".
r30154 jge  Add OWNER file
r30150 jrj  Remove UbChannel and UbController. Instead, define UBRequest/UBResponse in proto file so that they are both google::protobuf::Message, which enable us to send ub request using protobuf framework
r30150 jrj  Relative changes on UT and examples
r30150 jrj  Pack dummy `id' field into UBRPC_XXX's meta so that we can generate the whole request IOBuf inside `SerializeRequest' phase
r30150 jrj  Create 5 types of ub call which inherit UBRequest/UBResponse
r30113 jia  STORY=baidulib-900
r30072 czy  Use relative url in /index and /rpcz
r30054 jge  Print missing required fields in ErrorText
r30049 jge  Fix compilation errors and upgrade some deps to make this module compilable with gcc4.8
r30047 jia  STORY=baidulib-900
r30045 jge  Use older gtest which is compiled by gcc 3.4 so that jenkins can pass
r30042 jge  Use find_cstr and find_lowered_cstr extensively. Removed HttpHeader::GetHeaderInLowercase()
r30042 jge  Renamed "ch_mh" and "ch_md5" to "c_murmurhash" and "c_md5" respectively.
r30042 jge  Remove restrictions on name length to Extension<>.
r30039 jge  Add find_lowered_cstr
r30037 jrj  Fix compiler warnings under gcc4.8.2
r30031 jge  Add find_cstr to avoid memory allocation when querying a map with std::string as key.
r30009 czy  Add murmurhash to library
r30008 czy  Fix a compile error
r30007 czy  Fix compile errors within gcc3.4.5
r30007 czy  Add -Werror
r30006 czy  Random changes
r30006 czy  Add ComsistentLoadBalancer
r30005 jrj  Automatically allocate memory for paring/serializing idl objects instead of allocating inside UbController. Add a gflag to decide whether to use TLS buffer Remove buffer size options inside UbChannelOptions
r30001 jrj  Change default value of idle_timeout_second=0, max_connection_pool_size=100
r30001 jrj  Add a new gflag to decide whether to use service::fullname when packing PROTOCOL_BAIDU_RPC requests
r29970 jge  Not use boost::lockfree::stack in resource/object pool to reduce initial memory footprint
r29965 jge  Show info of pools of id/socket
r29963 jge  Show total_size in XXXPoolInfo and add id_pool_status()
r29963 jge  Not delete ResourcePool and ObjectPool at exit
r29963 jge  Make XXXPoolBlockMaxSize and XXXPoolBlockMaxItem smaller.
r29963 jge  Fix timing in test_cond.cpp
r29955 jge  Fix comment
r29954 jge  Add const ref to JsonToProtoMessage
r29953 jge  Make logs more readable and remove unnecessary deps from COMAKE
r29952 jge  Minor fixes
r29946 jia  STORY=baidulib-900
r29945 jia  STORY=baidulib-900
r29943 jrj  Trigger Agile
r29940 jrj  On reading EOF in InputMessenger, trying to call CutInputMessage for the last time. Under some protocol such as HTTP, this will generate a new InputMessageBase. Add a new parameter `bool read_eof' in Protocol::Parse callback
r29940 jrj  Change giano's version to 1.1.9 which should fix an compiling error under gcc4.4
r29940 jrj  Add UT for SocketMap*
r29940 jrj  Add close-idle thread in socket_map.cpp, which will close idle pooled connection and orphan connection when FLAGS_idle_timeout_second > 0
r29939 jge  Revert changes on RetryingError() and fix warnings
r29938 jge  Support memcache binary protocol.
r29938 jge  Added hton64/ntoh64 in raw_pack.h
r29930 jge  Replace the incorrect && with || in bthread_id_create
r29922 jge  Slice can be implicitly created from strings
r29922 jge  Remove unnecessary methods of Status and fix ambiguity
r29921 jge  strings can be assigned to IOBuf directly.
r29921 jge  Renamed cut(..., delim) to cut_until
r29921 jge  Fix the incorrect usage of size() in append_to
r29921 jge  cutn works with strings now.
r29907 czy  Allow emtpy value in query string
r29902 jrj  Fix a compiling error in example/nshead_extension_c++
r29902 jrj  Do NOT close listen fd when accept failed inside Acceptor
r29901 jge  Add timer in tests
r29897 bcl  STORY=scm_t-29
r29897 bcl  add BCLOUD file for public/protobuf-json
r29895 jrj  Clear pooled connections in SocketMapRemove
r29895 jrj  Add UT
r29891 jge  Add /sockets
r29885 czy  Change the version of gtest
r29884 jge  Use my_ip() and my_ip_cstr()
r29884 jge  Make quadratic_latency default false, tiny adjust to LALB
r29884 jge  Add FLAGS_spin to mt_echo_fns_c++
r29881 jge  Add my_ip(), my_ip_cstr(), my_hostname(), removed my_ip(variable)
r29880 czy  Change of version of gtest
r29877 czy  Touch local_build.sh to generate a new revision to release
r29875 czy  Fix compile warnings with GCC4.8.2
r29873 czy  Use bthread_id_lock_and_reset_range to fix the bug caused by that max_retry is undefined in Controller::call_id
r29871 czy  Add bthread_id_lock_and_reset_range
r29859 jrj  Change third-64/gtest from base to 1.6.0.100
r29858 jge  Rename copy() to copy_to(), add append_to()
r29846 jrj  Use RunDoneByState in CallMethod instead
r29844 czy  Add IdsService
r29842 czy  Remove test_id.cpp
r29841 czy  Add id_status
r29840 jrj  Always add newly-accepted Socket into _socket_map in Acceptor. Otherwise, Acceptor::BeforeRecycle could be called after Acceptor has been destroyed
r29840 jrj  Add UTs for Server
r29840 jrj  Add RunDoneByState which may run done in another thread according to `Controller::_run_done_state'
r29837 jge  Changed signature of URI.method_path()
r29837 jge  Add /bthreads service to show information of given bthread
r29834 jge  Add print_task
r29833 jge  Return EINVAL when bthread_id is invalid in bthread_id_unlock[_and_destroy]
r29826 jrj  Fix accessibility problem of `_run_done_state' in SubDone
r29823 jge  done is always called in another thread
r29823 jge  changed max_body_size to uint64
r29817 jrj  Fix COMAKE
r29815 jrj  Add UT for HTTP protocol
r29815 jrj  Add gflag `max_body_size' in input_messenger.cpp. Now each protocol will generate a `PARSE_ERROR_TOO_BIG_DATA' error if the body size indicated by its header is larger than `max_body_size'
r29814 czy  Add CorrelationId
r29795 jge  When a server is added into LALB, it takes (inaccurate) average weight as the initial weight.
r29795 jge  Set weight of failed socket to be (inaccurate) average weight to avoid the situation that most selections failed at an un-addressable server with a large weight. Not set the weight to be 0 or very small otherwise even if the server is recovered, it rarely gets a chance to be selected.
r29795 jge  Renamed -quadratic to -quadratic_latency and set it to true as default.
r29795 jge  Changed signature of LoadBalancer::SelectServer to set need_feedback per call. Feedback() will be called iff the variable is set to true.
r29795 jge  Add Describe() for rr and random LB.
r29793 jge  Fix typo
r29785 jge  Generalize protocol-specific code in channel.cpp as parse_server_address and supported_connection_type in Protocol. Channel does not treat http specially.
r29785 jge  Fix examples and deletion of request_http_header/response_http_header (leaking in last revision)
r29781 jge  Send ErrorText as http body rather than http header
r29781 jge  random changes on logging
r29779 jge  Convert key/value in /status into vars
r29778 jge  request_http_header and response_http_header are allocated on demand.
r29778 jge  Refactored Socket::XXXDepositedHttpMessage functions.
r29778 jge  HttpInputMessage inherits HttpMessage now.
r29774 jrj  Add UT for policy/*protocol.cpp
r29773 jrj  Add UbSubCall UbSubCall::Skip() which can be used to skip a UbChannel in UbCallMapper::Map
r29766 jge  Fix typo again
r29765 jge  Fix typo
r29764 jge  Remove a debug log in channel_context.cpp
r29764 jge  CallMapper can return Skip() now which makes the sub channel being skipped. If all channels are skipped, the call is ECANCELED.
r29762 jge  Add DEFINE_SMALL_ARRAY
r29732 jge  Separate DoublyBufferedData<>::ModifyWithForeground from Modify.
r29732 jge  RPC calls Feedback() of LB.
r29732 jge  LALB works now, however it may need further tweakings on calculation of inflight-delay and adding/removal of servers.
r29732 jge  Improved example/multi_threaded_echo_fns_c++
r29732 jge  ChannelContext describes LB in vars if needed.
r29732 jge  BatchAdd of LB return number of servers added.
r29731 czy  Remove redundent files
r29730 czy  Refine directory structure
r29729 jrj  Add UbParallelChannel
r29727 jrj  Add friend member in Controller
r29711 czy  Show the total buffer size of very sockets in /vars
r29711 czy  Show the buffer size of each socket in /connection
r29694 czy  Not show compile warnings when using GCC3.4.5
r29694 czy  Add some status and vars
r29684 czy  Use the real world giano authenticator in examples
r29684 czy  auth is disable by default in example clients
r29680 jge  Minor change to BAIDU_LIKELY
r29672 jge  Minor fix
r29671 jge  Replace EBADFD with EFAILEDSOCKET to make error text more readable
r29668 jge  Added example/parallel_echo_c++
r29663 jge  Fill /java-tmp with cleaned-up pbrpc4j which will be largely refactored further. It's NOT intended to be used by any user.
r29662 czy  ZeroCopyOutputStream can have its own block
r29662 czy  Remove copied boost files
r29640 jge  Add prototype of LocalityAwareLB
r29639 czy  Fix a bug about creating PyAuthenticator in ServerStart()
r29635 czy  Minor fix
r29635 czy  Fix bugs in authenticator
r29633 jge  Suppressed several strict-aliasing warnings.
r29633 jge  RR uses TLS offsets instead of shared offset (which is relatively contended and slow)
r29633 jge  Refactored LoadBalancer to make double buffering implementation-specific. Interfaces of LoadBalancer are largely changed.
r29633 jge  Generialize LoadBalancerControl to DoublyBufferedData<> which makes Read() almost lock-free by making Modify() much slower.
r29633 jge  Fixed uneffective AddOptFile in test/COMAKE
r29632 jge  Add timer only when it's needed after backup request occurs
r29631 jrj  Add local_build.sh and UT
r29616 czy  -Add flags in socket.cpp to set the send/recv buffer size of the tcp socket
r29611 jrj  Seriailize requests of UBRPC_* inside `PackUbRpcRequest' instead of `SerializeUbRpcRequest' since they contain correlation_id (which changes during retry) 
r29611 jrj  Rename examples and add target to build *.idl files
r29611 jrj  More UT of UbChannel
r29607 jge  Replace ESUBCANCELED with EPCHANFINISH
r29600 jge  Not catch exceptions in bthread
r29595 czy  Support authenticator
r29595 czy  Minor fix
r29595 czy  Add a option that server may own auth
r29567 jge  Refine again...
r29566 jge  Refine comments on ChannelOptions and ServerOptions
r29565 jge  Refine comments on protocol and fix typos
r29559 jge  Add several VLOG
r29558 jge  Limit _fail_limit to be inside [1, nchan]
r29555 jge  Move impl of NamingServiceWatcher from LoadBalancerControl into ChannelContext.
r29555 jge  Make Extension<> singleton explicitly instead of using macros
r29555 jge  Correct RoundRobinLB with shared offset between clones
r29541 jge  Move init of lb into ChannelContext
r29540 jge  Separate timeout of RPC from timeout of connect by ERPCTIMEDOUT, which will be translated to ETIMEDOUT before RPC finishes. Retry for ETIMEDOUT
r29540 jge  Replace max_fail with fail_limit which is easier to calculate. Unify error code when all non-ESUBCANCELED error code are same.
r29540 jge  Renamed JoinResponse to Join
r29540 jge  Init/Destroy of Socket::_id_wait_list are moved into Create/OnRecycle to reduce RSS.
r29540 jge  Controller called with call_id() but CallMethod() cancels the cid in dtor.
r29539 jrj  Add UT for rpcz
r29539 jrj  Add missing <deque> in span.h
r29537 jge  Preliminary support of rpcz for http
r29537 jge  Optimize starting of rpcz so that it can be turned on according to a request with trace_id
r29537 jge  Merged span_search.cpp into span.cpp
r29536 jge  More reasonable bthread_setconcurrency when tc is not created
r29516 czy  Add missing file
r29510 czy  Add more unit tests, and fix a bug in _GetDependencies
r29501 czy  Fix a bug that PWD is not changed to root directory when the unit test of Python fails
r29496 jge  Add a case on empty ParallelChannel
r29495 czy  Make and test the Python module in local_build.sh
r29492 jge  Support backup request (not including server cancelation)
r29486 jrj  Relevant changes to OnVersionedRPCReturned in baidu-rpc
r29483 jge  Not setfailed when status code is 2xx
r29474 jge  Replace two bool in SubCall with a int flag.
r29474 jge  Ignore baidu:: in test_channel.cpp
r29474 jge  Destroy call_id after calling done
r29473 czy  Not import dependent module, otherwise the global expression will be execute twice or more
r29473 czy  Minor fix
r29473 czy  Add echo_python to example
r29468 jrj  Fix a bug that rejected response/failure still `SetFailed' Controller and thus contaminate _error_code. Now `OnVersionedRPCReturned' has a third parameter: saved_error, to which _error_code will be reverted once check fails
r29468 jrj  Fix a bug in Acceptor that events of acception socket still arrives when `Join' returns. Apply state machine to Acceptor and `Join' will wait until acception socket has been recycled (and socket map is empty as before)
r29466 jge  User can call Controller.close_connection_rather_than_respond() tell the server to close connection rather than sending back response after running user's callback.
r29466 jge  ParallelChannel combines multiple channels into one. It behaves similarly as a single channel, but in the background, it accesses all channels simultaneously with optionally modified requests (by CallMapper) and merges responses (by ResponseMerger) when they come back.
r29466 jge  ErrorText at server side starts with [ip:port].
r29466 jge  Building test_channel.cpp depends on test/echo.proto correctly.
r29458 jge  Not new request in example/asynchronous_echo_c++
r29458 jge  HttpHeader is copyable.
r29457 jge  Remove fd from epoll just before close() so that EPOLL_CTL_DEL are always after EPOLL_CLT_ADD
r29453 jge  Add a unversioned slot for correlation_id
r29439 jge  Add iterators of headers
r29433 jrj  Fix a bug of missing `SetLogOff' when connection type isn't single
r29433 jrj  Do not parse json http body when request prototype has no field
r29430 jge  Modify signatures of _process, replace trylock, add versioned check
r29420 jge  Removed Controller::SetFailed(int error_code) whose text is too vague
r29418 jge  Use bthread_id_lock instead of bthread_id_trylock in protocols so that we don't have the chance to miss responses. As a result, we unlock the id after Socket::Write is done.
r29418 jge  socket_id() and arg() are got from InputMessageBase rather than parameters. We may let _process own the message in next revisions so that CheckEOF can be merged into InputMessageBase::Destroy()
r29418 jge  Re-arranged methods/fields of Controller.
r29418 jge  Better warnings on wrongly reusing Controllers
r29418 jge  Align memory of butex by int
r29415 jge  Add BCLOUD
r29414 jge  Use BAIDU_REGISTER_ERRNO instead of DEFINE_BAIDU_ERRNO
r29413 jge  Tidy comments on bthread_id in bthread.h
r29413 jge  Align Butex in safer way.
r29413 jge  Add bthread_id_lock() which may block until the id being unlocked.
r29366 jge  Fix user agent in test_builtin_service.cpp
r29365 jge  Use text when user-agent is absent
r29361 jge  Update comment in errno.h
r29360 jge  Rename DEFINE_BAIDU_ERRNO to BAIDU_REGISTER_ERRNO
r29350 jrj  Unlock bthread_id inside Controller::OnVersionedRPCReturned
r29350 jrj  Fix UT
r29313 jrj  Fix some TODO in protocols that parsing error of meta information will close the connection at server side
r29313 jrj  `_cur_retry_count' so that early error event will be dropped correctly
r29313 jrj  Add UT for builtin-services
r29313 jrj  Add OnVersionedRPCReturned which check parameter `id' against `_correlation_id'
r29313 jrj  Add MAX_RETRY_COUNT which is the upper bound for Controller::_max_retry, which will be used as the `range' parameter to `bthread_id_create_ranged'.
r29285 jge  Replace node name in test_baidu_naming_service.cpp because the name is invalid.
r29284 jge  Added bthread_id_create_ranged() which allocates a range of ids mapped to a same entity so that users can encode versions into ids.
r29207 jge  Show nshead_service_adaptor on /status
r29207 jge  Change wiki links
r29206 jrj  Change include path of nshead.h
r29203 jrj  Move src/baidu/rpc/policy/nshead.h to src/baidu/rpc/nshead.h
r29203 jrj  Add support for public/pbrpc protocol, both client and server side
r29203 jrj  Add example for public/pbrpc
r29203 jrj  Add const descriptor to `ServerPrivateAccessor::_server'
r29203 jrj  Add CompressTypeToCStr
r29201 jge  Check method_index against method_count before calling method()
r29190 czy  Extend baidu-rpc C++ module as a module of python
r29190 czy  Add server and controller in server-side
r29187 jrj  AddClientSideHandler for PROTOCOL_UBRPC
r29185 jrj  Separate favicon, flags, vars, connections, rpcz, dir into individual files (proto, pb and cpp)
r29185 jrj  Rules of parsing URI changed: First, try to find method by [service_name]/[method_name]. If not found, try to invoke the `default_method' of [service_name] and set the rest of URI into `HttpHeader::method_path'. Also, `/' is always regarded as path separator instead of namespace separator of full service name
r29185 jrj  Replace InputMessenger::Init with InputMessenger::AddHandler, which is thread-safe now
r29185 jrj  Decompose methods inside BuiltinServiceImpl into separate google service classes
r29185 jrj  Allow conflicts of user's ServiceDescriptor::name (not full_name) inside Server. It generates a warning and set the ambiguous flag. These methods MUST be called using full_name
r29185 jrj  Allocate global stuffs of `socket_map.cpp' on heap in order to avoid destruction when exits. Otherwise, it may conflicts with global `Channel'
r29185 jrj  Add StringToConnectionType which converts a string configuration into `ConnectionType'. Use this inside all examples
r29185 jrj  Add FLAGS_log_connected to switch log when a new connection has been created
r29185 jrj  Add echo.proto in test directory and some other fixes in UT
r29185 jrj  Add buitlin-service `badmethod' which contains two methods: `ambiguous' is used to warn the caller to invoke the method through full service name to resolve ambiguity; `no_method' will show a page of available methods of the given [service_name] when [method_name] is missing
r29154 jrj  baidu-rpc-ub changes according to bthread_id and serialize/pack separation
r29146 jge  Remove oneshot stuff and modify comments
r29143 jge  Separate serialization of requests into IOBuf from packing, the method is called once for each RPC call disregarding retries. pb protocols use SerializeRequestDefault defined in protocol.cpp as default. As a result, controller does not store pointer of request anymore, and users can immediately modify or delete the request after Channel::CallMethod even if it's asynchronous.
r29143 jge  Re-implement bthread_oneshot related code with bthread_id which allows more-than-once, more sophisticated access.
r29143 jge  Fixed a bug in acceptor that in_fd may be leaked.
r29143 jge  Controller::call_id() can be called before CallMethod and the id is cancelable before/during/after RPC call: [before] CallMethod will finish immediately with Controller.ErrorCode=ECANCELED; [during] RPC call will end soon with Controller.ErrorCode=ECANCELED; [after] nothing happens.
r29140 jge  Replace bthread_oneshot with bthread_id which differs from the former one that it can be used for more than once, it's designed to address various issues in baidu-rpc that can't be easily fixed with bthread_oneshot. bthread_oneshot functions are scheduled to be removed in next CI.
r29086 jge  Append to ErrorText instead of Replacing. ErrorText may look like this: "Fail to do sth; [R1] Fail to do another thing; ..." where "Rn" means n-th retry.
r29076 czy  Copy headers to output/include
r29074 jge  Separate /process from /status
r29074 jge  Clean up per-method stats
r29074 jge  Add /memory which shows stats of malloc (including tcmalloc)
r29071 jrj  Replace ServiceStatus with MethodStatus, thus add MethodMap in Server which use MethodDescriptor::full_name as key
r29071 jrj  NsheadServiceAdaptor::SerializeResponseToIOBuf now returns ResponseAction to specify whether to close the connection or send back response
r29071 jrj  Minor change in ServerPrivateAccessor
r29049 jge  Update comments in closure_guard.h
r29046 jge  Change dep on baidu-rpc back to ImportConfigsFrom in most examples so that users can comake2 -UB inside example directly.
r28989 jrj  Relevant changes to nshead protocol in baidu-rpc
r28988 jrj  Create global extension objects on heap
r28988 jrj  Add NsheadServiceAdaptor which adapt nshead protocols to use protobuf interface at server side. Add NovaServiceAdaptor which implements this. Add NsheadMeta  which is the intermedia data structure inside the adaptor
r28988 jrj  Add example for user-defined NsheadServiceAdaptor
r28982 jge  Make log on catching exception more clear.
r28970 jge  Remove *_inl.h from baidu/public_common.h
r28970 jge  Add baidu/macros.h
r28968 czy  Modify BCLOUD
r28966 czy  Modify BCLOUD
r28964 jge  Add RETURN_IF to simplify code on returning
r28958 czy  Add more status per service
r28957 czy  Override std::ostream::operator<<
r28952 czy  Remove authorization
r28933 jrj  Fix a bug that socket can't revive on receiving ELOGOFF under connection pool
r28926 jrj  Check EOF inside UBRPC protocol
r28921 jrj  Retry for EPIPE and ELOGOFF
r28921 jrj  Remove `_ninprocess' in InputMessenger
r28921 jrj  Remove all *.pb.h files when calculating coverage
r28921 jrj  Now Acceptor::StopAccept just calls ReleaseAdditionalReference on every socket. All sockets will destroy themselves after we've finished all current requests. 
r28921 jrj  Minor fixes inside the UT
r28921 jrj  EOF event now will be delayed (by calling PostponeEOF after parsing) until all the current response have been processed (by calling CheckEOF after  bthread_oneshot_fight inside all protocols)
r28921 jrj  Additional reference to `Socket' will be hold until done->Run has been called at server side. Also Acceptor::Join will wait until socket map becomes empty. As a result, the `timeout_ms' in Server::Stop/Acceptor::StopAccept becomes useless
r28914 jge  Add bthread_oneshot_cancel to recycle an unused oneshot object
r28907 jge  Move Socket::_write_head to be last to avoid cache-bouncing.
r28907 jge  add a case of global channel
r28905 jge  Not stop bthreads atexit
r28872 jge  Status/PassiveStatus changes according to latest bvar
r28870 jge  bthread changes according to new bvar::BasicStatus<>
r28869 jrj  Remove *.ph.h files when generating CCover report
r28869 jrj  Minor fixes reported by Coverty including checking return value, uninitialized fields
r28860 jge  Add Variable::get_value and Variable::get_exposed to get typed value.
r28860 jge  Add BasicStatus and BasicPassiveStatus to contain generic types
r28859 jrj  Reset fd before add it into epoll in Socket::ResetFileDescriptor
r28859 jrj  Remove servers in NamingServiceThread::StopAndJoin instead of Run
r28856 jge  Update test/COMAKE
r28854 czy  Minor fix for gcc3.4.5
r28853 czy  Add rapidjson.h and config.h to avoid the unused-local-typedefs warnings
r28851 czy  Add back Makefile for gcc3.4.5
r28849 jge  Remove Makefile
r28847 jge  Change %g to %.10g because glibc may hehave differently
r28844 jge  Merge src/COMAKE into COMAKE
r28842 jge  Add description_type() to Variable
r28840 jge  Indent content of /index to make it more readable
r28840 jge  Added /vlog which lists all VLOG callsites
r28838 jge  Cleanup UT and COMAKE
r28837 jge  Add print_vlog_sites
r28836 czy  pid_file can be with environment variables and uncrated direcotires
r28835 jge  Renamed span_search.proto to span.proto, SpanProto to RpczProto, SpanBrief to BriefSpan, Added TracingSpan for later usage.
r28833 jge  Replaced bthread_default_concurrency with reloadable bthread_concurrency
r28829 czy  Fix compile errors
r28821 jge  Status is formatable and resetable.
r28821 jge  Disallow Slice to be constructed from const char*/std::string implicitly.
r28819 jrj  Spin serveral times inside RoundRobinLoadBalancer
r28818 czy  Add a blank line to COMAKE so that there's a new version in agile.baidu.com and we can release it
r28816 czy  Update Makefile
r28811 czy  Update Makefile
r28810 czy  Just touch a file to release
r28808 czy  Fix the bug that g_concurrency was not changed when using default_concurrency
r28807 czy  Remove MutexWithRecorder::operator mutex_type&()
r28803 jrj  Fix a errno problem
r28801 czy  Update switch_trunk
r28798 jrj  Socket::ConnectIfNot inside Socket::Write now use asynchronous Connect
r28798 jrj  Remove a BAIDU_CHECK on correlation_id inside Controller
r28798 jrj  Add callback parameter to Socket::Connect which means to connect asynchronously. It will wrap context/callback information into EpollOutRequest, and then add this into epoll/timeout function. Since the onwership of EpollOutRequest has been passed to a temporary Socket, there is no race on the callback function
r28797 czy  Specilized unique_lock and lock_guard are not allowed to be copied and assigned
r28796 czy  Add MutexWithRecorder
r28789 czy  Add some vars
r28788 czy  Add lock_timer which can measure the average contention time by a certain lock
r28783 czy  Minor changes
r28783 czy  Add auto-started time
r28776 jia  Fix UT.
r28759 jge  StartCancel and JoinResponse do not rely on Controller
r28758 jge  Not use controller after timer_add
r28755 jge  Added ServerOptions::has_builtin_service to disable builtin services
r28754 jge  Use thread-safe localtime_r instead of localtime
r28754 jge  Generate ISize OSize IMsg OMsg per-second or per-minute from Socket counters and show them at /connections
r28754 jge  Disabled Socket::eof
r28754 jge  Count in/out bytes/messages of Socket.
r28753 czy  Fix a bug in /status
r28750 jge  Serve::Start accepts hostname as well
r28747 jge  Redirect to /rpcz when /rpcz/enable is successful
r28746 jge  Show iobuf_block_count and iobuf_block_memory again (not exposed as bvar by IOBuf now).
r28746 jge  Renamed baidu_rpc_server_xxx to rpc_server_<port>_xxx to avoid conflict between different servers inside a process.
r28743 jge  Channel fails to initialize when naming service returns nothing at the first time
r28739 jge  new_bthread=false in HandleSendFailed for sync calls
r28739 jge  extension names are case-insensitive
r28738 jge  HandleSendFailed runs the callback in new thread
r28737 jge  Changes according to latest IOBuf
r28737 jge  Add CONNECT_IN_KEEPWRITE in test_socket
r28735 jrj  Store ChannelContext in boost::intrusive_ptr
r28735 jrj  Store a reference to Socket during RPC in Controller so that Channel can be destroyed after sending an asynchronous RPC
r28735 jrj  Rearrange fields of Controller in a reasonable manor
r28735 jrj  Minor fixes in example
r28735 jrj  Add Socket::ReleaseAdditionalReference so that `Socket' can be recycled by this besides `SetFailed'
r28733 jge  IOBuf uses atomic to count blocks instead of bvar
r28733 jge  IOBuf::Block can be dynamically sized
r28733 jge  inline modifies impl. now rather than declaration.
r28731 jge  Force expr to be inside [0, 1] in BAIDU_(UN)LIKELY
r28729 czy  Fix the bug that g_channel_conn is not correctly updated
r28721 jge  Use fopen instead of fstream in file_naming_service.cpp because errno is unspecified when fstream fails to open
r28721 jge  Separate SocketUniquePtr into socket_id.h
r28713 jge  Replace all LOG_NE with noflush
r28711 jge  Minor changes
r28710 jrj  Fix a comparison between signed and unsigned
r28709 jrj  Change port number in test_socket.cpp
r28708 jrj  Replace BAIDU_FATAL with LOG(FATAL)
r28707 jge  thread_atexit() can be called inside a callback to thread_atexit()
r28707 jge  streaming log can be used before main() and destroyed when thread exits
r28707 jge  LogStream inherits CharArrayStreamBuf instead of ostringstream to prevent the extra copy (by ostringstream::str())
r28707 jge  LOG_NE is deprecated. Send noflush to streams instead.
r28707 jge  Added debugging versions of logging macros, DLOG, DVLOG ...
r28702 jrj  Trivial CI to trigger agile build
r28698 jge  Match full_module(filepath removed extname) when module does not match anything.
r28698 jge  Add VLOG2 and VLOG2_IF which accepts a virtual path instead of default __FILE__
r28696 jrj  Coding style fixes
r28696 jrj  Add `ub_long_connectin' in UbChannelOption corresponding to `DefaultConnectType' in UB
r28696 jrj  Add build.sh which will be used by comake2 when building this module as dependency
r28694 jrj  Trivial changes to trigger agile build
r28692 jrj  Update Makefile
r28691 jrj  Add reloadable gflag `log_connection_close'
r28690 jge  Update Makefile
r28688 jge  Trivial change just for triggering agile build.
r28687 jrj  Change default timeout to 1s
r28687 jrj  Add UT for authentication and idle connection
r28683 jge  include <set> in builtin_service.cpp
r28682 jge  Refactored /pprof/symbol using pre-generated results from nm rather than addr2line on need. It's aware of SO mapping offset and working for gcc4 now.
r28682 jge  Move most gflags into namespace again.
r28682 jge  flags/NAME?setvalue=VALUE reloads value of a gflag which must has a validator. Many gflags of baidu-rpc become reloadable now.
r28682 jge  flags can be viewed selectively like vars
r28682 jge  build the module no matter switched to trunk or not. build baidu-rpc as well.
r28679 jge  VLOG only keeps basename of the file (as module name). --verbose and --verbose_module should be both reloadable now.
r28679 jge  Extend slice with methods that find characters inside the slice. Add UT.
r28678 jge  /vars matches multiple glob names in the url directly, no need to add query string.
r28678 jge  Replace BAIDU_<loglevel> with LOG(<loglevel>)
r28676 jge  Replaced likely/unlikely with BAIDU_LIKELY/BAIDU_UNLIKELY
r28676 jge  Renamed FLAGS_v and FLAGS_vmodule to FLAGS_verbose and FLAGS_verbose_module respectively to avoid potential conflict with glog
r28676 jge  A couple of fixes to make sure streaming_log can be included before/after glog.
r28673 jge  Simplify code with IOBufBuilder
r28673 jge  Send attachement to ostream directly
r28673 jge  Replace BAIDU_FATAL/BAIDU_TRACE in examples with LOG(FATAL) and LOG(TRACE)
r28673 jge  fix GetFdNum()
r28671 jge  Not include check.h
r28668 jge  Merge BAIDU_CHECK into streaming_log
r28668 jge  Implement LOG/VLOG
r28667 jge  IOBuf can be sent to ostream directly
r28667 jge  Added IOBufBuilder which is an ostream
r28666 jrj  Replace short_connection option with ConnectionType, which is an enum type in options.proto
r28666 jrj  Relevant changes of UT, example, log
r28665 jge  Avoid including check.h
r28662 jrj  Support for ub raw data
r28654 jge  Include <map> in test_cond.cpp
r28653 czy  Add ServerOptions::num_threads
r28651 jge  Include <typeinfo> in class_name.h
r28649 jrj  Add support for sending public/ub requests via public/baidu-rpc
r28648 jge  rpcz improvements: time can ignore date, log_id can be read/printed in hexadecimal.
r28647 czy  bthread_setconcurrency() can be called after some bthread is running
r28645 jrj  Set protocol index when sending requests via short connection
r28645 jrj  Add new ProtocolType: PROTOCOL_UBRPC
r28644 jge  Add type in spans
r28644 jge  Add ServerOptions.pid_file
r28642 jge  /rpcz should show client spans correctly
r28642 jge  /rpcz results can be filtered by min_latency, min_reqeust_size, min_response_size, log_id, error_code, "max" is renamed to max_scan
r28642 jge  Fix style of several enums
r28642 jge  Add log_id in SpanBrief
r28638 jge  Add slice and status
r28637 jge  Support -usercode_in_pthread. When the flag is on, user callback will be run on stack of worker pthreads. Default is false.
r28637 jge  More comments and minor changes to InputMessenger::OnNewMessages()
r28637 jge  Added /threads which calls pstack (slow), /dir which is a file explorer of the server machine. They should be invisible to end users.
r28636 jge  Support BTHREAD_STACKTYPE_PTHREAD. bthreads started with this attribute will run on stack of worker pthread and all bthread functions that would block the bthread will block the pthread. The bthread will not allocate its own stack, simply occupying a little meta memory. This is required to run JNI code which checks layout of stack. The obvious drawback is that you need more worker pthreads when you have a lot of such bthreads.
r28636 jge  Main task and pthread task cannot create foreground tasks.
r28636 jge  bthread_usleep does not accept negative microseconds.
r28633 czy  Clear source buffer before calling user callback to reduce memory residence
r28591 jge  Use bthread_usleep instead of usleep
r28590 jge  Removed baidu/bthread/sys_sleep.cpp baidu/bthread/sys_sleep.h test/test_hook.cpp
r28590 jge  Not hook usleep/sleep. Add bthread_usleep back.
r28589 jrj  Fix log off which now wait for in-processing requests instead of letting client close the connection
r28588 jge  Reopen DB after it's removed.
r28588 jge  More links for rpcz on /status page
r28588 jge  Make UT of snappy loop less.
r28579 jge  Fix COMAKE and warning
r28578 jge  /rpcz can be searched by time.
r28578 jge  older spans before FLAGS_span_keeping_seconds are evicted from id_db and time_db now. GrabSpans and IndexSpans are separated into different threads. span_queue.h(cpp) is internally used in SpanSearch.
r28578 jge  Fix warnings in test/test_snappy_compress.cpp and make the perf case shorter.
r28578 jge  Create server span for nova protocol.
r28578 jge  Create server span for nova protocol.
r28578 jge  /connections shows long/short sockets of channels.
r28578 jge  CompressHandler has names and are shown in /status. protocols are also shown.
r28578 jge  Changed signature of CustomizedStatus::DescribeStatus.
r28578 jge  Added -ldl -lz in all examples to make them compile under g++4.8. blackbox is broken under g++4.8 now.
r28574 jia  ISSUE = 623514
r28574 jia  Add snappy compress/decompress in baidu-rpc.
r28555 jge  Print welcome in /index page
r28540 jge  User can set reason_phrase.
r28540 jge  Tidy http_header.h and uri.h which are exposed to users.
r28540 jge  status-code is typed int now.
r28540 jge  Send query/fragment in http request, fake user-agent as curl.
r28540 jge  Remove loop in example/http_c++
r28534 jge  trace-id/span-id on rpcz page are displayed/parsed as hexademical.
r28534 jge  Extensively improved http client: accessing protobuf services are POST now (which should be). "Host" "Accept" "User-Agent" are set for GET.  Channel accepts http url now. Users can assign url to  Controller.request_http_header().uri(). http-client does not need stub (method/request/response all can be NULL).
r28534 jge  Added example/http_c++
r28521 czy  Fix bugs of load balancer
r28507 jge  Use CPPFLAGS instead of CXXFLAGS in examples.
r28507 jge  Removed -w from COMAKEs(don't see boost warnings?) and fixed a couple of warnings
r28507 jge  Minor changes under tools/
r28507 jge  Fix broken blackbox/run.sh in my laptop
r28506 jge  Add "Exception: " prefix before print exception
r28504 jge  Merge id into combiner
r28492 jge  Changed signatures of CompressHandler to use ZeroCopyStream instead of IOBuf
r28487 jge  Fix memory leak in brought in r28455
r28485 czy  Add more process status
r28483 jge  Always set SofaRpcMeta::failed which is requied by sofa-pbrpc client currently(1.0.1.28195)
r28466 jge  Modify COMAKE so that some flags are also applied to C files
r28464 jge  IOBuf does not use fast_memcpy because the module does not support PIC currently.
r28464 jge  Fix a warning in UT
r28462 jge  Support protocol of sofa-pbrpc
r28462 jge  Change ports in examples to 800x
r28460 czy  Add a interface to start a Server from a range of ports
r28459 czy  Add request_protocol() in Controller
r28458 czy  Remove fatal log in Server::Stop() and Server::Join when server has been stopped
r28455 czy  IOBufAsZeroCopyStream owns Block before BackUp() is called
r28454 jge  update sofa_pbrpc_meta.proto
r28451 jge  rpcz displays better when error happens.
r28451 jge  Fix an bug in InputMessenger that EINTR is not ignored.
r28447 jge  Not store and join _event_thread and _health_check_thread in Socket.
r28447 jge  /index are hyperlinked.
r28447 jge  Implemented rpcz for baidu_std and hulu protocol, left issues are tracked at http://git.baidu.com/gejun/baidu-rpc-development/issues/60
r28447 jge  Constructors of Socket/Span are explicitly forbidden.
r28447 jge  Added example/cascade_echo_c++/ in which case the server calls itself for request.depth() times.
r28445 jge  Not suspend until runqueue being empty When control is stopped
r28445 jge  Add a special TLS pointer for baidu-rpc to implement rpcz
r28422 czy  Not convert response into response_attachment when there's response_attachment even if the content-type is "application/json"
r28410 czy  Add clear_attachment_after_sent flag in Controller
r28407 jrj  Fix UT
r28402 czy  Fix compile errors in GCC4.8.2
r28401 czy  Fix compile errors in GCC4.8.2
r28399 jrj  Add default timeout/max_retry in ChannelOption which can be overridden by Controller
r28379 lsz  add return bvars case
r28369 jrj  Add bthread_set_worker_startfn
r28362 jge  Use normal-stack to run user callbacks
r28362 jge  Respond plain text if user-agent is curl
r28362 jge  Add more bvar counters
r28360 jge  Add PassiveStatus
r28302 jge  explicitly hide variables before any other field gets destructed. this is a must in all subclasses of Variable now.
r28299 jge  Add api to get #blocks
r28298 jge  When a method is not found, call "handle_unknown" of the service(if the method exists) and add query string "unknown_method=<the-method-name>". The grants baidu-rpc the ability to handle dynamic URI
r28298 jge  Replace several counters with bvar in examples
r28298 jge  Implement /vars?name=NAME1,NAME2...NAMEn, /vars?prefix=PREFIX, /vars/NAME
r28298 jge  Add use_html in CustomizedStatus::DescribeStatus
r28290 jge  Use bvar
r28290 jge  Removed test/Makefile
r28290 jge  Add IOPortal::clear() to override IOBuf::clear() which fixes the memory leak in baidu::rpc::Socket
r28289 jge  Changed constructor of Status
r28287 jge  Add contructors to expose variables on construction
r28286 jge  Renamed init_result/init_element to result_identity/element_identity respectively because former ones are very likely to be wrongly understood.
r28286 jge  Make Maxer/Miner only default-constructible, Adder constructible with initial value.
r28284 jge  Make methods for accessing variables thread-safe
r28281 jge  Use Variable as the base of all bvar which can be exposed and listed globally.
r28281 jge  Use operator<< instead of push_element/add_sample respectively
r28281 jge  Remove test/Makefile
r28281 jge  Remove init() from reducer and recorder. BinaryOp can be stateful.
r28281 jge  Remove a couple of unused files
r28281 jge  Add baidu::bvar::Status
r28281 jge  Add Adder/Maxer/Miner
r28275 czy  Remove reference descriptor
r28274 jge  Add more modules in tools/switch_trunk
r28272 jge  Restrict http to use short connection.
r28272 jge  Removed correlation-id related from HttpMessage
r28272 jge  example/echo++ example/multi_threaded_echo_c++ work correctly with -use_http
r28271 jge  HttpMessage::ParseFromIOBuf does not print non-debug log
r28271 jge  Add Controller::set_short_connection()
r28270 jge  Remove an inaccurate assertion
r28269 jge  Make http work again (which is broken in previous CI)
r28269 jge  Fix a quote issue in blackbox/run.sh
r28269 jge  exmaple/echo_c++ have different flags on attachment.
r28269 jge  Change ports in examples from 900x to 700x
r28264 jge  Support http client (still have bug under MT)
r28262 jrj  Change proto option extension number
r28260 czy  Add BCLOUD
r28259 czy  Use ci-base for public/common
r28259 czy  Add IntRecorder::sample_count
r28259 czy  Add const descriptor
r28252 jge  Socket::_msg_size is more accurate.
r28252 jge  Channel returns EHOSTDOWN when signle server is not addressable.
r28252 jge  Adapt change of IOBuf
r28251 jge  Tweak some constant parameters, say BLOCK_SIZE is changed from 4K to 8K
r28251 jge  Refactored IOPortal::append_from_file_descriptor to improve its throughput with current BLOCK_SIZE
r28248 jge  Fix test_iobuf so that it does not complain about "Bad block"
r28248 jge  Add test/pprof
r28245 lsz  hide the report generation call through annotation
r28242 lsz  add reports&report httpd server
r28240 jge  Fixed a bug that cached short connections are not set-failed on EHOSTDOWN
r28240 jge  example/multi_threaded_echo_c++ can change size of request/attachment and its behavior changes slightly.
r28240 jge  Correct some comments
r28240 jge  Auto determine max_once_read when reading from _read_buf based on avg size of ast 10 messages.
r28240 jge  Add /connections page which displays detail of all connections
r28187 jge  Refine comments in examples
r28187 jge  Fix a bug brought in r28144 that the iteration waiting for nref == expected was break-ed incorrectly.
r28187 jge  Add missed tcmalloc in example/multi_threaded_echo_fns_c++/COMAKE
r28180 jrj  Replace wiki url to http://wiki.baidu.com/display/SAT/baidu-rpc
r28178 jrj  Use pipe fd to wakeup EventDispatcher
r28176 jrj  For production
r28173 jge  Tidy example/echo_c++_nova_pbrpc/COMAKE
r28171 jge  Tidy COMAKE of examples
r28171 jge  Show cpu_profiler_enabled and heap_profiler_enabled in /status
r28165 jge  Set versions in examples with their names
r28165 jge  Better /index page
r28164 jge  Support ListNamingService which reads servers from the url directly.
r28164 jge  Show supported lb and ns in /status
r28164 jge  Extension<T>::List clears input string.
r28163 jge  Skip heading space in hostname2ip
r28162 jge  Remove an incorrect comment from channel.h
r28161 jge  User can customize status for service by inheriting baidu::rpc::CustomizedStatus (already included by server.h)
r28160 jge  Add tools/switch_trunk to switch @ci-base module to trunk (safely)
r28159 jge  Retry on ENOENT
r28159 jge  Removed src/baidu/rpc/test/Makefile from repository
r28159 jge  Refactored FileNamingService to reload the list according to baidu::FileWatcher
r28159 jge  oneshot_wait was added after the WriteRequest was successfully written. If the adding is failed, reset the oneshot.
r28159 jge  Not comake2 -UB in blackbox/run.sh to prevent it from making whole code tree to branches.
r28159 jge  LoadBalancers do not print log if there's no server to choose.
r28159 jge  interval of PeriodicNamingService is configurable
r28159 jge  Improve threaded examples, added stuck_bug.sh and random_kill.sh
r28159 jge  Improve logs on fd/socket
r28158 jge  Fix FileWatcher::init() that timestamp calculation was wrong.
r28158 jge  Define bthread_self to be EMPTY weak symbol
r28157 jge  Use microsecond timestamps in FileWatcher so that frequent change to the watched file will not be missed (provided the file system supports subsecond timestamp)
r28156 jge  Use lowest bit of _pending_signal as the stopping flag so that stop will not be missed.
r28156 jge  Removed test/Makefile
r28156 jge  Added BTHREAD_SIGNAL_TYPE=1 which is the simplest signal_task/wait_task
r28144 jge  Address socket for KeepWrite in Write to avoid race
r28138 jge  Reset socket(closing fd) at the beginning of HC
r28138 jge  Replace echo with add-1 in multi-threaded examples, fix bugs.
r28138 jge  Improve logs on sockets.
r28131 jge  Added multi_threaded_echo_fns_c++
r28130 lsz  fix the problems that server.stop() sometimes hanged
r28127 jge  Replace ELOGOFF with EHOSTDOWN
r28127 jge  Put auth into options
r28126 czy  AgentGroup::s_free_ids is singleton instance
r28124 jge  Fix example/echo_c++_nova_pbrpc
r28121 lsz  add fail over case
r28112 jge  Fix comment in server.h
r28110 czy  Add missing fiels
r28107 czy  Add makefile
r28106 czy  Rename variable_table to variable_info
r28106 czy  Add reset to recorder and reducer
r28105 jge  Fix comments in examples
r28104 jge  Change a comment
r28103 jge  Log adjust in http
r28102 jge  Refine FetchServiceMethodName
r28098 czy  Rename bstatus and related stuff to bvar
r28096 jrj  Use ci-base version for protobuf-json
r28095 czy  Copy source file from public/bstatus
r28092 czy  Add BCLOUD
r28091 czy  Copy from public/bstatus
r28090 jge  When URL is accessed without any service name, return /index as the default page
r28090 jge  unique_service() always works no matter if Server is running or not.
r28090 jge  Support /pprof/growth.
r28090 jge  /status shows sth useful.
r28090 jge  Some methods of Server are removed or moved. GetUserServices is renamed to ListServers and having a different signature.
r28090 jge  Renamed Socket::GetAuthenticator to Socket::auth
r28090 jge  Refactored message parsing related code to make interfaces/life-time more clear.
r28090 jge  Make comments in server.h more readable.
r28090 jge  /flags returns text/html as default, text/plain when ?console=1 is present. When a flag is changed, its default value will be shown and in text/html mode the value will be red-color.
r28090 jge  Fix Server::_builtin_service_num which is not updated in previous RemoveServer/ClearServers
r28090 jge  Fixed missed return in ProcessHttpRequest after failing to parse json
r28090 jge  Added Server::set_version which is displayed at /version.
r28090 jge  Added Controller::set_type_of_service(), however it's not even unit-tested.
r28089 czy  Change the order of protocol
r28088 lsz  add async client blackbox case && fix symbol problem for pprof service
r28085 jrj  Fix a type in BCLOUD
r28083 jge  Fix a compilation issue with variables prefixed namespaces
r28082 jrj  Add operator<< for AuthContext
r28082 jrj  Add BCLOUD
r28081 czy  Include bthread_errno.h in the header of user interface
r28080 czy  Redefine errno to fix the `__attribute__(__const__)' issue
r28078 jrj  Add Phony
r28076 jrj  Add Phony
r28072 jrj  Add Phony
r28066 czy  Declare the helper class of BAIDU_GLOBAL_INIT in anonymous namespace to avoid the redefinition issue
r28062 jrj  Add BCLOUD
r28059 czy  Minor fix
r28059 czy  Add HttpMethod
r28056 jrj  Minor fixes on COMAKE
r28055 jrj  Minor fix on COMAKE
r28052 jrj  Add BCLOUD
r28047 jge  log cpu/heap profile requester
r28045 jge  Fix compilation error for an unused branch
r28042 jrj  Add BCLOUD
r28041 jrj  Remove BTHREAD_NOSIGNAL since it's unstable
r28041 jrj  Move non-user stuff in Controller into ControllerPrivateAccessor
r28041 jrj  Fix some UTs
r28041 jrj  echo_c++_nova_pbrpc now can use baidu-rpc
r28041 jrj  Add support for sending nova-pbrpc requests
r28041 jrj  Add support for asynchronous callback of Http protocol
r28039 czy  Fix comile error in gcc4.8.2
r28039 czy  Add module_info
r28038 czy  Add module_info
r28037 czy  Fix compile error in gcc4.8.2
r28021 jge  Remove Makefiles from examples
r28021 jge  .prof files generated by /pprof/profile is put into directory prof_tmp/
r28021 jge  /pprof/symbol supports symbols of shared libraries.
r28001 jge  Rename -giano_authentication in examples to -auth, and make -auth in servers false as default.
r28001 jge  Reimplement pprof/heap so that it can be used by pprof.
r28000 czy  Fix compile issue with gcc4.8.2 and boost 1.56
r27999 czy  Remove Makefile
r27996 czy  Compiled in gcc4.8.2
r27996 czy  Change boost to boost1.56
r27995 czy  Minor fix
r27995 czy  Compiled in both gcc4.8.2 and gcc3.4.5
r27995 czy  Change boost to boost1.56
r27993 jge  Rename input_event_dispatcher and accept_messenger to event_dispatcher and acceptor respectively
r27989 jge  throughput of Socket::Write is slightly higher
r27989 jge  Number of edisp is tunnable
r27989 jge  Make fd no-delay and close-on-exec as default.
r27989 jge  Enable one bthread per request
r27988 jge  Disable a log temporarily
r27986 jge  Replace send_http_request with probe
r27985 jge  ResourcePool/ObjectPool can limit thread-cached size dynamically, Replaced cancel_resource/cancel_object with ResourcePoolValidator/ObjectPoolValidator.
r27985 jge  Remove `using namespace' in most tests
r27985 jge  Refactored stack allocation. stacktype is limited to small/normal/large.
r27985 jge  Enable previous scheduling algorithm in TaskControl and TaskGroup.
r27980 jge  Arrange fields of Controller
r27978 lsz  re-commit issue 581895 and regenerated Makefile for unittest
r27977 lsz  revert r27976 to fix trunk failure.
r27976 lsz  add pprof::heap() builtin service
r27975 czy  Not call Clear() in the contructors and destructors of HttpMessage and URI
r27972 jge  Remove WARNINGS reported by COMAKE
r27971 jge  Remove WARNINGS reported by COMAKE
r27969 czy  Fix compile errors
r27968 czy  Fix compile errors
r27958 czy  Remove http_header_builder.h/cpp
r27958 czy  Refine http protocol
r27958 czy  Add HttpHeader (http_header.h/cpp) which is used by user to get/set user header fields
r27940 lsz  add pprof service blackbox cases & auto backup failed cases
r27927 jrj  Use offset+stride to retry another server if `Address' failed in RandomizedLoadBalancer
r27911 lsz  add blackbox cases for builtin service
r27852 lia  add BCLOUD
r27851 jge  fix typo and make jpaas_control executable
r27848 lsz  add load-balance blackbox test cases
r27847 jrj  Move pthread_once_t binding to RegisterAllExtensionsOrDie from channel.cpp/server.cpp to policy/register_all_extensions.cpp to avoid duplicate initialization
r27847 jrj  Add UT for Server
r27789 czy  Add Method list into builtin service to show all user-register services
r27788 jrj  Use data member atomic offset RoundRobinLoadBalancer::_cur_offset to replace tls_offset_and_stride as the latter is not strict round-robin and can't work if there are multiple RoundRobinLoadBalancer
r27788 jrj  Start a bthread to close idle connections (connections that has no data transmission during a period of time) in AcceptMessenger. Implementation: Each time a read/write operation occurs, Socket::_last_active_us will be  updated. Server can activate this feature via ServerOptions::idle_timeout_sec
r27788 jrj  Replace @ci-base with @1.1.3.1266 for CONFIGS(giano) since older version of comake2 doesn't support the first form
r27788 jrj  Remove unused *.pb.cc files in src/baidu/rpc/ (they are moved into src/baidu/rpc/policy)
r27788 jrj  Remove support for TalkType in hulu_pbrpc_protocol.* as nobody has used this so far  
r27788 jrj  Relevant changes to UT
r27788 jrj  Pass non-NULL closure done into server's callback to support asynchronous server. As a result, resources such as request, response and controller can't be  deleted until done has been called. This job has been done on all protocols except HTTP (TODO)
r27788 jrj  Add parameter new_bthread to Controller::OnRPCReturned to control whether to create a new bthread to run this function. Now Channel::HandleSocketFailed  calls this function with new_bthread=true to avoid blocking problems
r27788 jrj  Add gflag idle_timeout_s in example servers
r27788 jrj  Add connection pool to reuse short connection with 2 API: GetShortConnection/ReturnShortConnection. Each endpoint can have at most 16 (gflag) connections by default
r27785 czy  Set error_code for every response
r27761 jge  Change behavior of signalings in bthread_oneshot
r27761 jge  Add a rough impl of bthread_yield
r27760 jge  sleep on fail in example
r27757 jge  changes according to bthread r27756
r27756 jge  TaskGroup and TaskControl are extensively changed to implement new scheduling algorithm which is more scalable than current implementations.
r27756 jge  Split unstable bthread functions into bthread_unstable.h
r27756 jge  Removed bthread_start_urgent_short bthread_batch_start bthread_batch_signal bthread_revive
r27756 jge  Move cpu_relax barrier into baidu/bthread/processor.h
r27755 czy  Compiled in gcc4.8.2
r27754 lsz  make simple service/LB/performance case runnable & change MessageFile to use IOBuf instead
r27752 jge  Misc improvements on tools/makeproj: jpass support, more intuitive parameters, more comments in generated code
r27751 jge  Add test_cacheline.cpp
r27728 jge  Refactored builtin service additions in Server
r27728 jge  Implement pprof/profile pprof/symbol pprof/cmdline roughly, /pprof/symbol cannot interpret addresses in shared libraries yet.
r27728 jge  Added closure_guard.h to ease calling done.
r27725 jge  Add missed inline in string_splitter.h to avoid redefinition.
r27722 jrj  Move all RPC protocol stuff into policy/ including *.proto files and their auto-generated files
r27722 jrj  Add src/baidu/rpc/protocol.* and make RPC protcool as extension which can be registered and fetched using global functions. Use these APIs to initialize InputMessenger at client/server side
r27721 jge  Move source files depended by makeproj into tools/makeproj_template
r27716 lsz  add automatic blackbox test framework and make first case runnable
r27686 jrj  Uniform RPC failure path between synchronous and asynchronous RPC and support retry. All failed situation will enter Controller::OnRPCReturned which could trigger retry according to the error code, maximum retry  times and timeout. Both kinds of RPC use bthread_timer_add (instead of bthread_oneshot_wait(timeout)) to register RPC timeout
r27686 jrj  Simplify authentication API in Socket. Merge _auth_error and _auth_flag in order to prevent race conditions between setting 2 variables  
r27686 jrj  Replace butex with oneshot in NamingServiceThread::_wait_cond and Add NamingServiceActions::SetFailed to signal the oneshot. Otherwise,  WaitForFirstBatchOfServers could wait forever if it failed to get the first  batch of servers
r27686 jrj  Print verification failure log inside Authenticator instead of InputMessenger
r27686 jrj  Fix a bug when parsing naming url
r27686 jrj  Fix a bug that may cause missing epoll events in InputMessenger::OnNewMessages
r27686 jrj  Fix a bug in NamingServiceThread as boost::instrusive_ptr will automatically add reference when calling reset
r27686 jrj  Fix a bug in NamingServiceThread::Actions::ResetServers that vectors must be resized before calling set_difference
r27686 jrj  Channel can be deleted after CallMethod returns even when sending asynchronous RPC, since we copy all the information we need from Channel to Controller. However, the LoadBalancer information can't be copied and therefore it will return EINVAL if it needs retry and Channel has already been deleted. Use an additional Socket to save LoadBalancer (ChannelContext) so that we can test its validity by using Socket::Address inside the callback
r27686 jrj  Change AcceptMessenger::Stop(int) and AcceptMessenger::Join to support logoff. User can use Server::Stop(int) and Server::Join instead. If a  server enters logoff state, it closes its listening socket and respond  any incoming requests with an ELOGOFF error. The state lasts until all  the clients close their connections to server or it reaches a timeout when server will perform active close on all its connections. Related  changes to examples
r27686 jrj  Add UT: test_channel
r27686 jrj  AcceptMessenger::Start, Stop, Join is now thread-safe
r27686 jrj  AcceptMessenger and Server can re-start (call Start twice) once it has been stopped and joined (call Stop and Join)
r27679 jge  Simplify Socket::ProcessEvent
r27678 jge  bthread_oneshot_signal returns EINVAL if it's called after a bthread_oneshot_fight
r27677 jge  namespace is changeable in tools/makeproj
r27676 jge  Compute relative WORKROOT for COMAKE in tools/makeproj
r27675 jge  Move changelog.sh into tools/changelog
r27674 jge  Copy headers of baidu-rpc to output/include
r27674 jge  Add tools/makeproj to create runnable projects on baidu-rpc
r27666 jge  Not call on_reset of the oneshot object when Write() fails
r27664 jge  Use gtest to write test_sched_yield (to make jenkins happy)
r27663 jge  Changes according to bthread_oneshot
r27662 jge  replaced bthread_oneshot_destroy with bthread_oneshot_reset
r27660 jge  Removed error_code from bthread_oneshot functions. User should manage it by their own.
r27659 jge  Add a test on sched_yield
r27658 jge  bthread_oneshot_list_signal changed signature of the callback.
r27658 jge  Add test_ping_pong.cpp to test repeated wakeup of peer threads
r27651 jrj  Fix a hanging bug when bthread quit
r27626 jge  Remove dependency on nshead
r27612 jge  Limit # of thread-cached Sockets to be 32
r27612 jge  Clear Socket::_read_buf on recycle
r27604 jge  Add reuse_addr as a flag (of server side)
r27528 jge  Fix misc (too-early) assertions in test_socket.cpp
r27528 jge  Fix bugs in AcceptMessenger: Creation of socket must be last in StartAccept; Creation of mutex/condition is moved into ctor/dtor; Removed _initialized/_started which may cause thread-unsafety.
r27528 jge  Fix a bug in Socket::KeepWrite that a condition is reversed. -This line, and those below, will be ignored-- M    src/baidu/rpc/accept_messenger.cpp M    src/baidu/rpc/socket_inl.h M    src/baidu/rpc/socket.cpp M    src/baidu/rpc/test/test_socket.cpp M    src/baidu/rpc/test/Makefile M    src/baidu/rpc/accept_messenger.h M    src/baidu/rpc/socket.h
r27528 jge  Fix a bug in Socket::HealthCheckThread that usleep held the unique_ptr (so that Socket cannot be recycled during health-checking). Combine the two addressing into one.
r27524 jge  Suspend deletion of TaskGroup when runqueue is not empty (otherwise the task is lost)
r27524 jge  Move sys_sleep/sys_usleep into baidu/bthread/sys_sleep.h(cpp)
r27513 jge  adapt bthread change and fix a bug of Revive
r27512 jge  usleep sets errno and returns -1 on error
r27500 czy  Minor fix
r27500 czy  Add nova_pbrpc_support in server side to receive request from clients which use NOVA pb-rpc
r27500 czy  Add example which use NOVA pb-rpc as client
r27499 jge  Make bthread_types.h accessible by C
r27490 jge  Server creates _SC_NPROCESSORS_ONLN worker threads as default.
r27490 jge  Not set response when Controller::ErrorCode() is zero in baidu_rpc_support.cpp
r27490 jge  Not set_error_code when Controller::ErrorText() is empty() in hulu_pbrpc_support.cpp
r27490 jge  Minor changes in examples
r27490 jge  Ignores SIGPIPE in both Channel and Server
r27475 jge  Change according to bthread_oneshot_list_signal
r27474 jge  Turn off BTHREAD_COMBINE_SIGNAL
r27474 jge  Add handle_data as last parameter to bthread_oneshot_list_signal
r27471 czy  Rename baidu_pbrpc_* to baidu_rpc_*
r27471 czy  Remove errno.h/cpp, errnos are defined in errno.proto
r27471 czy  Minor fix
r27471 czy  BaiduRpc protocol is the default protocol of Channel
r27471 czy  Add baidu_rpc_support
r27463 czy  Add send_http_request
r27451 jge  default concurrency is a flag now.
r27451 jge  Add a new method to combine signal
r27442 jge  Make example/multi_threaded_echo_c++ configurable
r27437 jge  Fix a bug that remove_tls_block should deref rather than deleting the block.
r27437 jge  Disable caching thread-local IOBuf::Block by default.
r27436 jge  Add gflags in test/COMAKE
r27435 jge  Revert change to health-checking in r27419
r27435 jge  Comment refinement
r27434 jge  Replace some global variables with gflags.
r27434 jge  Add bthread_timer_add() and bthread_timer_del()
r27434 jge  Add bthread_about_to_quit()
r27419 jge  health-check when ever connected.
r27419 jge  Comment refinement.
r27418 jge  Fixed test/test_bthread.cpp
r27418 jge  Fixed sleep in bthread.cpp that the argument is not multiplied with 1000000L
r27416 jge  Initialize sys_usleep/sys_sleep at start time.
r27411 jge  Added tools/pprof
r27409 jge  Use usleep instead of bthread_usleep(which is removed)
r27406 jge  Hook usleep and sleep. Removed bthread_usleep and bthread_sleep
r27406 jge  Add a fast fail path for bthread_setconcurrency
r27389 jge  bthread_setconcurrency changes concurrency before calling other bthread functions.
r27389 jge  Added bthread_sleep
r27386 jrj  Update example to use new API of giano mock
r27381 jrj  Minor fix on coverage selection
r27380 jrj  Fix baas-lib-c dependency
r27379 jrj  Use gflags in examples
r27379 jrj  Store AuthContext in Socket and user can access it via Controller in server
r27379 jrj  Relative UT (test_socket)
r27379 jrj  Pass Authenticator* to Channel::Init and Server::Start to enable authentication
r27379 jrj  Disable coverage compile at first in local_build.sh and remove *.pb.* files out of coverage summary
r27379 jrj  Add Verify callback in InputMessageHandler to verifyt credential
r27379 jrj  Add Socket::WaitEpollOut which uses a butex to wait for EPOLLOUT event
r27379 jrj  Add Socket::Connect which establish tcp connection only blocking bthread (not pthread). Replace tcp_connect 
r27379 jrj  Add CreateShortConnection to support short connection
r27379 jrj  Add Authenticator (base class) and implement GianoAuthenticator
r27379 jrj  Add authentication in examples
r27379 jrj  Add authentication API in Socket, which has similar syntax as bthread_oneshot_t
r27379 jrj  Add AddEpollOut/RemoveEpollOut in InputEventDispatcher
r27361 jge  Renamed <protocol>_message to <protocol>_support
r27360 jge  Changed email in examples to pbrpc@baidu.com
r27359 jge  Socket::Create returns int instead of Socket*
r27345 czy  Remove redundant files which are not successfully removed by svn merge
r27344 czy  Revert former changes svn merge -r 27343:27342 .
r27343 czy  Merge former changes to test whether the compilation would fail in jenkins
r27342 czy  Add http_status_code.cpp
r27341 czy  Add missing files
r27340 czy  version, health, status and flags become the methods of BuiltinService
r27340 czy  There's no content in http_response if rpc is failed because neither response nor user_attachment is well defined in this situation
r27340 czy  status just returns "It works", which is to be refined in the future
r27340 czy  Resonable http status code is set when a rpc call is failed in ProcessHttpRequest
r27340 czy  Remove show_version_service.h/cpp show_health_service.h/cpp show_flags_service.h/cpp show_flags_service.h/cpp
r27340 czy  Not include http_parser.h in http_message.h, _http_parser in HttpMessage is an opaque pointer
r27340 czy  Implement GetHeaders and GetHeaderInLowerCase of HttpMessage REAL_ISSUE=522540
r27340 czy  HttpMessage becomes a read only structure
r27340 czy  EtrorText of controller is to be set in the headers response when this rpc was failed. which is named "Error-Text"
r27340 czy  BuiltinSerivce is the default service if the path in uri contains only method name
r27340 czy  Add all defined http status code of HTTP/1.1 in http_status_code.h.
r27315 jge  Add a constructor of EndPoint on sockaddr_in
r27308 jge  Fix a bug that _rqs[i] is not set in destroy_group
r27307 jge  Removed invalid_servers from LoadBalancer::SelectServerReentrant.
r27307 jge  Make error code in baidu/rpc/errno.h to be "extern const" so that errno.o will not be stripped by linker.
r27307 jge  Health checking does not accept HealthWatcher anymore, ConnectPeriodically is renamed to HealthCheckThread. Added health_check_interval_s to Socket::Create(), when health_check_interval_s > 0, the HealthCheckThread will be created when SetFailed.
r27307 jge  Example fixes
r27307 jge  Add additionaly reference in SocketMapInsert. Remove it in SocketMapRemove.
r27298 jge  Stop TimerThread without waiting for left tasks, reverting the corresponding change in r24872
r27294 jge  TaskGroup::usleep returns errno after calling usleep.
r27294 jge  Make error_code defined in bthread_errno.h to "extern const" so that bthread_errno.o will not be stripped by linker
r27293 jge  berror() returns "General Error(-1)" for -1, "Unknown Error(the-error-code)" for unknown error code
r27284 jge  Define __const__ to empty even in gcc3.4
r27278 jge  Fix return code in bthread_connect
r27277 jge  LazyArray has smaller BLOCK_SIZE and addresses less fd.
r27277 jge  Clear LazyArray and LazyArray::Block at creation.
r27262 czy  Rename HttpMessage related stuff to DepositedHttpMessage
r27262 czy  Remove CreateHttpMessage from socket, uers now are responsible for creating http message now
r27262 czy  Not include http_message in socket.h
r27262 czy  Modify http message related stuff in Socket
r27262 czy  Add some builtin services
r27262 czy  Add ShowVersionService(show_version_service.h/cpp) to show the version of this server (which just returns "1.0.0.0" in plaintext now)
r27262 czy  Add ShowHealthService(show_health_service.h/cpp) to show the heath check result of this server (which just returns "It works." in plaintext now)
r27262 czy  Add ShowFlagsService(show_flags_service.h/cpp) to list all gflags of this process
r27262 czy  Add GetFaviconService(get_favicon_service.h/cpp) for browsers to acquire favicon.ico from rpc server
r27262 czy  Add Comments for http_message related stuff in socket.h
r27261 jrj  Minor changes
r27260 jrj  Add example of multiple threads
r27259 jge  Fixed a bug that a branch in InputMessenger may not recycle InputMessageBase
r27258 jge  Revert initialization of _singleton in ResourcePool and ObjectPool
r27257 jge  Fixed a bug that ResourcePoolBlockMaxItemNum should be declared in task_meta.h otherwise different compilation unit may see different values.
r27257 jge  Enable a seq_cst fence in WorkStealingQueue
r27255 jge  Initialize ObjectPool::singleton at runtime
r27254 jge  Initialize ResourcePool::singleton at runtime
r27253 jge  Limit TaskMeta per thread to 10 temporarily
r27248 jge  Move operator<< into baidu namespace
r27246 jge  Disable compressions in examples
r27245 jge  Add missed file
r27244 jge  Fix incorrect assertions.
r27243 jge  Added IOBuf::to_string()
r27242 jge  Fix a bug in Socket::Write that _write_head is incorrectly set to NULL when WriteRequest is not fully written.
r27242 jge  Clean up header dependencies in channel.h controller.h server.h
r27242 jge  Better documented example
r27241 jge  Fix a bug in accounting of main_stat.cputime_ns
r27241 jge  bthread_oneshot is automatically reyclced after wait/signal/destroy, the error_code is passed by user.
r27241 jge  Add more comments
r27241 jge  Add butex_wait_uninterruptible()
r27240 jge  Simplify hostname2ip
r27240 jge  Fixed a no-return bug in cpuwide_time_ns()
r27239 jge  Make BTHREAD_SMALL_STACKSIZE 32K
r27239 jge  Allocate stack by mmap as default.
r27236 jge  Update comment
r27235 jge  Update comment
r27234 jge  Start KeepWrite thread at background.
r27234 jge  Implemented Controller::StartCancel
r27231 jge  Added endpoint2str().
r27229 jge  Move echo.proto in examples from package `echo' to `example'
r27228 jge  Change several TRACE to FATAL in example
r27227 jge  Minor changes
r27225 jge  Minor change
r27222 jge  Update comments
r27221 jge  Change comment
r27220 jge  Renamed Controller::WaitForResponse to JoinResponse.
r27220 jge  Changed example/streaming_echo_c++ a lot.
r27218 jge  Refine comments in example
r27217 jge  Initialize _request and _done in Controller
r27209 jge  Renamed example/echo_example_c++ to example/echo_c++, tidy the code. Added example/streaming_echo_c++
r27209 jge  Channel returns after writing request when done is not NULL. User can laterly wait on Control::WaitForResponsee. User can submit multiple requests and wait for responses in batch (streaming RPC)
r27208 jge  Make the epoll warning log debug.
r27202 jge  Added str2endpoint and hostname2endpoint overloads
r27202 jge  Added experimental fast_realtime and fast_realtime_ns, don't use.
r27201 jge  Add a case for oneshot
r27200 jge  butex_wait returns -1(ETIMEDOUT) directly when *abstime is not earlier than 1 microsecond after current time.
r27198 jrj  Support command line arguments in echo_server/echo_client
r27198 jrj  Hulu protocol's header does not use network byte order
r27198 jrj  Fix a recursive lock problem in AcceptMessenger::CloseAllConnections
r27198 jrj  Add switch to RawPacker to turn on/off ntoh
r27196 jge  Socket::Revive suspends until #ref hits an expected value. not a perfect solution, just works in current system. However the health-checking mechanism is still broken.
r27196 jge  Rewrite Socket::Write() which behaves correctly when error occurs, it also much fairer(to calling threads) than previous impl. Write() does ConnectIfNot().
r27196 jge  Removed policy/no_compress.h(cpp), handle CompressTypeNone in ParseFromCompressedData and SerializeAsCompressedData directly.
r27196 jge  Put variables on stack in example/echo_example_c++/client.cpp
r27196 jge  Process fd events inside Socket by calling StartInputEvent
r27196 jge  Call done to Channel::CallMethod at ProcessXXXResponse.
r27196 jge  Added policy/register_all_extensions.h to register all globally available instances of extensions. They can't be declared in global scope directly because linker may not link. Channel and Server call RegisterAllExtensionsOrDie with pthread_once at start.
r27195 czy  Remove unused test file
r27194 czy  Add URI(uri.*) which is a simple uri parser
r27194 czy  Add http_rpc_message.* to parse and process http messeage
r27194 czy  Add HttpMessage as the structure of HttpRequest/Response
r27194 czy  Add HttpHeaderBuilder to build a valid http header for either request or response
r27192 jge  Added hostname2endpoint.
r27191 jge  Replaced bthread_oneshot_wait with bthread_oneshot_wait_and_destroy.
r27191 jge  bthread_oneshot_t* is not visible to users anymore.
r27191 jge  bthread_oneshot_destroy and bthread_oneshot_wait_and_destroy are thread-safe, only one of the callers recycles the internal oneshot object. Doubly destroy or wait returns EINVAL.
r27190 czy  User can pass a ZeroCopyOutputStream as ouput to ProtoMessageToJson, instead of std::string
r27190 czy  User can pass a ZeroCopyInputStream as input to JsonToProtoMessage, instead of std::string
r27190 czy  Add ZeroCopyStreamReader/Writer as wrappers to serialize/deserialize between rapidjson::Document and ZeroCopyStream
r27180 jrj  Remove debug code
r27180 jrj  Relevant changes to str2ip/EndPoint
r27180 jrj  Fix a bug of missing static initialization
r27176 jge  str2ip and hostname2ip return int instead of ip_t directly because all possible values of ip_t is valid.
r27176 jge  Replaced tcp_listen(EndPoint, int backlog) with tcp_listen(EndPoint, bool reuse_addr), the backlog takes SOMAXCONN as default.
r27176 jge  Removed EndPoint(const char* ip_str, int port2);
r27176 jge  Added str2endpoint.
r27175 jrj  Add example
r27174 jge  Not clear ResourcePool/ObjectPool when all threads called related functions quit on default, because the memory may still be used by threads never called related functions.
r27174 jge  bthread_oneshot_wait() returns set error_code correctly.
r27172 jrj  Rename compress_helper.* to compress.*. Separate policy and mechanism
r27172 jrj  Rename baidu_rpc_errno.* to errno.*
r27172 jrj  Minor fixes
r27172 jrj  Implement Server::Join
r27172 jrj  Add AcceptMessenger which inherits InputMessenger
r27171 jge  Call StopAndJoinGlobalDispatcher atexit
r27166 jge  Stop g_task_control atexit.
r27162 jge  Fixed return values in EpollThread::fd_wait and added more related cases.
r27161 jge  Use EmptyProcessHuluRequest in test_input_messenger.cpp
r27159 jge  Adaptive changes
r27154 jge  Added error_code as second parameter to bthread_oneshot_signal, not change error_code when the parameter is 0.
r27152 jrj  Implement some APIs in Server
r27152 jrj  Implement ProcessHuluRequest
r27152 jrj  Add compress_helper.* to parse/serialize compressed data
r27152 jrj  Add baidu_rpc_errno.*
r27152 jrj  Add baidu_pbrpc_meta.proto
r27152 jrj  Add additional parameter SocketId in InputMessengerHandler.Process
r27146 jge  Check in missed file in last CI
r27145 jge  Return EINVAL immediately when bthread_join on self.
r27140 jrj  Add tcp_listen in baidu/endpoint.h
r27132 jge  Use bthread_batch_start/signal in input_messenger, although the code is commented yet.
r27132 jge  Remove _lb_control from _nsthread_ptr in ~Channel.
r27132 jge  Move RoundRobin/Randomized LoadBalancer into namespace policy (to be consistent with the directory)
r27132 jge  Changed interfaces of NamingService so that event-driven naming service can be implemented efficiently.
r27132 jge  Added request_code to SelectServerReentrant. The field is from Controller::request_code() set by user.
r27132 jge  Added periodic_naming_service.h(cpp) to simplify impl. of previous style of naming services.
r27124 jge  Change comment
r27123 jge  Replaced bthread_startv() with bthread_batch_start() and bthread_batch_signal().
r27122 jge  Update test/Makefile
r27122 jge  Return EINVAL for invalid parameters to bthread_oneshot_* functions, bthread_oneshot_signal returns int again.
r27121 jge  Added ErrorCode in Controller. Added error_code as first parameter to SetFailed
r27120 jge  Eliminate repeated call to ByteSize() which is costly(relatively)
r27119 jge  Change comment
r27118 jge  Better support for hulu-pbrpc protocol.
r27117 jge  Move proto files inside protocol/ into protocol/baidu/rpc
r27117 jge  Implement ProcessHuluResponse in hulu_pbrpc_message.cpp roughly.
r27116 jge  Make bthread_oneshot_signal returns void
r27112 jge  Renamed CompressionMethod in rpc_option.proto to CompressType.
r27112 jge  Implement Channel::CallMethod roughly.
r27112 jge  Added serialize_request.h to provide the prototype callback to serialize protobuf message into IOBuf. Added (rough) SerializeHuluRequest in hulu_pbrpc_message.h(cpp)
r27112 jge  Added a couple of new fields into Controller.
r27111 jge  Added string_vprintf and string_vappendf.
r27110 jge  Serializations into IOBufAsZeroCopyOutputStream can be mixed with other changes to IOBuf.
r27109 jge  Fixed various issues in bthread_oneshot and refine comments, tests.
r27099 czy  Add http_parser which is a clone of https://github.com/joyent/http-parser
r27090 jge  Comment change
r27088 jge  TaskMeta::version_butex is constructed from TaskMeta::version_butex_memory instead of new.
r27088 jge  bthread_mutex_t::data is constructed from bthread_mutex_t::butex_memory instead of new.
r27088 jge  Added butex_construct, butex_destruct, butex_locate to create butex in-place. Removed pooled_butex_*.
r27088 jge  Added bthreadc_oneshot_* functions to synchronize caller with one-shot event.
r27065 czy  Fix a typo in logging
r27064 czy  Upate Makefile
r27063 czy  Add FileNamingService and BaiduNamingService
r27060 jge  Make delete_instance_map in extension_inl.h to be member static function of Extension<T> so that it can access private fields
r27058 jge  Update Makefile
r27057 jge  Use third-64/gtest instead of btest/gtest.
r27056 jge  Refine comments
r27054 jge  Comment change
r27053 jge  Remove addtional comma in BAIDU_RPC_REGISTER_XXX macros
r27052 jge  Minor changes
r27051 jge  Extension<T> initializes _instance_map correctly, added test/test_extension.cpp
r27051 jge  Added BAIDU_RPC_REGISTER_NAMING_SERVICE_GLOBALLY and BAIDU_RPC_REGISTER_LOAD_BALANCER_GLOBALLY
r27050 jge  Use third-64/gtest instead of btest/gtest
r27049 jge  Adaptive changes
r27048 jge  Minor change
r27047 jge  baidu_fatal/warning/... are upper cased.
r27047 jge  Adding new log level to streaming_log is more convenient.
r27046 jge  IOBuf can be parsed from or serialized to protobuf messsage via IOBufAsZeroCopyInputStream and IOBufAsZeroCopyOutputStream
r27035 jge  Use non-private futex functions when kernel does not support private futex.
r27024 jge  Fixed a out-of-range error in src/baidu/rcp/test/COMAKE
r27021 jge  Generate *.pb.cc *.pb.h into src/baidu/rpc rather than protocol/
r27020 jge  Update Makefile
r27019 jge  Socket creates a global instance of InputEventDispatcher on demand. on_edge_triggered_events is the parameter of Socket::Create() now. Added SocketUser to extend usage of Socket.
r27019 jge  RoundRobin and Randomized LoadBalancer checks duplication in their AddServer.
r27019 jge  Renamed rpc_channel/RpcChannel to channel/Channel, rpc_control/RpcControl to control/Control, rpc_server/RpcServer to server/Server.
r27019 jge  Removed health_checker.h(cpp). Health checking is integrated into Socket::StartHealthCheck(), which will connect remote_side() every `interval_s' seconds. When the connection is established, Revive() of the Socket will be called. Revive() cancels active health-checking as well. As a consequence, signature of LoadBalancer::SelectServerReentrant is changed.
r27019 jge  LoadBalancerControl is a subclass of HealthWatcher as well as NamingServiceWatcher, it could add/remove servers in batch.
r27019 jge  InputMessenger is not a subclass of InputEventDispatcher, and the parsing and processing callbacks are refactored to abstract protocols more properly as well as minimizing memory allocation.
r27019 jge  Channel::Init accepts naming_service_url and load_balancer_name instead. Using the registration macros provided in naming_service.h/load_balancer.h, Channel recognizes new NamingService and LoadBalancer directly. The mechanism is done by a global mapping from names to extended instances, which is implemented in extension.h
r27019 jge  Added socket_map.h to share out-going Sockets globally.
r27019 jge  Added NamingServiceThread to call NamingService::GetServers and share the thread between Channels with the same naming_service_url.
r26880 jge  Added a temporary function bthread_revive to emulate EINTR with ESTOP.
r26820 jrj  Add local_build.sh
r26800 jge  Split conditional CONFIGS from COMAKE into COMAKE.gcc4 due to limitation of comake
r26786 jge  Join subthreads (running ProcessEvent) in input_event_dispatchers.
r26786 jge  Implemented HealthChecker
r26786 jge  Fixed bugs in policy/round_robin_load_balancer.cpp, although it's still incorrect at using thread-local variables. Will be fixed in future CI.
r26786 jge  Added random_number_seed.h(cpp), naming_service_thread.h
r26777 jge  Added bthread_list_stop()
r26776 jge  Replaced pthread part in bthread_fd_*wait() with pthread_fd_wait()
r26776 jge  Removed bthread_wakeup_pthread from bthread.h
r26776 jge  Implemented bthread_connect()
r26776 jge  Implemented bthread_close()
r26776 jge  Fixed ABA issue in TaskGroup::usleep by locking TaskMeta::version_lock
r26775 jge  bthread_start* functions have similar signatures with pthread_create
r26775 jge  bthread_* functions returns errno on error rather than -1 with errno set.
r26775 jge  Added bthread_stopped()
r26774 jge  Removed state from TaskMeta, added a couple of fields for stopping bthreads.
r26774 jge  Removed schedule_repeated from TimerThread. Allocation of TaskId skips 0 and -1. Renamed try_unschedule to unschedule and changed its returning value.
r26774 jge  Implemented bthread_stop() which is composed with stop_butex_wait() in butex.cpp and stop_usleep() in task_group.cpp. When a bthread is told to stop, all function(in the thread) that would block will return -1 with errno=ESTOP.
r26774 jge  Enrich unit tests
r26774 jge  Combined SleepState and cancelled in ButexBthreadWait(butex.cpp) with WaiterState. Rewrite unsleep_if_necessary which is buggy previously.
r26774 jge  Added bthread_cond_timedwait in bthread_cond.cpp. Return the error code instead of setting errno(to be consistent with pthread) in several bthread functions.
r26773 jge  Tidy and comment task_control.h(cpp) and task_group.h(cpp)
r26772 jge  Make branch coverage of fd_guard to be 100%
r26771 jge  Added cases for fd_guard, errno and scoped_lock
r26770 jge  Invoke the weak bthread_connect() wrapper in tcp_connect.
r26769 jge  Added tcp_connect in endpoint.h
r26766 jge  Added unit-test for bthread_list_t
r26764 jge  Unified format of including guards.
r26764 jge  Added bthread_list* to track and join many bthreads
r26761 jge  Unify format of copyright
r26760 jge  Unify format of copyright
r26758 jge  undef same named macros in noncopyable.h
r26757 jge  Replaced "operator bool" with "operator const void*" in StringSplitter and StringMultiSplitter
r26754 jge  Refine comment of load_balancer.h
r26753 jge  Socket::Create() does not require fd, inner fd can be reset and released.
r26753 jge  InputMessenger::init() does not require listend_fd to be provided.
r26753 jge  Added policy/round_robin_load_balancer.h(cpp) policy/randomized_load_balancer.h(cpp)
r26753 jge  Added load_balancer.h, load_balancer_control.h(cpp) which are interfaces for load balancing.
r26751 jge  Renamed butex_create<T> to butex_create_checked<T>, butex_create_raw to butex_create
r26751 jge  Added pooled_butex_create, pooled_butex_destroy, pooled_butex_address to create buetex on ResourcePool
r26722 jge  Added delete_object in thread_local.h which is a common function to thread_atexit
r26718 jge  Change comment
r26717 jge  Add BAIDU_SYMBOLSTR in baidu/concat.h to convert symbol to string.
r26715 jge  Fixed typo
r26714 jge  Minor change
r26712 jge  Log streams can be used as std::ostream directly.
r26712 jge  Fixed break in baidu/check.h
r26711 jge  Renamed baidu_<level> to baidu_<level>_ne, baidu_<level>_ae to baidu_<level>, thus making auto-ending version default.
r26710 jge  baidu_debug and baidu_debug_ae have no effects when NDEBUG is defined
r26710 jge  Add auto ending version of streaming log, suffixed with _ae
r26709 jge  Minor change
r26708 jge  A couple of renames on FileWatcher
r26697 jge  Unify coding style of unit tests
r26696 jge  Minor changes
r26695 jge  Added file_watcher.h to watch change of files.
r26693 jge  Fix typo
r26689 jge  Minor changes
r26687 jge  Added string_splitter.h to split a string with one or multiple separators iteratively.
r26631 jge  Unify coding style according to http://styleguide.baidu.com/style/cpp/index.html
r26625 jge  Minor changes
r26624 jge  Added check.h to privide BAIDU_CHECK* and BAIDU_DCHECK*
r26624 jge  Added AutoEnd in streaming_log.h to flush LogStream automatically
r26618 jge  Added interfaces for LoadBalancer and HealthChecker
r26616 jge  Comment changes
r26615 jge  Check in missed test/test_socket.cpp
r26608 jge  Replaced Socket::Destroy with Socket::SetFailed. When a Socket is SetFailed(), following Address() shall return NULL and the Socket will be recycled when no one references it.
r26608 jge  IOBuf can be written into Socket using Write()
r26604 jge  Renamed IOBuf to IOPortal, IOPiece to IOBuf.
r26604 jge  append(const char*), append(const void*, size_t), append(const std::string&) are implemented as member functions of IOBuf, using thread local blocks.
r26603 jge  Add a quick path for count==1 in cut_multiple_into_file_descriptor
r26602 jge  Initialize comlog in the thread of TaskGroup
r26602 jge  Add a workaround to make bthread_fd_wait work correctly when BAIDU_KERNEL_FIXED_EPOLLONESHOT_BUG is not defined. Performance of ping_pong case in test_fd is half lower.
r26601 jge  The unmatched-expected-value branch in wait_for_butex is still buggy, don't jump back to original thread now.
r26601 jge  Removed last two parameters of butex_wait. Removed last_tid from TaskGroup.
r26601 jge  Implemented EpollThread::fd_wait without aid of last two parameters to previous butex_wait.
r26600 jge  Fix overloaded operators in endpoint.h
r26585 jrj  Minor fix
r26584 jrj  Minor fix
r26583 jrj  Ignore xml output of test_errno_c since it's not a gtest
r26581 jge  Update local_build.sh
r26580 jge  Add local_build.sh
r26579 jge  Fixed a race condition that destructing thread-local table shall be placed before changing *version_butex otherwise bthread joiners may not see side effects in the destructors.
r26576 jge  test_key may fail sometimes
r26576 jge  Depend on third-64/gtest instead of com/btest/gtest, and revert r26575 because (official) gtest does not use getenv/setenv
r26575 jge  Move asserts outside of threads
r26569 jge  Disable ping_pong UT in test_fd.cpp
r26569 jge  Add local_build.sh
r26565 jge  Added changelog.sh- Added changelog.sh- Added changelog.sh- Added changelog.sh- Added changelog.sh- Added changelog.sh- Added changelog.sh- Added changelog.sh- Added changelog.sh
r26552 jge  Support boost.context 1.56
r26548 jge  Copy boost and libboost_context.a into output if necessary
r26546 jge  Use newer boost for gcc4
r26546 jge  Adjust style
r26544 jge  Style adjust.
r26544 jge  Add __THROW to bthread functions except bthread_exit
r26542 jge  `parallel' -> `concurrent' in Header
r26541 jge  Minor change
r26540 jge  Make `pieces' to cut_multiple_into_file_descriptor const.
r26539 jge  Minor changes
r26537 jge  Remove local files in Makefile
r26536 jge  COMAKE changes
r26534 jge  Include thread_local.h instead of deleted thread_atexit.h
r26533 jge  Add missing includes.
r26532 jge  Merge thread_atexit.h(cpp) into thread_local.h(cpp), added thread_local_inl.h
r26532 jge  Added get_thread_local() to simplify getting thread-local objects
r26529 jge  Include thread_local.h in thread_atexit.h as default
r26528 jge  Removed test/test_unique_ptr.cpp which causes compilation error under g++4.8
r26528 jge  Added baidu/thread_local.h to provide compile-independent keyword thread_local
r26511 jge  Put inlined implementation into baidu/iobuf_inl.h
r26511 jge  IOPiece::pop_front returns popped bytes
r26511 jge  IOPiece::cut_into_file_descriptor does not retry writing internally.
r26511 jge  Added IOPiece::cut_multiple_into_file_descriptor to write multiple IOPiece into a file descriptor in one call.
r26498 jge  Minor change
r26497 jge  InputMessenger inherits InputEventDispatcher now. Handler are abstracted as user callbacks and Scissors&handler of hulu messages are moved into hulu_pbrpc_message.h(cpp)
r26497 jge  Changed InputEventDispatcher to make use of Socket.
r26497 jge  Added socket.h, socket_inl.h, socket.cpp to replace previous InputEventConsumer in input_event_dispatcher.h. Socket is the object associated with every file descriptor. User shall use Create()/Destroy()/Address() to access the object concurrently, check source code for details.
r26497 jge  Added raw_pack.h to parse message header
r26497 jge  Added has_epollrdhup.h(cpp) to support EPOLLRDHUP (when it's supported by kernel)
r26497 jge  Added baidu/rpc/config.h as pre-defined header
r26496 jge   Add a parameter `quit_when_read_less' to IOBuf::append_from_file_descriptor
r26494 jge  Unify copyright headers
r26494 jge  Overload opoerator<< on ResourcePoolInfo/ObjectPoolInfo as default. When BAIDU_RESOURCE_POOL_NEED_FREE_ITEM_NUM or BAIDU_OBJECT_POOL_NEED_FREE_ITEM_NUM is defined, free_item_num will be printed.
r26494 jge  Added baidu/bthread/config.h as pre-defined header
r26492 jge  Include <unistd.h> in fd_guard.h
r26428 jge  Fix a typo in baidu/noncopyable.h
r26418 jge  Assert size of unique_ptr
r26415 jge  Add a case of transfering ownership in test/test_unique_ptr.cpp
r26382 jge  Minor changes
r26381 jge  Depend on base of ullib rather than ullib_3-1-85-0_PD_BL
r26374 jge  (Partially) support C++11
r26372 jge  Added typeof.h provding BAIDU_TYPEOF which makes container_of correct under C++11
r26368 jge  Support gcc 4.8.2 and -std=c++11
r26368 jge  Added unique_ptr.h which is an emulated version before c++11
r26348 jge  Changed BTHREAD_SMALL_STACKSIZE from 8K to 16K due to stackoverflow casued by glibc
r26348 jge  A couple of renames of constants
r26344 jge  Added unix_socket.h which is convenient for network related unit-testing
r26343 jge  Include <stddef.h> in baidu/array_size.h
r26342 jge  Replaced _create_local() and related code with get_or_new_local_pool()
r26342 jge  _nlocal is thread-safe.
r26342 jge  Delete new_block in the end of add_block()
r26341 jge  Move allocation of new Block in add_block() forward to avoid possible corruption of BlockGroup::blocks
r26340 jge  Use BAIDU_SCOPED_LOCK in resource_pool_inl.h/object_pool_inl.h to fix missing-unlock bugs in add_block_group and possible other functions.
r26340 jge  Relaxed loads on _block_groups/BlockGroup::blocks to memory_order_consume.
r26327 jge  Minor changes
r26319 jge  Fixed a bug in ResourcePool/ObjectPool that global free list(_free) is not cleared after deleting all memory. The deleting memory iteration also has a typo bug which makes the first item being destructed for nitem times and other items are not destructed.
r26319 jge  bthread_start* functions return error when constructing TaskMeta failed.
r26319 jge  Add cancel_resource and cancel_object to release just-got resource/object in resource_pool.h and object_pool.h respectively.
r26310 jge  Add against-NULL check in str2ip
r26301 jge  Move bthread_log.h to baidu/bthread/log.h
r26297 jge  Move resource_pool.h and object_pool.h from baidu/bthread/ into baidu/
r26296 jge  Add object_pool.h which inherits from resource_pool.h, to allocate fixed-size objects without identifiers.
r26288 jge  Renamed bthread_start to bthread_start_urgent, bthread_start_short to bthread_start_urgent_short. Added bthread_start_background.
r26288 jge  Fixed a bug that padding[1] of bthread_mutex_parted is not initialized.
r26278 jge  Second parameter to IOBuf::append_from_file_descriptor is exact limit and not set by default.
r26275 jge  Move iobuf.h and iobuf.cpp into baidu/
r26274 jge  Check existence of file to AddOptFile in test/COMAKE
r26273 jge  Check existence of file to AddOptFile in test/COMAKE
r26272 jge  Remove __FUNCTION__ from streaming log, support bthread_self.
r26271 jge  Use cpuwide_time in baidu::Timer
r26271 jge  Added streaming_log.h(cpp)
r26270 jge  Add baidu/bthread/*.h as prefixes
r26265 jge  Minor changes
r26264 jge  Fix COMAKE
r26263 jge  Revert back to r26235 with minor changes
r26262 jge  Move source files not prefixed with 'bthread_" into baidu/bthread/
r26257 jge  Point to correct public/common headers
r26256 jge  Point to correct public/common headers
r26255 jge  Moved all files into baidu/ and removed baidu_ prefixes.
r26255 jge  Added scoped_file.h and fd_guard.h
r26251 jge  Renamed EventDispatcher to InputEventDispatcher, renamed InputEventConsumer::OnInputEvent to OnEdgeTriggeredEvent which adds InputEventDispatcher* as second parameter. InputEventDispatcher::AddConsumer does not make the fd non-blocking.
r26247 jge  Added event_dispatcher.h(cpp) to dispatch edge triggered events to consumers running in separate bthreads
r26244 jge  Fix typo
r26243 jge  Minor changes
r26237 jge  Fix some include guards
r26236 jge  _groups/_rqs in TaskControl do not point to atomic values.
r26236 jge  Changed including headers of public/common
r26236 jge  Added test_dispatcher.cpp to dispatch fd to parallel bthreads
r26235 jge  Remove dependence on boost by copying headers.
r26234 jge  Fixed a bug in gen_all.sh that baidu_public_common.h is not excluded
r26233 jge  Renamed public_common_all.h to baidu_public_common.h, libcommon.a to libbaidu_public_common.a, cacheline.h to baidu_cacheline.h
r26232 jge  Removed BAIDU_UNCHECKED_SCOPED_LOCK and unchecked_lock_guard. lock_guard is defined in namespace std now, and has different behavior in debugging mode
r26231 jge  Fixed incorrect include guards.
r26230 jge  Renamed scoped_lock.h to baidu_scoped_lock.h, SCOPED_LOCK to BAIDU_SCOPED_BLOCK, UNCHECKED_SCOPED_LOCK to BAIDU_UNCHECKED_SCOPED_LOCK, baidu::detail::ScopedLocker to baidu::lock_guard, baidu::detail::UncheckedScopedLocker to baidu::unchecked_lock_guard respectively. The utility supports C++11 mutexes as well.
r26216 jge  Minor renamings
r26215 jge  Added rpc_channel.h rpc_control.h rpc_server.h naming_service.h
r26215 jge  Added protocol/rpc_option.proto, renamed protocol/rpc_meta_ub.proto to protocol/rpc_meta_public_pbrpc.proto
r26204 jge  Make class_name thread-safe
r26203 jge  Refine comment in global_init.h
r26202 jge  Added baidu_endpoint.h which wraps IP and port.
r26200 jge  Move test/ into src/baidu/rpc
r26199 jge  Add more directories
r26198 jge  Added synchronous_event.h
r26197 jge  Removed dependency on com/btest/gtest in COMAKE
r26196 jge  Make sequence in TaskControl::wait_task more reasonable. Relaxed some atomic operations. Refine related comments.
r26195 jge  Added COMAKE and .proto
r26194 jge  Not depend on third-64/boost headers in test/COMAKE
r26193 jge  Clean up COMAKE and test/COMAKE
r26191 jge  Fix the incorrect file guard in task_control.h
r26191 jge  bthread_self() is always 0 for pthreads.
r26190 jge  Add "extern const" to exposed variables in baidu_time.cpp
r26188 jge  Minor change
r26187 jge  cpuwide_time returns monotonic_time if CPU does not support invariant tsc
r26186 jge  bthread_join on tid=0 returns -1 (EINVAL).
r26186 jge  Added bthread_uptime_us(), TaskStatistics::uptime is removed
r26183 jge  Replaced process_time with cpuwide_time
r26182 jge  Add missed baidu_time.cpp
r26181 jge  Use monotonic_time in Timer.
r26181 jge  Use int64_t instead of long in baidu_time.h
r26181 jge  Use CLOCK_MONOTONIC_RAW in monotonic_time if availabl.
r26181 jge  Replaced process_time with cpuwide_time(based on rdtsc) because clock_gettime of glibc has different semantics on CLOCK_PROCESS_CPUTIME_ID from sys_clock_gettime
r26138 jge  test/COMAKE does not define NDEBUG by default.
r26138 jge  Comment a seq_cst fence in WorkStealingQueue::steal which is probably wrong in principle.
r26138 jge  Combine frequent signal_task: let first woken-up waiter signals unsignalled tasks
r26138 jge  Changed signature of sched, sched_to,exchange, usleep, start in TaskGroup, to prevent (often) forgotten re-assignment of the returned TaskGroup.
r26135 jge  Replaced long_enough_us with EveryManyUS
r25946 jge  Changed BLOCK_SIZE from 128K to 4K
r25843 jge  Changed comments in temp_file.h
r25842 jge  Added class_name
r25750 jge  Added string_printf
r25737 jge  Add missed COMAKE
r25736 jge  Port iobuf from DP
r25722 jge  Added fd_utility.h/cpp
r25546 jge  Added TempFile to create temporary files for unit testing
r25498 jge  minor changes
r25496 jge  Update comment in butex.cpp
r25494 jge  TaskGroup::sched_to checks recursive invocation when NDEBUG is not defined.
r25494 jge  Renamed TimerThread::unschedule to try_unschedule
r25494 jge  Implemented butex_requeue, butex_wake_all, butex_wake_except
r25494 jge  Implemented bthread_fd_wait and bthread_fd_timedwait
r25494 jge  Found the bug that EPOLL_CTL_MOD+EPOLLONESHOT may miss events, verified and submitted patch(https://patchwork.kernel.org/patch/1970231/) to baidu kernel
r25494 jge  Fixed a bug in TaskControl::write_task that expected_task_version was not updated.
r25494 jge  bthread_exit works for pthread.
r25494 jge  Added bthread_startv to create multiple threads simultaneously.
r25276 jge  Replaced normalize_timespec with timespec_normalize
r25276 jge  Added test/test_baidu_time.cpp
r25266 jge  Tidy baidu_time.h
r25219 jge  Update COMAKE
r25218 jge  Renamed all.h to public_common_all.h
r25217 jge  Added test_cond.cpp
r25217 jge  A couple of renamings
r25168 jge  Implemented bthread_key_t related functions
r25168 jge  Implemented bthread_cond_t related functions
r25168 jge  Fixed a bug in TaskGroup that _last_context_remained is not initialized.
r25168 jge  Clean up bthread.cpp
r25125 jge  A couple of renamings in bthread_mutex.cpp
r25124 jge  Added normalize_timespec in baidu_time.h
r25123 jge  test/COMAKE links foo*.cpp|foo*.cc|foo*.c for tests depending on foo.h, which solves some linking issues of implicit dependencies(extern)
r25123 jge  TaskMeta is returned to correct ResourcePool variant.
r25123 jge  TaskGroup::usleep schedule TimerThread in remained (otherwise there's a race)
r25123 jge  sched(), sched2() save and restore errno correctly.
r25123 jge  Implemented bthread_mutex_t related functions in bthread_mutex.cpp
r25123 jge  Fixed issues of TaskControl::signal_task, wait_task and _steal_task
r25123 jge  event logging of specific bthread is settable in bthread_attr_t
r25123 jge  butex_wait accepts absolute timespec rather than relative one.
r25105 jge  Added global_init.h to run code before main() of C/C++
r25105 jge  Added baidu_concat.h and replace previous adhoc-defined macros with it.
r25088 jge  TimerThread::unschedule() returns value so that we know status of the task. Include baidu_time.h instead of self-defined utilities.
r25088 jge  TaskGroup::_do_what_last_contex_left() is replaced with more flexible TaskGroup::set_remains(), added TaskGroup::exchange(), a couple of renamings
r25088 jge  Separate wrappers of sys_futex from task_control.cpp into sys_futex.h
r25088 jge  Implemented butex which is a futex-like building block for synchronizing bthreads/pthreads. Some functions are not done yet.
r25088 jge  bthread_join() is implemented with butex (TaskMeta::version_butex)
r25088 jge  Added bthread_errno.h(.cpp) which is based on public/common/baidu_errno.h
r25082 jge  Error produced by BAIDU_CASSERT is cleaner.
r25042 jge  Added container_of.h
r25030 jge  Fixed a typo in baidu_errno.h
r25029 jge  Added gen_all.sh to generate all.h
r25029 jge  Added baidu_errno.h baidu_error.cpp test/test_baidu_errno.cpp test/test_baidu_errno_c.c to facilitate customized errno
r25026 jge  Fixed typo in timespec_add()
r25025 jge  Added timespec_from_now()
r25024 jge  Added timespec_to_*seconds(), *seconds_to_timespec(), timeval_to_*seconds, *seconds_to_timeval(), timespec_add()
r25008 jge  SCOPED_LOCK accepts reference of locks rather than pointer.
r25008 jge  Added UNCHECKED_SCOPED_LOCK in scoped_lock.h
r25005 jge  Added all.h to include all headers
r25002 jge  Replaced content of array_size.h with the one implemented in chrome.
r25002 jge  BAIDU_CASSERT redirects to static_assert on newer gcc.
r25002 jge  Added scoped_lock.h, noncopyable.h, ignore_result.h
r24942 jge  add comment
r24941 jge  fix typo
r24940 jge  Add and cleanup functions in baidu_time.h
r24872 jge  TimerThread::stop_and_join waits until internal thread finishes all schedule tasks, and schedule/schedule_repeated reject tasks after stop_and_join is called.
r24872 jge  Implemented fixed size work stealing queue.
r24872 jge  Fixed a serious race in ResourcePool<T>::add_block_group
r24872 jge  Extensively improved TaskControl and TaskGroup. Tasks can be stolen and moved around cores correctly.
r24872 jge  Added test/test_bthread.cpp
r24845 jge  Extensively improved ResourcePool. The class is able to create recycling-separated variants.
r24845 jge  Extensively elaborated TaskGroup, TaskControl and bthread.cpp.
r24844 jge  Reserve internal vector before usage in thread_atexit.cpp
r24839 jge  Reverse calling sequense of atexit functions, order between void (*fn)() an void (*fn)(void*) is respected as well.
r24839 jge  Added thread_atexit_cancel in thread_atexit.h to remove registered functions.
r24838 jge  Add comments in array_size.h
r24837 jge  Add array_size.h which provides ARRAY_SIZE
r24832 jge  Added thread_atexit.h which registers a function being called at caller's exit.
r24832 jge  Added compile_time_assert.h to assert boolean expressions at compile-time
r24754 jge  Depend on public/common and public/flatmap
r24753 jge  Added likely.h, cacheline.h, baidu_time.h
r24749 jge  Partially initialize.
r24569 jge  Added first revisions of murmurhash.h(cpp) and iterative_murmurhash3.h(cpp). Besides moving useful fmix32 and fmix64 into header, murmurhash3.h(cpp) is basically same with original version(https://code.google.com/p/smhasher/source/browse/trunk/MurmurHash3.h). iterative_murmurhash3.h(cpp) adds MD5 alike interfaces to compute murmurhash3 iteratively.
```
