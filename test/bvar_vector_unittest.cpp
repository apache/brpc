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

#include <cstddef>
#include <sstream>
#include <type_traits>

// Existing users may forward-declare the original class template.
namespace bvar {
template <typename T, size_t N> class Vector;
}

#include "bvar/passive_status.h"
#include <gtest/gtest.h>

// Alias templates cannot replace a class template in explicit instantiations.
template class bvar::Vector<int, 2>;

namespace {

struct DerivedVector : bvar::Vector<int, 2> {
    DerivedVector() : Vector(7) {}
};

struct CustomVector {};

}  // namespace

namespace bvar {

// Preserve existing user specializations of the original trait.
template <> struct is_vector<CustomVector> : butil::true_type {};

}  // namespace bvar

namespace {

TEST(VectorTest, preserves_class_identity) {
    static_assert(std::is_same<bvar::Vector<int, 2>,
                               bvar::FixedVector<int, 2> >::value,
                  "Both names must denote the original class template");
    DerivedVector derived;
    EXPECT_EQ(7, derived[0]);
    EXPECT_EQ(7, derived[1]);

    bvar::FixedVector<int, 2> value(3);
    value += derived;
    EXPECT_EQ(10, value[0]);
    EXPECT_EQ(10, value[1]);
    value -= derived;
    value *= 4;
    value /= 2;
    EXPECT_EQ((bvar::Vector<int, 2>(6)), value);

    std::ostringstream legacy_output;
    std::ostringstream new_output;
    legacy_output << static_cast<const bvar::Vector<int, 2>&>(value);
    new_output << value;
    EXPECT_EQ(legacy_output.str(), new_output.str());
}

TEST(VectorTest, preserves_legacy_trait_specializations) {
    EXPECT_TRUE((bvar::is_vector<bvar::FixedVector<int, 2> >::value));
    EXPECT_TRUE((bvar::is_fixed_vector<bvar::Vector<int, 2> >::value));
    EXPECT_FALSE(bvar::is_fixed_vector<int>::value);
    EXPECT_TRUE(bvar::is_fixed_vector<CustomVector>::value);
    EXPECT_TRUE(bvar::PassiveStatus<CustomVector>::ADDITIVE);
}

TEST(VectorTest, preserves_series_labels) {
    typedef bvar::FixedVector<int, 2> Value;
    typedef bvar::detail::AddTo<Value> Op;
    bvar::detail::Series<Value, Op> series((Op()));
    series.append(Value(3));

    std::ostringstream default_output;
    series.describe(default_output, nullptr);
    EXPECT_NE(std::string::npos,
              default_output.str().find("\"label\":\"Vector[0]\""));
    EXPECT_NE(std::string::npos,
              default_output.str().find("\"label\":\"Vector[1]\""));

    const std::string names = "used,free";
    std::ostringstream named_output;
    series.describe(named_output, &names);
    EXPECT_NE(std::string::npos,
              named_output.str().find("\"label\":\"used\""));
    EXPECT_NE(std::string::npos,
              named_output.str().find("\"label\":\"free\""));
}

}  // namespace
