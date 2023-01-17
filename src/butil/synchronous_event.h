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

// Date: Sun Nov 7 21:43:34 CST 2010

#ifndef BUTIL_SYNCHRONOUS_EVENT_H
#define BUTIL_SYNCHRONOUS_EVENT_H

#include <vector>                             // std::vector
#include <algorithm>                          // std::find
#include <errno.h>                            // errno
#include "butil/logging.h"

// Synchronous event notification.
// Observers to an event will be called immediately in the same context where
// the event is notified. This utility uses a vector to store all observers
// thus is only suitable for a relatively small amount of observers.
//
// Example:
//    // Declare event type
//    typedef SynchronousEvent<int, const Foo*> FooEvent;
//
//    // Implement observer type
//    class FooObserver : public FooEvent::Observer {
//        void on_event(int, const Foo*) {
//            ... handle the event ...
//        }
//    };
//
//    FooEvent foo_event;                 // An instance of the event
//    FooObserver foo_observer;           // An instance of the observer
//    foo_event.subscribe(&foo_observer); // register the observer to the event
//    foo_event.notify(1, NULL);          // foo_observer.on_event(1, NULL) is
//                                        // called *immediately*

namespace butil {

namespace detail {
// NOTE: This is internal class. Inherit SynchronousEvent<..>::Observer instead.
template <typename _A1, typename _A2, typename _A3> class EventObserver;
}

// All methods are *NOT* thread-safe.
// You can copy a SynchronousEvent.
template <typename _A1 = void, typename _A2 = void, typename _A3 = void>
class SynchronousEvent {
public:
    typedef detail::EventObserver<_A1, _A2, _A3> Observer;
    typedef std::vector<Observer*> ObserverSet;
    
    SynchronousEvent() : _n(0) {}

    // Add an observer, callable inside on_event() and added observers
    // will be called with the same event in the same run.
    // Returns 0 when successful, -1 when the obsever is NULL or already added. 
    int subscribe(Observer* ob) {
        if (NULL == ob) {
            LOG(ERROR) << "Observer is NULL";
            return -1;
        }
        if (std::find(_obs.begin(), _obs.end(), ob) != _obs.end()) {
            return -1;
        }
        _obs.push_back(ob);
        ++_n;
        return 0;
    }

    // Remove an observer, callable inside on_event().
    // Users are responsible for removing observers before destroying them.
    // Returns 0 when successful, -1 when the observer is NULL or already removed.
    int unsubscribe(Observer* ob) {
        if (NULL == ob) {
            LOG(ERROR) << "Observer is NULL";
            return -1;
        }
        typename ObserverSet::iterator
            it = std::find(_obs.begin(), _obs.end(), ob);
        if (it == _obs.end()) {
            return -1;
        }
        *it = NULL;
        --_n;
        return 0;
    }

    // Remove all observers, callable inside on_event()
    void clear() {
        for (typename ObserverSet::iterator
                 it = _obs.begin(); it != _obs.end(); ++it) {
            *it = NULL;
        }
        _n = 0;
    }

    // Get number of observers
    size_t size() const { return _n; }

    // No observers or not
    bool empty() const { return size() == 0UL; }

    // Notify observers without parameter, errno will not be changed
    void notify() {
        const int saved_errno = errno;
        for (size_t i = 0; i < _obs.size(); ++i) {
            if (_obs[i]) {
                _obs[i]->on_event();
            }
        }
        _shrink();
        errno = saved_errno;
    }

    // Notify observers with 1 parameter, errno will not be changed
    template <typename _B1> void notify(const _B1& b1) {
        const int saved_errno = errno;
        for (size_t i = 0; i < _obs.size(); ++i) {
            if (_obs[i]) {
                _obs[i]->on_event(b1);
            }
        }
        _shrink();
        errno = saved_errno;
    }

    // Notify observers with 2 parameters, errno will not be changed
    template <typename _B1, typename _B2>
    void notify(const _B1& b1, const _B2& b2) {
        const int saved_errno = errno;
        for (size_t i = 0; i < _obs.size(); ++i) {
            if (_obs[i]) {
                _obs[i]->on_event(b1, b2);
            }
        }
        _shrink();
        errno = saved_errno;
    }

    // Notify observers with 3 parameters, errno will not be changed
    template <typename _B1, typename _B2, typename _B3>
    void notify(const _B1& b1, const _B2& b2, const _B3& b3) {
        const int saved_errno = errno;
        for (size_t i = 0; i < _obs.size(); ++i) {
            if (_obs[i]) {
                _obs[i]->on_event(b1, b2, b3);
            }
        }
        _shrink();
        errno = saved_errno;
    }
        
private:
    void _shrink() {
        if (_n == _obs.size()) {
            return;
        }
        for (typename ObserverSet::iterator
                 it1 = _obs.begin(),
                 it2 = _obs.begin(); it2 != _obs.end(); ++it2) {
            if (*it2) {
                *it1++ = *it2;
            }
        }
        _obs.resize(_n);
    }
    
    size_t _n;
    ObserverSet _obs;
};

namespace detail {

// Add const reference for types which is larger than sizeof(void*). This
// is reasonable in most cases and making signature of SynchronousEvent<...>
// cleaner.
template <typename T> struct _AddConstRef { typedef const T& type; };
template <typename T> struct _AddConstRef<T&> { typedef T& type; };

// We have to re-invent some wheels to avoid dependence on <boost/mpl/if.hpp>
template <bool cond, typename T1, typename T2>
struct if_c { typedef T1 type; };

template <typename T1, typename T2>
struct if_c<false, T1, T2> { typedef T2 type; };

template <typename T>
struct AddConstRef : public if_c<(sizeof(T)<=sizeof(void*)),
    T, typename _AddConstRef<T>::type> {};

template <> class EventObserver<void, void, void> {
public:
    virtual ~EventObserver() {}
    virtual void on_event() = 0;
};

template <typename _A1> class EventObserver<_A1, void, void> {
public:
    virtual ~EventObserver() {}
    virtual void on_event(typename AddConstRef<_A1>::type) = 0;
};

template <typename _A1, typename _A2> class EventObserver<_A1, _A2, void> {
public:
    virtual ~EventObserver() {}
    virtual void on_event(typename AddConstRef<_A1>::type,
                          typename AddConstRef<_A2>::type) = 0;
};

template <typename _A1, typename _A2, typename _A3> class EventObserver {
public:
    virtual ~EventObserver() {}
    virtual void on_event(typename AddConstRef<_A1>::type,
                          typename AddConstRef<_A2>::type,
                          typename AddConstRef<_A3>::type) = 0;
};
}  // end namespace detail
}  // end namespace butil

#endif  // BUTIL_SYNCHRONOUS_EVENT_H
