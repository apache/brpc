// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Oct 14 15:16:29 CST 2014

#ifndef BRPC_EXTENSION_INL_H
#define BRPC_EXTENSION_INL_H


namespace brpc {

template <typename T>
Extension<T>* Extension<T>::instance() {
    // NOTE: We don't delete extensions because in principle they can be
    // accessed during exiting, e.g. create a channel to send rpc at exit.
    return base::get_leaky_singleton<Extension<T> >();
}

template <typename T>
Extension<T>::Extension() {
    pthread_mutex_init(&_map_mutex, NULL);
    _instance_map.init(29);
}

template <typename T>
Extension<T>::~Extension() {
    pthread_mutex_destroy(&_map_mutex);
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
    for (typename base::CaseIgnoredFlatMap<T*>::iterator
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
