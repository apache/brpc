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

#include "brpc/backup_request_policy.h"

#include "butil/logging.h"
#include "bvar/reducer.h"
#include "bvar/window.h"
#include "butil/atomicops.h"
#include "butil/time.h"

namespace brpc {

// Standalone statistics module for tracking backup/total request ratio
// within a sliding time window. Each instance schedules two bvar::Window
// sampler tasks; keep this in mind for high channel-count deployments.
class BackupRateLimiter {
public:
    BackupRateLimiter(double max_backup_ratio,
                      int window_size_seconds,
                      int update_interval_seconds)
        : _max_backup_ratio(max_backup_ratio)
        , _update_interval_us(update_interval_seconds * 1000000LL)
        , _total_count()
        , _backup_count()
        , _total_window(&_total_count, window_size_seconds)
        , _backup_window(&_backup_count, window_size_seconds)
        , _cached_ratio(0.0)
        , _last_update_us(0) {
    }

    // All atomic operations use relaxed ordering intentionally.
    // This is best-effort rate limiting: a slightly stale ratio is
    // acceptable for approximate throttling. Within a single update interval,
    // the cached ratio is not updated, so bursts up to update_interval_seconds
    // in duration can exceed the configured max_backup_ratio transiently.
    bool ShouldAllow() const {
        const int64_t now_us = butil::cpuwide_time_us();
        int64_t last_us = _last_update_us.load(butil::memory_order_relaxed);
        double ratio = _cached_ratio.load(butil::memory_order_relaxed);

        if (now_us - last_us >= _update_interval_us) {
            if (_last_update_us.compare_exchange_strong(
                    last_us, now_us, butil::memory_order_relaxed)) {
                int64_t total = _total_window.get_value();
                int64_t backup = _backup_window.get_value();
                // Fall back to cumulative counts when the window has no
                // sampled data yet (cold-start within the first few seconds).
                if (total <= 0) {
                    total = _total_count.get_value();
                    backup = _backup_count.get_value();
                }
                if (total > 0) {
                    ratio = static_cast<double>(backup) / total;
                } else if (backup > 0) {
                    // Backups issued but no completions in window yet (latency spike).
                    // Be conservative to prevent backup storms.
                    ratio = 1.0;
                } else {
                    // True cold-start: no traffic yet. Allow freely.
                    ratio = 0.0;
                }
                _cached_ratio.store(ratio, butil::memory_order_relaxed);
            }
        }

        bool allow = ratio < _max_backup_ratio;
        if (allow) {
            // Count backup decisions immediately for faster feedback
            // during latency spikes (before RPCs complete).
            _backup_count << 1;
        }
        return allow;
    }

    void OnRPCEnd(const Controller* /*controller*/) {
        // Count all completed RPC legs (both original and backup RPCs).
        // Backup decisions are counted in ShouldAllow() at decision time for
        // faster feedback. As a result, the effective suppression threshold is
        // (backup_count / total_legs), where total_legs includes both original
        // and backup completions.
        _total_count << 1;
    }

private:
    double  _max_backup_ratio;
    int64_t _update_interval_us;

    bvar::Adder<int64_t>            _total_count;
    mutable bvar::Adder<int64_t>    _backup_count;
    bvar::Window<bvar::Adder<int64_t>> _total_window;
    bvar::Window<bvar::Adder<int64_t>> _backup_window;

    mutable butil::atomic<double>  _cached_ratio;
    mutable butil::atomic<int64_t> _last_update_us;
};

// Internal BackupRequestPolicy that composes a BackupRateLimiter
// for ratio-based suppression.
class RateLimitedBackupPolicy : public BackupRequestPolicy {
public:
    RateLimitedBackupPolicy(int32_t backup_request_ms,
                            double max_backup_ratio,
                            int window_size_seconds,
                            int update_interval_seconds)
        : _backup_request_ms(backup_request_ms)
        , _rate_limiter(max_backup_ratio, window_size_seconds,
                        update_interval_seconds) {
    }

    int32_t GetBackupRequestMs(const Controller* /*controller*/) const override {
        return _backup_request_ms;
    }

    bool DoBackup(const Controller* /*controller*/) const override {
        return _rate_limiter.ShouldAllow();
    }

    void OnRPCEnd(const Controller* controller) override {
        _rate_limiter.OnRPCEnd(controller);
    }

private:
    int32_t _backup_request_ms;
    BackupRateLimiter _rate_limiter;
};

BackupRequestPolicy* CreateRateLimitedBackupPolicy(
    const RateLimitedBackupPolicyOptions& options) {
    if (options.backup_request_ms < -1) {
        LOG(ERROR) << "Invalid backup_request_ms=" << options.backup_request_ms
                   << ", must be >= -1 (-1 means inherit from ChannelOptions)";
        return NULL;
    }
    if (options.max_backup_ratio <= 0 || options.max_backup_ratio > 1.0) {
        LOG(ERROR) << "Invalid max_backup_ratio=" << options.max_backup_ratio
                   << ", must be in (0, 1]";
        return NULL;
    }
    if (options.window_size_seconds < 1 || options.window_size_seconds > 3600) {
        LOG(ERROR) << "Invalid window_size_seconds=" << options.window_size_seconds
                   << ", must be in [1, 3600]";
        return NULL;
    }
    if (options.update_interval_seconds < 1) {
        LOG(ERROR) << "Invalid update_interval_seconds="
                   << options.update_interval_seconds << ", must be >= 1";
        return NULL;
    }
    if (options.update_interval_seconds > options.window_size_seconds) {
        LOG(WARNING) << "update_interval_seconds=" << options.update_interval_seconds
                     << " exceeds window_size_seconds=" << options.window_size_seconds
                     << "; the ratio window will rarely refresh within its own period";
    }
    return new RateLimitedBackupPolicy(
        options.backup_request_ms, options.max_backup_ratio,
        options.window_size_seconds, options.update_interval_seconds);
}

} // namespace brpc
