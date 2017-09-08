// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_EXTENSION_INL_H
#define BRPC_EXTENSION_INL_H


namespace brpc {

template <typename T>
Extension<T>* Extension<T>::instance() {
    // NOTE: We don't delete extensions because in principle they can be
    // accessed during exiting, e.g. create a channel to send rpc at exit.
    return butil::get_leaky_singleton<Extension<T> >();
}

template <typename T>
Extension<T>::Extension() {
    _instance_map.init(29);
}

template <typename T>
Extension<T>::~Extension() {
}

template <typename T>
int Extension<T>::Register(const std::string& name, T* instance) {
    if (NULL == instance) {
        LOG(ERROR) << "instance to \"" << name << "\" is NULL";
        return -1;
    }
    BAIDU_SCOPED_LOCK(_map_mutex);
    if (_instance_map.seek(name) != NULL) {
        LOG(ERROR) << "\"" << name << "\" was registered";
        return -1;
    }
    _instance_map[name] = instance;
    return 0;
}

template <typename T>
int Extension<T>::RegisterOrDie(const std::string& name, T* instance) {
    if (Register(name, instance) == 0) {
        return 0;
    }
    exit(1);
}

template <typename T>
T* Extension<T>::Find(const char* name) {
    if (NULL == name) {
        return NULL;
    }
    BAIDU_SCOPED_LOCK(_map_mutex);
    T** p = _instance_map.seek(name);
    if (p) {
        return *p;
    }
    return NULL;
}

template <typename T>
void Extension<T>::List(std::ostream& os, char separator) {
    BAIDU_SCOPED_LOCK(_map_mutex);
    for (typename butil::CaseIgnoredFlatMap<T*>::iterator
             it = _instance_map.begin(); it != _instance_map.end(); ++it) {
        // private extensions which is not intended to be seen by users starts
        // with underscore.
        if (it->first.data()[0] != '_') {
            if (it != _instance_map.begin()) {
                os << separator;
            }
            os << it->first;
        }
    }
}

} // namespace brpc


#endif  // BRPC_EXTENSION_INL_H
