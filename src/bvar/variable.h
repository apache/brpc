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

// Date: 2014/09/22 11:57:43

#ifndef  BVAR_VARIABLE_H
#define  BVAR_VARIABLE_H

#include <ostream>                     // std::ostream
#include <initializer_list>            // std::initializer_list
#include <string>                      // std::string
#include <vector>                      // std::vector
#include <memory>                      // std::shared_ptr
#include <utility>                     // std::declval
#include <gflags/gflags_declare.h>
#include "butil/macros.h"               // DISALLOW_COPY_AND_ASSIGN
#include "butil/strings/string_piece.h" // butil::StringPiece
#include "butil/type_traits.h"
#include "bvar/detail/exposed_ref.h"     // detail::ExposedRef

#ifdef BAIDU_INTERNAL
#include <boost/any.hpp>
#else
namespace boost {
class any;
}
#endif

namespace bvar {

DECLARE_bool(save_series);

#define COMMON_VARIABLE_CONSTRUCTOR(TypeName)                                    \
    TypeName() = default;                                                        \
    TypeName(const butil::StringPiece& name) {                                   \
        this->expose(name);                                                      \
    }                                                                            \
    TypeName(const butil::StringPiece& prefix, const butil::StringPiece& name) { \
        this->expose_as(prefix, name);                                           \
    }                                                                            \


// Bitwise masks of displayable targets 
enum DisplayFilter {
    DISPLAY_ON_HTML = 1,
    DISPLAY_ON_PLAIN_TEXT = 2,
    DISPLAY_ON_ALL = 3,
};

// Implement this class to write variables into different places.
class Dumper {
public:
    virtual ~Dumper() = default;
    // Dump one variable under its exposed `name`. `description` is produced by
    // Variable::describe(). Returning false stops Variable::dump_exposed(),
    // which then returns -1.
    virtual bool dump(const std::string& name,
                      const butil::StringPiece& description) = 0;
    // Dump one metric of a variable that maps to several of them, namely a
    // multiple dimension var or a Histogram. `name` already carries the labels
    // if there're any, as in `foo_bucket{le="10"}`.
    // Unlike dump(), implementations must NOT emit any per-metric preamble:
    // the whole family is described by a single preceding dump_comment().
    virtual bool dump_mvar(const std::string& /*name*/,
                           const butil::StringPiece& /*description*/) {
        return true;
    }
    // Dump the comment describing the type of the metric family `name`, which
    // precedes the dump_mvar() of all the metrics inside. Only meaningful to
    // the prometheus service, ignored by default.
    virtual bool dump_comment(const std::string&, const std::string& /*type*/) {
        return true;
    }
};

// One prometheus metric family exported by a composite metric, see the
// dump_samples() contract below.
struct MetricFamily {
    // Appended to the name the metric is exposed under. Empty for the main
    // family, as a Histogram has: it exports foo_bucket/foo_sum/foo_count,
    // which are the series of the single family `foo`. A LatencyRecorder in
    // contrast exports five families, "_latency", "_avg_latency" and so on.
    const char* suffix;
    // "gauge" / "counter" / "histogram" / "summary".
    const char* type;
    // Label names emitted by samples in this family. An enclosing
    // MultiDimension must not use any of them, otherwise one sample would
    // contain the same label more than once and be invalid prometheus text.
    std::vector<std::string> reserved_labels;
    // Additional sample names within this family, relative to `suffix`. A
    // Histogram family with an empty suffix declares `_bucket`, `_sum` and
    // `_count`.
    std::vector<std::string> additional_sample_suffixes;
};

// The contract of a composite metric inside a MultiDimension
//
// A type that maps to more than one prometheus metric (Histogram,
// LatencyRecorder, or one of your own) cannot be dumped as a MultiDimension
// value the default way, which writes describe() as a single number. Declare
// these two members on it instead and MultiDimension will pick them up:
//
//   // The families this type exports, in the order they should be dumped.
//   // Each family also declares label names and additional suffixed sample
//   // names it emits, if any.
//   // Return a reference to immutable storage with static lifetime.
//   static const std::vector<bvar::MetricFamily>& list_metric_families();
//
//   // Emit the samples of the family at `family_index` and nothing else: the
//   // "# TYPE" line belongs to the whole family, so the caller writes it once
//   // for all the label sets. `name' is the exposed name with the family's
//   // suffix already appended. `labels` is the labels of the enclosing
//   // MultiDimension as `k="v",k="v"` with no enclosing braces, empty when
//   // there is no MultiDimension; merge it into the brace group of every
//   // sample, before any label of your own:
//   //     foo_bucket{method="echo",le="10"} 3
//   // Return false to stop, as Dumper::dump() does.
//   bool dump_samples(bvar::Dumper* dumper, size_t family_index,
//                     const std::string& name,
//                     butil::StringPiece labels) const;
//
// Neither a base class nor a template specialization is involved: the members
// travel with the type, so they cannot go missing in a translation unit that
// forgot an include, which is exactly what would make a traits class dangerous
// here: the value would silently fall back to json.
namespace detail {

// A double as the prometheus text format spells it, used both for sample
// values and for the numbers inside a label such as `le` or `quantile`.
// Neither of the two obvious ways of writing one will do:
//   - printf("%g") is locale dependent, and an application that called
//     setlocale under a de_DE environment gets `quantile="0,99"`, where the
//     comma ends the label and starts another one;
//   - butil::DoubleToString is locale free but spells the non finite values
//     "Infinity" and "NaN", while the sample value grammar only takes `+Inf`,
//     `-Inf` and `NaN`, and a line it cannot parse is dropped from the scrape.
//     It also leaves out the integer part, and `.5` reads worse than `0.5`
//     next to the values the scalar path prints through an ostream.
std::string prometheus_double_to_string(double value);

inline std::vector<std::string> collect_metric_family_names(
    const std::string& exposed_name, const std::vector<MetricFamily>& families) {
    std::vector<std::string> names;
    for (auto& family : families) {
        std::string family_name(exposed_name);
        if (family.suffix != nullptr) {
            family_name.append(family.suffix);
        }
        names.push_back(family_name);
        for (auto& sample_suffix : family.additional_sample_suffixes) {
            names.push_back(family_name + sample_suffix);
        }
    }
    return names;
}

// True if `T` declares both members of the contract above.
template <typename T>
class IsCompositeMetric {
    template <typename U>
    static auto probe(int) -> decltype(
        U::list_metric_families(),
        std::declval<const U&>().dump_samples(
            (Dumper*)nullptr, (size_t)0, std::declval<const std::string&>(),
            butil::StringPiece()),
        butil::true_type());
    template <typename>
    static butil::false_type probe(...);
public:
    // An enum rather than `static const bool`: the latter is odr-used as soon as
    // it is bound to a reference, and C++14 would then ask for an out-of-class
    // definition of it. An enumerator never does.
    enum { value = decltype(probe<T>(0))::value };
};

}  // namespace detail

// Options for Variable::dump_exposed().
struct DumpOptions {
    // Constructed with default options.
    DumpOptions();

    // If this is true, string-type values will be quoted.
    bool quote_string;

    // The ? in wildcards. Wildcards in URL need to use another character
    // because ? is reserved.
    char question_mark;

    // Dump variables with matched display_filter
    DisplayFilter display_filter;

    // Name matched by these wildcards (or exact names) are kept.
    std::string white_wildcards;

    // Name matched by these wildcards (or exact names) are skipped.
    std::string black_wildcards;
};

struct SeriesOptions {
    SeriesOptions() : fixed_length(true), test_only(false) {}
    
    bool fixed_length; // useless now
    bool test_only;
};

// Base class of all bvar.
//
// About thread-safety:
//   bvar is thread-compatible:
//     Namely you can create/destroy/expose/hide or do whatever you want to
//     different bvar simultaneously in different threads.
//   bvar is NOT thread-safe:
//     You should not operate one bvar from different threads simultaneously.
//     If you need to, protect the ops with locks. Similarly with ordinary
//     variables, const methods are thread-safe, namely you can call
//     describe()/get_description()/get_value() etc from diferent threads
//     safely (provided that there's no non-const methods going on).
class Variable {
public:
    using SharedExposedRef = detail::SharedExposedRef<Variable>;

    Variable() = default;

    // bvar uses TLS, thus copying/assignment need to copy TLS stuff as well,
    // which is heavy. We disable copying/assignment now.
    DISALLOW_COPY_AND_ASSIGN(Variable);

    virtual ~Variable();

    // Implement this method to print the variable into ostream.
    virtual void describe(std::ostream&, bool quote_string) const = 0;

    // string form of describe().
    std::string get_description() const;

#ifdef BAIDU_INTERNAL
    // Get value.
    // If subclass does not override this method, the value is the description
    // and the type is std::string.
    virtual void get_value(boost::any* value) const;
#endif

    // Describe saved series as a json-string into the stream.
    // The output will be ploted by flot.js
    // Returns 0 on success, 1 otherwise(this variable does not save series).
    virtual int describe_series(std::ostream&, const SeriesOptions&) const
    { return 1; }

    // Send this variable to `dumper` under the exposed name `name`.
    // The default implementation sends describe() as a single metric, which is
    // what almost every variable wants. Variables mapping to several metrics
    // (Histogram maps to one metric per bucket plus `_sum` and `_count`) override
    // this to drive `dumper' themselves: one dump_comment() describing the
    // family followed by a dump_mvar() per metric.
    // Returns false to stop dump_exposed(), as Dumper::dump() does.
    virtual bool dump(Dumper* dumper, const DumpOptions& options,
                      const std::string& name) const;

    // Expose this variable globally so that it's counted in following
    // functions:
    //   list_exposed
    //   count_exposed
    //   describe_exposed
    //   find_exposed
    // Return 0 on success, -1 otherwise.
    int expose(const butil::StringPiece& name,
               DisplayFilter display_filter = DISPLAY_ON_ALL) {
        return expose_impl(butil::StringPiece(), name, display_filter);
    }
 
    // Expose this variable with a prefix.
    // Example:
    //   namespace foo {
    //   namespace bar {
    //   class ApplePie {
    //       ApplePie() {
    //           // foo_bar_apple_pie_error
    //           _error.expose_as("foo_bar_apple_pie", "error");
    //       }
    //   private:
    //       bvar::Adder<int> _error;
    //   };
    //   }  // foo
    //   }  // bar
    // Returns 0 on success, -1 otherwise.
    int expose_as(const butil::StringPiece& prefix,
                  const butil::StringPiece& name,
                  DisplayFilter display_filter = DISPLAY_ON_ALL) {
        return expose_impl(prefix, name, display_filter);
    }

    // Hide this variable so that it's not counted in *_exposed functions.
    // Returns false if this variable is already hidden.
    // CAUTION!! Subclasses must call hide() manually to avoid displaying
    // a variable that is just destructing.
    bool hide();

    // Check if this variable is is_hidden.
    bool is_hidden() const;

    // Get exposed name. If this variable is not exposed, the name is empty.
    const std::string& name() const { return _name; }

    // ====================================================================
    
    // Put names of all exposed variables into `names'.
    // If you want to print all variables, you have to go through `names'
    // and call `describe_exposed' on each name. This prevents an iteration
    // from taking the lock too long.
    static void list_exposed(std::vector<std::string>* names,
                             DisplayFilter = DISPLAY_ON_ALL);

    // Get number of exposed variables.
    static size_t count_exposed();

    // Find an exposed variable by `name' and put its description into `os'.
    // Returns 0 on found, -1 otherwise.
    static int describe_exposed(const std::string& name,
                                std::ostream& os,
                                bool quote_string = false,
                                DisplayFilter = DISPLAY_ON_ALL);
    // String form. Returns empty string when not found.
    static std::string describe_exposed(const std::string& name,
                                        bool quote_string = false,
                                        DisplayFilter = DISPLAY_ON_ALL);

    // Describe saved series of variable `name' as a json-string into `os'.
    // The output will be ploted by flot.js
    // Returns 0 on success, 1 when the variable does not save series, -1
    // otherwise (no variable named so).
    static int describe_series_exposed(const std::string& name,
                                       std::ostream&,
                                       const SeriesOptions&);

#ifdef BAIDU_INTERNAL
    // Find an exposed variable by `name' and put its value into `value'.
    // Returns 0 on found, -1 otherwise.
    static int get_exposed(const std::string& name, boost::any* value);
#endif

    // Find all exposed variables matching `white_wildcards' but
    // `black_wildcards' and send them to `dumper'.
    // Use default options when `options' is nullptr.
    // Return number of dumped variables, -1 on error.
    static int dump_exposed(Dumper* dumper, const DumpOptions* options);

protected:
    virtual int expose_impl(const butil::StringPiece& prefix,
                            const butil::StringPiece& name,
                            DisplayFilter display_filter);

    virtual std::vector<std::string> collect_prometheus_names() const;

private:
    std::string _name;
    // Shared indirection handle for calling describe() outside the VarMap lock.
    SharedExposedRef _ref;
};

// Make name only use lowercased alphabets / digits / underscores, and append
// the result to `out'.
// Examples:
//   foo-inl.h       -> foo_inl_h
//   foo::bar::Apple -> foo_bar_apple
//   Car_Rot         -> car_rot
//   FooBar          -> foo_bar
//   RPCTest         -> rpctest
//   HELLO           -> hello
void to_underscored_name(std::string* out, const butil::StringPiece& name);

}  // namespace bvar

// Make variables printable.
namespace std {

inline ostream& operator<<(ostream &os, const ::bvar::Variable &var) {
    var.describe(os, false);
    return os;
}

}  // namespace std

#endif  // BVAR_VARIABLE_H
