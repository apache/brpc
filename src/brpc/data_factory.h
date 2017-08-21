// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Oct 13 18:44:28 CST 2015

#ifndef BRPC_DATA_FACTORY_H
#define BRPC_DATA_FACTORY_H

// To baidu-rpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

namespace brpc {

class DataFactory {
public:
    virtual ~DataFactory() {}

    // Implement this method to create a piece of data.
    // Notice that this method is const.
    // Returns the data, NULL on error.
    virtual void* CreateData() const = 0;

    // Implement this method to destroy a piece of data that was created
    // by Create().
    // Notice that this method is const.
    virtual void DestroyData(void*) const = 0;
};

} // namespace brpc

#endif  // BRPC_DATA_FACTORY_H
