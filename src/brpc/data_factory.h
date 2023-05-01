// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.


#ifndef BRPC_DATA_FACTORY_H
#define BRPC_DATA_FACTORY_H

// To brpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

namespace brpc {

// ---- thread safety ----
// Method implementations of this interface should be thread-safe
class DataFactory {
public:
    virtual ~DataFactory() {}

    // Implement this method to create a piece of data
    // Returns the data, NULL on error.
    virtual void* CreateData() const = 0;

    // Implement this method to destroy data created by Create().
    virtual void DestroyData(void*) const = 0;

    // Overwrite this method to reset the data before reuse. Nothing done by default.
    // Returns
    //   true:  the data can be kept for future reuse
    //   false: the data is improper to be reused and should be sent to 
    //          DestroyData() immediately after calling this method
    virtual bool ResetData(void*) const { return true; }
};

} // namespace brpc

#endif  // BRPC_DATA_FACTORY_H
