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

// Date 2014/09/25 17:50:21

#ifndef  BVAR_RECORDER_H
#define  BVAR_RECORDER_H

#include <stdint.h>                              // int64_t uint64_t
#include "butil/macros.h"                         // BAIDU_CASSERT
#include "butil/logging.h"                        // LOG
#include "bvar/detail/combiner.h"                // detail::AgentCombiner
#include "bvar/variable.h"
#include "bvar/window.h"
#include "bvar/detail/sampler.h"

namespace bvar {

struct Stat {
    Stat() : sum(0), num(0) {}
    Stat(int64_t sum2, int64_t num2) : sum(sum2), num(num2) {}
    int64_t sum;
    int64_t num;
        
    int64_t get_average_int() const {
        //num can be changed by sampling thread, use tmp_num
        int64_t tmp_num = num;
        if (tmp_num == 0) {
            return 0;
        }
        return sum / (int64_t)tmp_num;
    }
    double get_average_double() const {
        int64_t tmp_num = num;
        if (tmp_num == 0) {
            return 0.0;
        }
        return (double)sum / (double)tmp_num;
    }
    Stat operator-(const Stat& rhs) const {
        return Stat(sum - rhs.sum, num - rhs.num);
    }
    void operator-=(const Stat& rhs) {
        sum -= rhs.sum;
        num -= rhs.num;
    }
    Stat operator+(const Stat& rhs) const {
        return Stat(sum + rhs.sum, num + rhs.num);
    }
    void operator+=(const Stat& rhs) {
        sum += rhs.sum;
        num += rhs.num;
    }
};

inline std::ostream& operator<<(std::ostream& os, const Stat& s) {
    const int64_t v = s.get_average_int();
    if (v != 0) {
        return os << v;
    } else {
        return os << s.get_average_double();
    }
}

// For calculating average of numbers.
// Example:
//   IntRecorder latency;
//   latency << 1 << 3 << 5;
//   CHECK_EQ(3, latency.average());
class IntRecorder : public Variable {
public:
    // Compressing format:
    // | 20 bits (unsigned) | sign bit | 43 bits |
    //       num                   sum
    const static size_t SUM_BIT_WIDTH=44;
    const static uint64_t MAX_SUM_PER_THREAD = (1ul << SUM_BIT_WIDTH) - 1;
    const static uint64_t MAX_NUM_PER_THREAD = (1ul << (64ul - SUM_BIT_WIDTH)) - 1;
    BAIDU_CASSERT(SUM_BIT_WIDTH > 32 && SUM_BIT_WIDTH < 64, 
                  SUM_BIT_WIDTH_must_be_between_33_and_63);

    struct AddStat {
        void operator()(Stat& s1, const Stat& s2) const { s1 += s2; }
    };
    struct MinusStat {
        void operator()(Stat& s1, const Stat& s2) const { s1 -= s2; }
    };    

    typedef Stat value_type;
    typedef detail::ReducerSampler<IntRecorder, Stat,
                                   AddStat, MinusStat> sampler_type;

    typedef Stat SampleSet;
    
    struct AddToStat {
        void operator()(Stat& lhs, uint64_t rhs) const {
            lhs.sum += _extend_sign_bit(_get_sum(rhs));
            lhs.num += _get_num(rhs);
        }
    };
    
    typedef detail::AgentCombiner<Stat, uint64_t, AddToStat> combiner_type;
    typedef combiner_type::Agent agent_type;

    IntRecorder() : _sampler(NULL) {}

    explicit IntRecorder(const butil::StringPiece& name) : _sampler(NULL) {
        expose(name);
    }

    IntRecorder(const butil::StringPiece& prefix, const butil::StringPiece& name)
        : _sampler(NULL) {
        expose_as(prefix, name);
    }

    ~IntRecorder() {
        hide();
        if (_sampler) {
            _sampler->destroy();
            _sampler = NULL;
        }
    }

    // Note: The input type is acutally int. Use int64_t to check overflow.
    IntRecorder& operator<<(int64_t/*note*/ sample);

    int64_t average() const {
        return _combiner.combine_agents().get_average_int();
    }

    double average(double) const {
        return _combiner.combine_agents().get_average_double();
    }

    Stat get_value() const {
        return _combiner.combine_agents();
    }
    
    Stat reset() {
        return _combiner.reset_all_agents();
    }

    AddStat op() const { return AddStat(); }
    MinusStat inv_op() const { return MinusStat(); }
    
    void describe(std::ostream& os, bool /*quote_string*/) const override {
        os << get_value();
    }

    bool valid() const { return _combiner.valid(); }
    
    sampler_type* get_sampler() {
        if (NULL == _sampler) {
            _sampler = new sampler_type(this);
            _sampler->schedule();
        }
        return _sampler;
    }

    // This name is useful for printing overflow log in operator<< since
    // IntRecorder is often used as the source of data and not exposed.
    void set_debug_name(const butil::StringPiece& name) {
        _debug_name.assign(name.data(), name.size());
    }
    
private:
    // TODO: The following numeric functions should be independent utils
    static uint64_t _get_sum(const uint64_t n) {
        return (n & MAX_SUM_PER_THREAD);
    }

    static uint64_t _get_num(const uint64_t n) {
        return n >> SUM_BIT_WIDTH;
    }

    // Fill all the first (64 - SUM_BIT_WIDTH + 1) bits with 1 if the sign bit is 1 
    // to represent a complete 64-bit negative number
    // Check out http://en.wikipedia.org/wiki/Signed_number_representations if
    // you are confused
    static int64_t _extend_sign_bit(const uint64_t sum) {
        return (((1ul << (64ul - SUM_BIT_WIDTH + 1)) - 1) 
               * ((1ul << (SUM_BIT_WIDTH - 1) & sum)))
               | (int64_t)sum;
    }

    // Convert complement into a |SUM_BIT_WIDTH|-bit unsigned integer
    static uint64_t _get_complement(int64_t n) {
        return n & (MAX_SUM_PER_THREAD);
    }

    static uint64_t _compress(const uint64_t num, const uint64_t sum) {
        return (num << SUM_BIT_WIDTH) 
               // There is a redundant '1' in the front of sum which was
               // combined with two negative number, so truncation has to be 
               // done here
               | (sum & MAX_SUM_PER_THREAD)
               ;
    }

    // Check whether the sum of the two integer overflows the range of signed
    // integer with the width of SUM_BIT_WIDTH, which is 
    // [-2^(SUM_BIT_WIDTH -1), 2^(SUM_BIT_WIDTH -1) - 1) (eg. [-128, 127) for
    // signed 8-bit integer)
    static bool _will_overflow(const int64_t lhs, const int rhs) {
        return 
            // Both integers are positive and the sum is larger than the largest
            // number
            ((lhs > 0) && (rhs > 0) 
                && (lhs + rhs > ((int64_t)MAX_SUM_PER_THREAD >> 1)))
            // Or both integers are negative and the sum is less than the lowest
            // number
            || ((lhs < 0) && (rhs < 0) 
                    && (lhs + rhs < (-((int64_t)MAX_SUM_PER_THREAD >> 1)) - 1))
            // otherwise the sum cannot overflow iff lhs does not overflow
            // because |sum| < |lhs| 
            ;
    }

private:
    combiner_type           _combiner;
    sampler_type*           _sampler;
    std::string             _debug_name;
};

inline IntRecorder& IntRecorder::operator<<(int64_t sample) {
    if (BAIDU_UNLIKELY((int64_t)(int)sample != sample)) {
        const char* reason = NULL;
        if (sample > std::numeric_limits<int>::max()) {
            reason = "overflows";
            sample = std::numeric_limits<int>::max();
        } else {
            reason = "underflows";
            sample = std::numeric_limits<int>::min();
        }
        // Truncate to be max or min of int. We're using 44 bits to store the
        // sum thus following aggregations are not likely to be over/underflow.
        if (!name().empty()) {
            LOG(WARNING) << "Input=" << sample << " to `" << name()
                       << "\' " << reason;
        } else if (!_debug_name.empty()) {
            LOG(WARNING) << "Input=" << sample << " to `" << _debug_name
                       << "\' " << reason;
        } else {
            LOG(WARNING) << "Input=" << sample << " to IntRecorder("
                       << (void*)this << ") " << reason;
        }
    }
    agent_type* agent = _combiner.get_or_create_tls_agent();
    if (BAIDU_UNLIKELY(!agent)) {
        LOG(FATAL) << "Fail to create agent";
        return *this;
    }
    uint64_t n;
    agent->element.load(&n);
    const uint64_t complement = _get_complement(sample);
    uint64_t num;
    uint64_t sum;
    do {
        num = _get_num(n);
        sum = _get_sum(n);
        if (BAIDU_UNLIKELY((num + 1 > MAX_NUM_PER_THREAD) ||
                           _will_overflow(_extend_sign_bit(sum), sample))) {
            // Although agent->element might have been cleared at this 
            // point, it is just OK because the very value is 0 in
            // this case
            agent->combiner->commit_and_clear(agent);
            sum = 0;
            num = 0;
            n = 0;
        }
    } while (!agent->element.compare_exchange_weak(
                 n, _compress(num + 1, sum + complement)));
    return *this;
}

}  // namespace bvar

#endif  //BVAR_RECORDER_H
