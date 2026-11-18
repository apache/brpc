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

#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
#include "bvar/detail/exposed_ref.h"
#include "bvar/mvariable.h"
#include "bvar/variable.h"

namespace {

using bvar::detail::ScopedExposedRef;
using bvar::detail::make_exposed_ref;

static_assert(!std::is_copy_constructible<ScopedExposedRef<int>>::value,
              "ScopedExposedRef must not duplicate ownership");
static_assert(!std::is_copy_assignable<ScopedExposedRef<int>>::value,
              "ScopedExposedRef must not duplicate ownership");

TEST(ScopedExposedRefTest, scope_exit_and_early_return) {
    std::string value = "value";
    auto ref = make_exposed_ref(&value);
    {
        ScopedExposedRef<std::string> first(ref);
        EXPECT_EQ(&value, first.get());
        EXPECT_EQ(value.size(), first->size());
        const auto read = [&]() {
            ScopedExposedRef<std::string> second(ref);
            EXPECT_EQ(2, ref->_nref);
            return second->size();
        };
        EXPECT_EQ(value.size(), read());
        EXPECT_EQ(1, ref->_nref);
    }
    EXPECT_EQ(0, ref->_nref);
}

TEST(ScopedExposedRefTest, keeps_handle_alive) {
    int value = 42;
    auto ref = make_exposed_ref(&value);
    std::weak_ptr<bvar::detail::ExposedRef<int>> weak_ref = ref;
    {
        ScopedExposedRef<int> reader(ref);
        ref.reset();
        EXPECT_FALSE(weak_ref.expired());
        EXPECT_EQ(&value, reader.get());
    }
    EXPECT_TRUE(weak_ref.expired());
}

TEST(ScopedExposedRefTest, reset_and_hidden_handle) {
    int value = 42;
    auto ref = make_exposed_ref(&value);
    auto hidden_ref = make_exposed_ref(&value);
    hidden_ref->hide_and_wait();
    {
        ScopedExposedRef<int> reader;
        EXPECT_TRUE(reader == nullptr);
        EXPECT_EQ(nullptr, reader.get());
        reader.reset();
        reader.reset(ref);
        EXPECT_TRUE(reader != nullptr);
        EXPECT_EQ(1, ref->_nref);
        reader.reset(ref);
        EXPECT_EQ(1, ref->_nref);
        reader.reset(hidden_ref);
        EXPECT_TRUE(reader == nullptr);
        EXPECT_EQ(0, ref->_nref);
        EXPECT_EQ(0, hidden_ref->_nref);
        reader.reset();
        reader.reset();
    }
    EXPECT_EQ(0, hidden_ref->_nref);
}

TEST(ScopedExposedRefTest, move_transfers_ownership) {
    int first_value = 1;
    int second_value = 2;
    auto first_ref = make_exposed_ref(&first_value);
    auto second_ref = make_exposed_ref(&second_value);
    {
        ScopedExposedRef<int> first(first_ref);
        ScopedExposedRef<int> moved(std::move(first));
        EXPECT_TRUE(first == nullptr);
        EXPECT_EQ(&first_value, moved.get());
        EXPECT_EQ(1, first_ref->_nref);

        ScopedExposedRef<int> second(second_ref);
        second = std::move(moved);
        EXPECT_TRUE(moved == nullptr);
        EXPECT_EQ(&first_value, second.get());
        EXPECT_EQ(1, first_ref->_nref);
        EXPECT_EQ(0, second_ref->_nref);

        auto* self = &second;
        second = std::move(*self);
        EXPECT_EQ(&first_value, second.get());
        EXPECT_EQ(1, first_ref->_nref);

        // hide_all() stores guards in a vector, which may move them again.
        std::vector<ScopedExposedRef<int>> readers;
        readers.push_back(std::move(second));
        readers.emplace_back(first_ref);
        EXPECT_TRUE(second == nullptr);
        EXPECT_EQ(2, first_ref->_nref);
    }
    EXPECT_EQ(0, first_ref->_nref);
    EXPECT_EQ(0, second_ref->_nref);
}

#if GTEST_HAS_EXCEPTIONS

struct CallbackError {};

// There are no concurrent readers in these tests. Drain leaked references on
// failure so a regression reports an assertion instead of hanging in hide().
template <typename T>
void expect_no_readers(const bvar::detail::SharedExposedRef<T>& ref) {
    EXPECT_EQ(0, ref->_nref);
    while (ref->_nref > 0) {
        ref->release();
    }
}

class ThrowingVariable : public bvar::Variable {
public:
    ~ThrowingVariable() override { hide(); }
    void describe(std::ostream&, bool) const override {
        throw CallbackError();
    }
    int describe_series(std::ostream&,
                        const bvar::SeriesOptions&) const override {
        throw CallbackError();
    }
#ifdef BAIDU_INTERNAL
    void get_value(boost::any*) const override { throw CallbackError(); }
#endif
};

class ThrowingMVariable : public bvar::MVariableBase {
public:
    ~ThrowingMVariable() override { hide(); }
    void describe(std::ostream&) override { throw CallbackError(); }
    size_t dump(bvar::Dumper*, const bvar::DumpOptions*) override {
        throw CallbackError();
    }
};

class DiscardingDumper : public bvar::Dumper {
public:
    bool dump(const std::string&, const butil::StringPiece&) override {
        return true;
    }
};

TEST(ScopedExposedRefTest, variable_callbacks_release_on_exception) {
    ThrowingVariable var;
    ASSERT_EQ(0, var.expose("scoped_ref_throwing_variable"));
    std::ostringstream os;
    EXPECT_THROW(bvar::Variable::describe_exposed(var.name(), os),
                 CallbackError);
    expect_no_readers(var._ref);

    bvar::SeriesOptions series_options;
    EXPECT_THROW(bvar::Variable::describe_series_exposed(
                     var.name(), os, series_options), CallbackError);
    expect_no_readers(var._ref);

    DiscardingDumper dumper;
    bvar::DumpOptions dump_options;
    dump_options.white_wildcards = var.name();
    EXPECT_THROW(bvar::Variable::dump_exposed(&dumper, &dump_options),
                 CallbackError);
    expect_no_readers(var._ref);
#ifdef BAIDU_INTERNAL
    boost::any value;
    EXPECT_THROW(bvar::Variable::get_exposed(var.name(), &value),
                 CallbackError);
    expect_no_readers(var._ref);
#endif
    EXPECT_TRUE(var.hide());
}

TEST(ScopedExposedRefTest, mvariable_callbacks_release_on_exception) {
    ThrowingMVariable var;
    ASSERT_EQ(0, var.expose("scoped_ref_throwing_mvariable"));
    std::ostringstream os;
    EXPECT_THROW(bvar::MVariableBase::describe_exposed(var.name(), os),
                 CallbackError);
    expect_no_readers(var._ref);

    DiscardingDumper dumper;
    EXPECT_THROW(bvar::MVariableBase::dump_exposed(&dumper, nullptr),
                 CallbackError);
    expect_no_readers(var._ref);
    EXPECT_TRUE(var.hide());
}

#endif  // GTEST_HAS_EXCEPTIONS

}  // namespace
