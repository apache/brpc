// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Time represents an absolute point in coordinated universal time (UTC),
// internally represented as microseconds (s/1,000,000) since the Windows epoch
// (1601-01-01 00:00:00 UTC) (See http://crbug.com/14734).  System-dependent
// clock interface routines are defined in time_PLATFORM.cc.
//
// TimeDelta represents a duration of time, internally represented in
// microseconds.
//
// TimeTicks represents an abstract time that is most of the time incrementing
// for use in measuring time durations. It is internally represented in
// microseconds.  It can not be converted to a human-readable time, but is
// guaranteed not to decrease (if the user changes the computer clock,
// Time::Now() may actually decrease or jump).  But note that TimeTicks may
// "stand still", for example if the computer suspended.
//
// These classes are represented as only a 64-bit value, so they can be
// efficiently passed by value.

#ifndef BUTIL_TIME_TIME_H_
#define BUTIL_TIME_TIME_H_

#include <time.h>

#include "butil/base_export.h"
#include "butil/basictypes.h"
#include "butil/build_config.h"

#if defined(OS_MACOSX)
#include <CoreFoundation/CoreFoundation.h>
// Avoid Mac system header macro leak.
#undef TYPE_BOOL
#endif

#if defined(OS_POSIX)
#include <unistd.h>
#include <sys/time.h>
#endif

#if defined(OS_WIN)
// For FILETIME in FromFileTime, until it moves to a new converter class.
// See TODO(iyengar) below.
#include <windows.h>
#endif

#include <limits>

#ifdef BAIDU_INTERNAL
// gejun: Force inclusion of ul_def.h and undef conflict macros
#include <ul_def.h>
#undef Uchar
#undef Ushort
#undef Uint
#undef Max
#undef Min
#undef Exchange
#endif // BAIDU_INTERNAL

namespace butil {

class Time;
class TimeTicks;

// TimeDelta ------------------------------------------------------------------

class BUTIL_EXPORT TimeDelta {
 public:
  TimeDelta() : delta_(0) {
  }

  // Converts units of time to TimeDeltas.
  static TimeDelta FromDays(int days);
  static TimeDelta FromHours(int hours);
  static TimeDelta FromMinutes(int minutes);
  static TimeDelta FromSeconds(int64_t secs);
  static TimeDelta FromMilliseconds(int64_t ms);
  static TimeDelta FromMicroseconds(int64_t us);
  static TimeDelta FromSecondsD(double secs);
  static TimeDelta FromMillisecondsD(double ms);
  static TimeDelta FromMicrosecondsD(double us);

#if defined(OS_WIN)
  static TimeDelta FromQPCValue(LONGLONG qpc_value);
#endif

  // Converts an integer value representing TimeDelta to a class. This is used
  // when deserializing a |TimeDelta| structure, using a value known to be
  // compatible. It is not provided as a constructor because the integer type
  // may be unclear from the perspective of a caller.
  static TimeDelta FromInternalValue(int64_t delta) {
    return TimeDelta(delta);
  }

  // Returns the maximum time delta, which should be greater than any reasonable
  // time delta we might compare it to. Adding or subtracting the maximum time
  // delta to a time or another time delta has an undefined result.
  static TimeDelta Max();

  // Returns the internal numeric value of the TimeDelta object. Please don't
  // use this and do arithmetic on it, as it is more error prone than using the
  // provided operators.
  // For serializing, use FromInternalValue to reconstitute.
  int64_t ToInternalValue() const {
    return delta_;
  }

  // Returns true if the time delta is the maximum time delta.
  bool is_max() const {
    return delta_ == std::numeric_limits<int64_t>::max();
  }

#if defined(OS_POSIX)
  struct timespec ToTimeSpec() const;
#endif

  // Returns the time delta in some unit. The F versions return a floating
  // point value, the "regular" versions return a rounded-down value.
  //
  // InMillisecondsRoundedUp() instead returns an integer that is rounded up
  // to the next full millisecond.
  int InDays() const;
  int InHours() const;
  int InMinutes() const;
  double InSecondsF() const;
  int64_t InSeconds() const;
  double InMillisecondsF() const;
  int64_t InMilliseconds() const;
  int64_t InMillisecondsRoundedUp() const;
  int64_t InMicroseconds() const;

  TimeDelta& operator=(TimeDelta other) {
    delta_ = other.delta_;
    return *this;
  }

  // Computations with other deltas.
  TimeDelta operator+(TimeDelta other) const {
    return TimeDelta(delta_ + other.delta_);
  }
  TimeDelta operator-(TimeDelta other) const {
    return TimeDelta(delta_ - other.delta_);
  }

  TimeDelta& operator+=(TimeDelta other) {
    delta_ += other.delta_;
    return *this;
  }
  TimeDelta& operator-=(TimeDelta other) {
    delta_ -= other.delta_;
    return *this;
  }
  TimeDelta operator-() const {
    return TimeDelta(-delta_);
  }

  // Computations with ints, note that we only allow multiplicative operations
  // with ints, and additive operations with other deltas.
  TimeDelta operator*(int64_t a) const {
    return TimeDelta(delta_ * a);
  }
  TimeDelta operator/(int64_t a) const {
    return TimeDelta(delta_ / a);
  }
  TimeDelta& operator*=(int64_t a) {
    delta_ *= a;
    return *this;
  }
  TimeDelta& operator/=(int64_t a) {
    delta_ /= a;
    return *this;
  }
  int64_t operator/(TimeDelta a) const {
    return delta_ / a.delta_;
  }

  // Defined below because it depends on the definition of the other classes.
  Time operator+(Time t) const;
  TimeTicks operator+(TimeTicks t) const;

  // Comparison operators.
  bool operator==(TimeDelta other) const {
    return delta_ == other.delta_;
  }
  bool operator!=(TimeDelta other) const {
    return delta_ != other.delta_;
  }
  bool operator<(TimeDelta other) const {
    return delta_ < other.delta_;
  }
  bool operator<=(TimeDelta other) const {
    return delta_ <= other.delta_;
  }
  bool operator>(TimeDelta other) const {
    return delta_ > other.delta_;
  }
  bool operator>=(TimeDelta other) const {
    return delta_ >= other.delta_;
  }

 private:
  friend class Time;
  friend class TimeTicks;
  friend TimeDelta operator*(int64_t a, TimeDelta td);

  // Constructs a delta given the duration in microseconds. This is private
  // to avoid confusion by callers with an integer constructor. Use
  // FromSeconds, FromMilliseconds, etc. instead.
  explicit TimeDelta(int64_t delta_us) : delta_(delta_us) {
  }

  // Delta in microseconds.
  int64_t delta_;
};

inline TimeDelta operator*(int64_t a, TimeDelta td) {
  return TimeDelta(a * td.delta_);
}

// Time -----------------------------------------------------------------------

// Represents a wall clock time in UTC.
class BUTIL_EXPORT Time {
 public:
  static const int64_t kMillisecondsPerSecond = 1000;
  static const int64_t kMicrosecondsPerMillisecond = 1000;
  static const int64_t kMicrosecondsPerSecond = kMicrosecondsPerMillisecond *
                                              kMillisecondsPerSecond;
  static const int64_t kMicrosecondsPerMinute = kMicrosecondsPerSecond * 60;
  static const int64_t kMicrosecondsPerHour = kMicrosecondsPerMinute * 60;
  static const int64_t kMicrosecondsPerDay = kMicrosecondsPerHour * 24;
  static const int64_t kMicrosecondsPerWeek = kMicrosecondsPerDay * 7;
  static const int64_t kNanosecondsPerMicrosecond = 1000;
  static const int64_t kNanosecondsPerSecond = kNanosecondsPerMicrosecond *
                                             kMicrosecondsPerSecond;

#if !defined(OS_WIN)
  // On Mac & Linux, this value is the delta from the Windows epoch of 1601 to
  // the Posix delta of 1970. This is used for migrating between the old
  // 1970-based epochs to the new 1601-based ones. It should be removed from
  // this global header and put in the platform-specific ones when we remove the
  // migration code.
  static const int64_t kWindowsEpochDeltaMicroseconds;
#endif

  // Represents an exploded time that can be formatted nicely. This is kind of
  // like the Win32 SYSTEMTIME structure or the Unix "struct tm" with a few
  // additions and changes to prevent errors.
  struct BUTIL_EXPORT Exploded {
    int year;          // Four digit year "2007"
    int month;         // 1-based month (values 1 = January, etc.)
    int day_of_week;   // 0-based day of week (0 = Sunday, etc.)
    int day_of_month;  // 1-based day of month (1-31)
    int hour;          // Hour within the current day (0-23)
    int minute;        // Minute within the current hour (0-59)
    int second;        // Second within the current minute (0-59 plus leap
                       //   seconds which may take it up to 60).
    int millisecond;   // Milliseconds within the current second (0-999)

    // A cursory test for whether the data members are within their
    // respective ranges. A 'true' return value does not guarantee the
    // Exploded value can be successfully converted to a Time value.
    bool HasValidValues() const;
  };

  // Contains the NULL time. Use Time::Now() to get the current time.
  Time() : us_(0) {
  }

  // Returns true if the time object has not been initialized.
  bool is_null() const {
    return us_ == 0;
  }

  // Returns true if the time object is the maximum time.
  bool is_max() const {
    return us_ == std::numeric_limits<int64_t>::max();
  }

  // Returns the time for epoch in Unix-like system (Jan 1, 1970).
  static Time UnixEpoch();

  // Returns the current time. Watch out, the system might adjust its clock
  // in which case time will actually go backwards. We don't guarantee that
  // times are increasing, or that two calls to Now() won't be the same.
  static Time Now();

  // Returns the maximum time, which should be greater than any reasonable time
  // with which we might compare it.
  static Time Max();

  // Returns the current time. Same as Now() except that this function always
  // uses system time so that there are no discrepancies between the returned
  // time and system time even on virtual environments including our test bot.
  // For timing sensitive unittests, this function should be used.
  static Time NowFromSystemTime();

  // Converts to/from time_t in UTC and a Time class.
  // TODO(brettw) this should be removed once everybody starts using the |Time|
  // class.
  static Time FromTimeT(time_t tt);
  time_t ToTimeT() const;

  // Converts time to/from a double which is the number of seconds since epoch
  // (Jan 1, 1970).  Webkit uses this format to represent time.
  // Because WebKit initializes double time value to 0 to indicate "not
  // initialized", we map it to empty Time object that also means "not
  // initialized".
  static Time FromDoubleT(double dt);
  double ToDoubleT() const;

#if defined(OS_POSIX)
  // Converts the timespec structure to time. MacOS X 10.8.3 (and tentatively,
  // earlier versions) will have the |ts|'s tv_nsec component zeroed out,
  // having a 1 second resolution, which agrees with
  // https://developer.apple.com/legacy/library/#technotes/tn/tn1150.html#HFSPlusDates.
  static Time FromTimeSpec(const timespec& ts);
#endif

  // Converts to/from the Javascript convention for times, a number of
  // milliseconds since the epoch:
  // https://developer.mozilla.org/en/JavaScript/Reference/Global_Objects/Date/getTime.
  static Time FromJsTime(double ms_since_epoch);
  double ToJsTime() const;

  // Converts to Java convention for times, a number of
  // milliseconds since the epoch.
  int64_t ToJavaTime() const;

#if defined(OS_POSIX)
  static Time FromTimeVal(struct timeval t);
  struct timeval ToTimeVal() const;
#endif

#if defined(OS_MACOSX)
  static Time FromCFAbsoluteTime(CFAbsoluteTime t);
  CFAbsoluteTime ToCFAbsoluteTime() const;
#endif

#if defined(OS_WIN)
  static Time FromFileTime(FILETIME ft);
  FILETIME ToFileTime() const;

  // The minimum time of a low resolution timer.  This is basically a windows
  // constant of ~15.6ms.  While it does vary on some older OS versions, we'll
  // treat it as static across all windows versions.
  static const int kMinLowResolutionThresholdMs = 16;

  // Enable or disable Windows high resolution timer. If the high resolution
  // timer is not enabled, calls to ActivateHighResolutionTimer will fail.
  // When disabling the high resolution timer, this function will not cause
  // the high resolution timer to be deactivated, but will prevent future
  // activations.
  // Must be called from the main thread.
  // For more details see comments in time_win.cc.
  static void EnableHighResolutionTimer(bool enable);

  // Activates or deactivates the high resolution timer based on the |activate|
  // flag.  If the HighResolutionTimer is not Enabled (see
  // EnableHighResolutionTimer), this function will return false.  Otherwise
  // returns true.  Each successful activate call must be paired with a
  // subsequent deactivate call.
  // All callers to activate the high resolution timer must eventually call
  // this function to deactivate the high resolution timer.
  static bool ActivateHighResolutionTimer(bool activate);

  // Returns true if the high resolution timer is both enabled and activated.
  // This is provided for testing only, and is not tracked in a thread-safe
  // way.
  static bool IsHighResolutionTimerInUse();
#endif

  // Converts an exploded structure representing either the local time or UTC
  // into a Time class.
  static Time FromUTCExploded(const Exploded& exploded) {
    return FromExploded(false, exploded);
  }
  static Time FromLocalExploded(const Exploded& exploded) {
    return FromExploded(true, exploded);
  }

  // Converts an integer value representing Time to a class. This is used
  // when deserializing a |Time| structure, using a value known to be
  // compatible. It is not provided as a constructor because the integer type
  // may be unclear from the perspective of a caller.
  static Time FromInternalValue(int64_t us) {
    return Time(us);
  }

  // Converts a string representation of time to a Time object.
  // An example of a time string which is converted is as below:-
  // "Tue, 15 Nov 1994 12:45:26 GMT". If the timezone is not specified
  // in the input string, FromString assumes local time and FromUTCString
  // assumes UTC. A timezone that cannot be parsed (e.g. "UTC" which is not
  // specified in RFC822) is treated as if the timezone is not specified.
  // TODO(iyengar) Move the FromString/FromTimeT/ToTimeT/FromFileTime to
  // a new time converter class.
  static bool FromString(const char* time_string, Time* parsed_time) {
    return FromStringInternal(time_string, true, parsed_time);
  }
  static bool FromUTCString(const char* time_string, Time* parsed_time) {
    return FromStringInternal(time_string, false, parsed_time);
  }

  // For serializing, use FromInternalValue to reconstitute. Please don't use
  // this and do arithmetic on it, as it is more error prone than using the
  // provided operators.
  int64_t ToInternalValue() const {
    return us_;
  }

  // Fills the given exploded structure with either the local time or UTC from
  // this time structure (containing UTC).
  void UTCExplode(Exploded* exploded) const {
    return Explode(false, exploded);
  }
  void LocalExplode(Exploded* exploded) const {
    return Explode(true, exploded);
  }

  // Rounds this time down to the nearest day in local time. It will represent
  // midnight on that day.
  Time LocalMidnight() const;

  Time& operator=(Time other) {
    us_ = other.us_;
    return *this;
  }

  // Compute the difference between two times.
  TimeDelta operator-(Time other) const {
    return TimeDelta(us_ - other.us_);
  }

  // Modify by some time delta.
  Time& operator+=(TimeDelta delta) {
    us_ += delta.delta_;
    return *this;
  }
  Time& operator-=(TimeDelta delta) {
    us_ -= delta.delta_;
    return *this;
  }

  // Return a new time modified by some delta.
  Time operator+(TimeDelta delta) const {
    return Time(us_ + delta.delta_);
  }
  Time operator-(TimeDelta delta) const {
    return Time(us_ - delta.delta_);
  }

  // Comparison operators
  bool operator==(Time other) const {
    return us_ == other.us_;
  }
  bool operator!=(Time other) const {
    return us_ != other.us_;
  }
  bool operator<(Time other) const {
    return us_ < other.us_;
  }
  bool operator<=(Time other) const {
    return us_ <= other.us_;
  }
  bool operator>(Time other) const {
    return us_ > other.us_;
  }
  bool operator>=(Time other) const {
    return us_ >= other.us_;
  }

 private:
  friend class TimeDelta;

  explicit Time(int64_t us) : us_(us) {
  }

  // Explodes the given time to either local time |is_local = true| or UTC
  // |is_local = false|.
  void Explode(bool is_local, Exploded* exploded) const;

  // Unexplodes a given time assuming the source is either local time
  // |is_local = true| or UTC |is_local = false|.
  static Time FromExploded(bool is_local, const Exploded& exploded);

  // Converts a string representation of time to a Time object.
  // An example of a time string which is converted is as below:-
  // "Tue, 15 Nov 1994 12:45:26 GMT". If the timezone is not specified
  // in the input string, local time |is_local = true| or
  // UTC |is_local = false| is assumed. A timezone that cannot be parsed
  // (e.g. "UTC" which is not specified in RFC822) is treated as if the
  // timezone is not specified.
  static bool FromStringInternal(const char* time_string,
                                 bool is_local,
                                 Time* parsed_time);

  // The representation of Jan 1, 1970 UTC in microseconds since the
  // platform-dependent epoch.
  static const int64_t kTimeTToMicrosecondsOffset;

#if defined(OS_WIN)
  // Indicates whether fast timers are usable right now.  For instance,
  // when using battery power, we might elect to prevent high speed timers
  // which would draw more power.
  static bool high_resolution_timer_enabled_;
  // Count of activations on the high resolution timer.  Only use in tests
  // which are single threaded.
  static int high_resolution_timer_activated_;
#endif

  // Time in microseconds in UTC.
  int64_t us_;
};

// Inline the TimeDelta factory methods, for fast TimeDelta construction.

// static
inline TimeDelta TimeDelta::FromDays(int days) {
  // Preserve max to prevent overflow.
  if (days == std::numeric_limits<int>::max())
    return Max();
  return TimeDelta(days * Time::kMicrosecondsPerDay);
}

// static
inline TimeDelta TimeDelta::FromHours(int hours) {
  // Preserve max to prevent overflow.
  if (hours == std::numeric_limits<int>::max())
    return Max();
  return TimeDelta(hours * Time::kMicrosecondsPerHour);
}

// static
inline TimeDelta TimeDelta::FromMinutes(int minutes) {
  // Preserve max to prevent overflow.
  if (minutes == std::numeric_limits<int>::max())
    return Max();
  return TimeDelta(minutes * Time::kMicrosecondsPerMinute);
}

// static
inline TimeDelta TimeDelta::FromSeconds(int64_t secs) {
  // Preserve max to prevent overflow.
  if (secs == std::numeric_limits<int64_t>::max())
    return Max();
  return TimeDelta(secs * Time::kMicrosecondsPerSecond);
}

// static
inline TimeDelta TimeDelta::FromMilliseconds(int64_t ms) {
  // Preserve max to prevent overflow.
  if (ms == std::numeric_limits<int64_t>::max())
    return Max();
  return TimeDelta(ms * Time::kMicrosecondsPerMillisecond);
}

// static
inline TimeDelta TimeDelta::FromSecondsD(double secs) {
  // Preserve max to prevent overflow.
  if (secs == std::numeric_limits<double>::infinity())
    return Max();
  return TimeDelta((int64_t)(secs * Time::kMicrosecondsPerSecond));
}

// static
inline TimeDelta TimeDelta::FromMillisecondsD(double ms) {
  // Preserve max to prevent overflow.
  if (ms == std::numeric_limits<double>::infinity())
    return Max();
  return TimeDelta((int64_t)(ms * Time::kMicrosecondsPerMillisecond));
}

// static
inline TimeDelta TimeDelta::FromMicroseconds(int64_t us) {
  // Preserve max to prevent overflow.
  if (us == std::numeric_limits<int64_t>::max())
    return Max();
  return TimeDelta(us);
}

// static
inline TimeDelta TimeDelta::FromMicrosecondsD(double us) {
  // Preserve max to prevent overflow.
  if (us == std::numeric_limits<double>::infinity())
    return Max();
  return TimeDelta((int64_t)us);
}

inline Time TimeDelta::operator+(Time t) const {
  return Time(t.us_ + delta_);
}

// TimeTicks ------------------------------------------------------------------

class BUTIL_EXPORT TimeTicks {
 public:
  // We define this even without OS_CHROMEOS for seccomp sandbox testing.
#if defined(OS_LINUX)
  // Force definition of the system trace clock; it is a chromeos-only api
  // at the moment and surfacing it in the right place requires mucking
  // with glibc et al.
  static const clockid_t kClockSystemTrace = 11;
#endif

  TimeTicks() : ticks_(0) {
  }

  // Platform-dependent tick count representing "right now."
  // The resolution of this clock is ~1-15ms.  Resolution varies depending
  // on hardware/operating system configuration.
  static TimeTicks Now();

  // Returns a platform-dependent high-resolution tick count. Implementation
  // is hardware dependent and may or may not return sub-millisecond
  // resolution.  THIS CALL IS GENERALLY MUCH MORE EXPENSIVE THAN Now() AND
  // SHOULD ONLY BE USED WHEN IT IS REALLY NEEDED.
  static TimeTicks HighResNow();

  static bool IsHighResNowFastAndReliable();

  // FIXME(gejun): CLOCK_THREAD_CPUTIME_ID behaves differently in different
  // glibc. To avoid potential bug, disable ThreadNow() always.
  // Returns true if ThreadNow() is supported on this system.
  static bool IsThreadNowSupported() {
      return false;
  }

  // Returns thread-specific CPU-time on systems that support this feature.
  // Needs to be guarded with a call to IsThreadNowSupported(). Use this timer
  // to (approximately) measure how much time the calling thread spent doing
  // actual work vs. being de-scheduled. May return bogus results if the thread
  // migrates to another CPU between two calls.
  static TimeTicks ThreadNow();

  // Returns the current system trace time or, if none is defined, the current
  // high-res time (i.e. HighResNow()). On systems where a global trace clock
  // is defined, timestamping TraceEvents's with this value guarantees
  // synchronization between events collected inside chrome and events
  // collected outside (e.g. kernel, X server).
  static TimeTicks NowFromSystemTraceTime();

#if defined(OS_WIN)
  // Get the absolute value of QPC time drift. For testing.
  static int64_t GetQPCDriftMicroseconds();

  static TimeTicks FromQPCValue(LONGLONG qpc_value);

  // Returns true if the high resolution clock is working on this system.
  // This is only for testing.
  static bool IsHighResClockWorking();

  // Enable high resolution time for TimeTicks::Now(). This function will
  // test for the availability of a working implementation of
  // QueryPerformanceCounter(). If one is not available, this function does
  // nothing and the resolution of Now() remains 1ms. Otherwise, all future
  // calls to TimeTicks::Now() will have the higher resolution provided by QPC.
  // Returns true if high resolution time was successfully enabled.
  static bool SetNowIsHighResNowIfSupported();

  // Returns a time value that is NOT rollover protected.
  static TimeTicks UnprotectedNow();
#endif

  // Returns true if this object has not been initialized.
  bool is_null() const {
    return ticks_ == 0;
  }

  // Converts an integer value representing TimeTicks to a class. This is used
  // when deserializing a |TimeTicks| structure, using a value known to be
  // compatible. It is not provided as a constructor because the integer type
  // may be unclear from the perspective of a caller.
  static TimeTicks FromInternalValue(int64_t ticks) {
    return TimeTicks(ticks);
  }

  // Get the TimeTick value at the time of the UnixEpoch. This is useful when
  // you need to relate the value of TimeTicks to a real time and date.
  // Note: Upon first invocation, this function takes a snapshot of the realtime
  // clock to establish a reference point.  This function will return the same
  // value for the duration of the application, but will be different in future
  // application runs.
  static TimeTicks UnixEpoch();

  // Returns the internal numeric value of the TimeTicks object.
  // For serializing, use FromInternalValue to reconstitute.
  int64_t ToInternalValue() const {
    return ticks_;
  }

  TimeTicks& operator=(TimeTicks other) {
    ticks_ = other.ticks_;
    return *this;
  }

  // Compute the difference between two times.
  TimeDelta operator-(TimeTicks other) const {
    return TimeDelta(ticks_ - other.ticks_);
  }

  // Modify by some time delta.
  TimeTicks& operator+=(TimeDelta delta) {
    ticks_ += delta.delta_;
    return *this;
  }
  TimeTicks& operator-=(TimeDelta delta) {
    ticks_ -= delta.delta_;
    return *this;
  }

  // Return a new TimeTicks modified by some delta.
  TimeTicks operator+(TimeDelta delta) const {
    return TimeTicks(ticks_ + delta.delta_);
  }
  TimeTicks operator-(TimeDelta delta) const {
    return TimeTicks(ticks_ - delta.delta_);
  }

  // Comparison operators
  bool operator==(TimeTicks other) const {
    return ticks_ == other.ticks_;
  }
  bool operator!=(TimeTicks other) const {
    return ticks_ != other.ticks_;
  }
  bool operator<(TimeTicks other) const {
    return ticks_ < other.ticks_;
  }
  bool operator<=(TimeTicks other) const {
    return ticks_ <= other.ticks_;
  }
  bool operator>(TimeTicks other) const {
    return ticks_ > other.ticks_;
  }
  bool operator>=(TimeTicks other) const {
    return ticks_ >= other.ticks_;
  }

 protected:
  friend class TimeDelta;

  // Please use Now() to create a new object. This is for internal use
  // and testing. Ticks is in microseconds.
  explicit TimeTicks(int64_t ticks) : ticks_(ticks) {
  }

  // Tick count in microseconds.
  int64_t ticks_;

#if defined(OS_WIN)
  typedef DWORD (*TickFunctionType)(void);
  static TickFunctionType SetMockTickFunction(TickFunctionType ticker);
#endif
};

inline TimeTicks TimeDelta::operator+(TimeTicks t) const {
  return TimeTicks(t.ticks_ + delta_);
}

}  // namespace butil

#endif  // BUTIL_TIME_TIME_H_
