/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#pragma once

#include <atomic>
#include <vector>

namespace eloq {
template <typename T> class SpscQueue {
public:
  SpscQueue(size_t init_size) : data_(init_size) {}

  bool TryEnqueue(const T &element) {
    size_t tail_idx = tail_idx_.load(std::memory_order_relaxed);
    size_t next_tail_idx = AdvanceIdx(tail_idx);

    if (next_tail_idx == head_cache_) {
      head_cache_ = head_idx_.load(std::memory_order_acquire);
      if (next_tail_idx == head_cache_) {
        return false;
      }
    }

    data_[tail_idx] = element;
    tail_idx_.store(next_tail_idx, std::memory_order_release);
    return true;
  }

  bool TryEnqueue(T &&element) {
    size_t tail_idx = tail_idx_.load(std::memory_order_relaxed);
    size_t next_tail_idx = AdvanceIdx(tail_idx);

    if (next_tail_idx == head_cache_) {
      head_cache_ = head_idx_.load(std::memory_order_acquire);
      if (next_tail_idx == head_cache_) {
        return false;
      }
    }

    data_[tail_idx] = std::move(element);
    tail_idx_.store(next_tail_idx, std::memory_order_release);
    return true;
  }

  template <typename It> size_t TryDequeueBulk(It itemFirst, size_t max) {
    const size_t head_idx = head_idx_.load(std::memory_order_relaxed);
    const size_t tail_idx = tail_idx_.load(std::memory_order_acquire);

    if (head_idx == tail_idx) {
      return 0;
    }

    size_t total = 0;
    size_t new_head;
    if (head_idx < tail_idx) {
      // Copies elements in [head_idx, tail_idx)
      total = std::min(tail_idx - head_idx, max);
      std::move(data_.begin() + head_idx, data_.begin() + head_idx + total,
                itemFirst);
      new_head = head_idx + total;
    } else {
      // Copies elements in [head_idx, data_.size())
      size_t tail_seg_len = data_.size() - head_idx;
      size_t cnt_1 = std::min(tail_seg_len, max);
      std::move(data_.begin() + head_idx, data_.begin() + head_idx + cnt_1,
                itemFirst);
      itemFirst += cnt_1;
      total = cnt_1;

      if (max > tail_seg_len) {
        // Copies elements in [0, tail)
        size_t head_seg_len = tail_idx;
        size_t cnt_2 = std::min(head_seg_len, max - tail_seg_len);
        std::move(data_.begin(), data_.begin() + cnt_2, itemFirst);

        total += cnt_2;
        new_head = cnt_2;
      } else {
        new_head = head_idx + cnt_1;
        if (new_head == data_.size()) {
          new_head = 0;
        }
      }
    }

    head_idx_.store(new_head, std::memory_order_release);
    return total;
  }

private:
  size_t AdvanceIdx(size_t idx) {
    size_t next_idx = idx + 1;
    if (next_idx == data_.size()) {
      next_idx = 0;
    }
    return next_idx;
  }

  std::vector<T> data_{};
  alignas(64) std::atomic<size_t> head_idx_{0};
  alignas(64) std::atomic<size_t> tail_idx_{0};
  alignas(64) size_t head_cache_{0};
};
} // namespace eloq