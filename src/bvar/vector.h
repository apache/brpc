// Copyright (c) 2015 Baidu, Inc.
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

// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Sep 20 12:25:11 CST 2015

#ifndef  BVAR_VECTOR_H
#define  BVAR_VECTOR_H

#include <ostream>
#include <gflags/gflags_declare.h>

namespace bvar {

DECLARE_bool(quote_vector);

// Data inside a Vector will be plotted in a same graph.
template <typename T, size_t N>
class Vector {
public:
    static const size_t WIDTH = N;

    Vector() {
        for (size_t i = 0; i < N; ++i) {
            _data[i] = T();
        }
    }
    
    Vector(const T& initial_value) {
        for (size_t i = 0; i < N; ++i) {
            _data[i] = initial_value;
        }
    }
    
    void operator+=(const Vector& rhs) {
        for (size_t i = 0; i < N; ++i) {
            _data[i] += rhs._data[i];
        }
    }
    
    void operator-=(const Vector& rhs) {
        for (size_t i = 0; i < N; ++i) {
            _data[i] -= rhs._data[i];
        }
    }

    template <typename S>
    void operator*=(const S& scalar) {
        for (size_t i = 0; i < N; ++i) {
            _data[i] *= scalar;
        }
    }

    template <typename S>
    void operator/=(const S& scalar) {
        for (size_t i = 0; i < N; ++i) {
            _data[i] /= scalar;
        }
    }

    bool operator==(const Vector& rhs) const {
        for (size_t i = 0; i < N; ++i) {
            if (!(_data[i] == rhs._data[i])) {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const Vector& rhs) const {
        return !operator==(rhs);
    }
    
    T& operator[](int index) { return _data[index]; }
    const T& operator[](int index) const { return _data[index]; }

private:
    T _data[N];
};

template <typename T, size_t N>
std::ostream& operator<<(std::ostream& os, const Vector<T, N>& vec) {
    if (FLAGS_quote_vector) {
        os << '"';
    }
    os << '[';
    if (N != 0) {
        os << vec[0];
        for (size_t i = 1; i < N; ++i) {
            os << ',' << vec[i];
        }
    }
    os << ']';
    if (FLAGS_quote_vector) {
        os << '"';
    }
    return os;
}

template <typename T>
struct is_vector : public butil::false_type {};

template <typename T, size_t N>
struct is_vector<Vector<T,N> > : public butil::true_type {};

}  // namespace bvar

#endif  //BVAR_VECTOR_H
