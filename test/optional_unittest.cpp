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


#include <gtest/gtest.h>
#include "butil/containers/optional.h"

namespace {

butil::optional<int> empty_optional() {
    return {};
}

TEST(OptionalTest, sanity) {
    {
        butil::optional<int> empty;
        ASSERT_FALSE(empty);
    }

    {
        butil::optional<int> empty{};
        ASSERT_FALSE(empty);
    }

    {
        butil::optional<int> empty = empty_optional();
        ASSERT_FALSE(empty);
    }

    {
        butil::optional<int> non_empty(42);
        ASSERT_TRUE(non_empty);
        ASSERT_TRUE(non_empty.has_value());
        ASSERT_EQ(*non_empty, 42);
    }

    butil::optional<std::string> opt_string = "abc";
    ASSERT_TRUE(opt_string);
    ASSERT_EQ(*opt_string, "abc");
}

TEST(OptionalTest, nullopt) {
    butil::optional<int> empty(butil::nullopt);
    ASSERT_FALSE(empty);

    empty = 1;
    ASSERT_TRUE(empty);
    ASSERT_EQ(1, empty);

    empty = butil::nullopt;
    ASSERT_FALSE(empty);
}

TEST(OptionalTest, copy) {
    butil::optional<int> op(1);
    ASSERT_TRUE(op);
    ASSERT_EQ(1, op);

    butil::optional<int> non_empty(op);
    ASSERT_TRUE(non_empty);

    op = butil::nullopt;
    ASSERT_FALSE(op);

    butil::optional<int> empty(op);
    ASSERT_FALSE(empty);

    non_empty = empty;
    ASSERT_FALSE(non_empty);

    op = 10;
    non_empty = op;
    ASSERT_TRUE(non_empty);
    ASSERT_EQ(10, non_empty);
}

TEST(OptionalTest, move) {
    butil::optional<int> empty;
    ASSERT_FALSE(empty);
    butil::optional<int> non_empty = 1;
    ASSERT_TRUE(non_empty);
    ASSERT_EQ(1, non_empty);

    butil::optional<int> empty_move(std::move(empty));
    ASSERT_FALSE(empty_move);

    butil::optional<int> non_empty_move(std::move(non_empty));
    ASSERT_TRUE(non_empty_move);
    ASSERT_EQ(1, non_empty_move);

    butil::optional<int> empty_move_assign;
    empty_move_assign = std::move(empty);
    ASSERT_FALSE(empty_move_assign);
}

struct Obj {};

struct Convert {
    Convert()
        :default_ctor(false), move_ctor(false) { }
    explicit Convert(const Obj&)
        :default_ctor(true), move_ctor(false) { }
    explicit Convert(Obj&&)
        :default_ctor(true), move_ctor(true) { }

    bool default_ctor;
    bool move_ctor;
};

struct ConvertFromOptional {
    ConvertFromOptional()
        :default_ctor(false), move_ctor(false), from_optional(false) { }
    ConvertFromOptional(const Obj&)
        :default_ctor(true), move_ctor(false), from_optional(false) { }
    ConvertFromOptional(Obj&&)
        :default_ctor(true), move_ctor(true), from_optional(false) { }
    ConvertFromOptional(
        const butil::optional<Obj>&)
        :default_ctor(true), move_ctor(false), from_optional(true) { }
    ConvertFromOptional(butil::optional<Obj>&&)
        :default_ctor(true), move_ctor(true), from_optional(true) { }

    bool default_ctor;
    bool move_ctor;
    bool from_optional;
};

TEST(OptionalTest, convert) {
    butil::optional<Obj> i_empty;
    ASSERT_FALSE(i_empty);
    butil::optional<Obj> i(butil::in_place);
    ASSERT_TRUE(i);
    {
        butil::optional<Convert> empty(i_empty);
        ASSERT_FALSE(empty);
        butil::optional<Convert> opt_copy(i);
        ASSERT_TRUE(opt_copy);
        ASSERT_TRUE(opt_copy->default_ctor);
        ASSERT_FALSE(opt_copy->move_ctor);

        butil::optional<Convert> opt_move(butil::optional<Obj>{ butil::in_place });
        ASSERT_TRUE(opt_move);
        ASSERT_TRUE(opt_move->default_ctor);
        ASSERT_TRUE(opt_move->move_ctor);
    }

    {
        static_assert(
            std::is_convertible<butil::optional<Obj>,
                                butil::optional<ConvertFromOptional>>::value,
            "");
        butil::optional<ConvertFromOptional> opt0 = i_empty;
        ASSERT_TRUE(opt0);
        ASSERT_TRUE(opt0->default_ctor);
        ASSERT_FALSE(opt0->move_ctor);
        ASSERT_TRUE(opt0->from_optional);
        butil::optional<ConvertFromOptional> opt1 = butil::optional<Obj>();
        ASSERT_TRUE(opt1);
        ASSERT_TRUE(opt1->default_ctor);
        ASSERT_TRUE(opt1->move_ctor);
        ASSERT_TRUE(opt1->from_optional);
    }
}

TEST(OptionalTest, value) {
    butil::optional<double> opt_empty;
    butil::optional<double> opt_double = 1.0;
    ASSERT_THROW(opt_empty.value(), butil::bad_optional_access);
    ASSERT_EQ(10.0, opt_empty.value_or(10));
    ASSERT_EQ(1.0, opt_double.value());
    ASSERT_EQ(1.0, opt_double.value_or(42));
    ASSERT_EQ(10.0, butil::optional<double>().value_or(10));
    ASSERT_EQ(1.0, butil::optional<double>(1).value_or(10));
}

TEST(OptionalTest, emplace) {
    butil::optional<std::string> opt_string;
    ASSERT_TRUE((std::is_same<std::string&, decltype(opt_string.emplace("abc"))>::value));
    std::string& str = opt_string.emplace("abc");
    ASSERT_EQ(&str, &opt_string.value());
}

TEST(OptionalTest, swap) {
    butil::optional<int> opt_empty, opt1 = 1, opt2 = 2;
    ASSERT_FALSE(opt_empty);
    ASSERT_TRUE(opt1);
    ASSERT_EQ(1, opt1.value());
    ASSERT_TRUE(opt2);
    ASSERT_EQ(2, opt2.value());
    swap(opt_empty, opt1);
    ASSERT_FALSE(opt1);
    ASSERT_TRUE(opt_empty);
    ASSERT_EQ(1, opt_empty.value());
    ASSERT_TRUE(opt2);
    ASSERT_EQ(2, opt2.value());
    swap(opt_empty, opt1);
    ASSERT_FALSE(opt_empty);
    ASSERT_TRUE(opt1);
    ASSERT_EQ(1, opt1.value());
    ASSERT_TRUE(opt2);
    ASSERT_EQ(2, opt2.value());
    swap(opt1, opt2);
    ASSERT_FALSE(opt_empty);
    ASSERT_TRUE(opt1);
    ASSERT_EQ(2, opt1.value());
    ASSERT_TRUE(opt2);
    ASSERT_EQ(1, opt2.value());

    ASSERT_TRUE(noexcept(opt1.swap(opt2)));
    ASSERT_TRUE(noexcept(swap(opt1, opt2)));
}

TEST(OptionalTest, make_optional) {
    auto opt_int = butil::make_optional(1);
    ASSERT_TRUE((std::is_same<decltype(opt_int), butil::optional<int>>::value));
    ASSERT_EQ(1, opt_int);
}

TEST(OptionalTest, comparison) {
    butil::optional<int> empty;
    butil::optional<int> one = 1;
    butil::optional<int> two = 2;
    ASSERT_TRUE(empty == empty);
    ASSERT_FALSE(empty == one);
    ASSERT_FALSE(empty == two);
    ASSERT_TRUE(empty == butil::nullopt);
    ASSERT_TRUE(one == one);
    ASSERT_FALSE(one == two);
    ASSERT_FALSE(one == butil::nullopt);
    ASSERT_TRUE(two == two);
    ASSERT_FALSE(two == butil::nullopt);

    ASSERT_FALSE(empty != empty);
    ASSERT_TRUE(empty != one);
    ASSERT_TRUE(empty != two);
    ASSERT_FALSE(empty != butil::nullopt);
    ASSERT_FALSE(one != one);
    ASSERT_TRUE(one != two);
    ASSERT_TRUE(one != butil::nullopt);
    ASSERT_FALSE(two != two);
    ASSERT_TRUE(two != butil::nullopt);

    ASSERT_FALSE(empty < empty);
    ASSERT_TRUE(empty < one);
    ASSERT_TRUE(empty < two);
    ASSERT_FALSE(empty < butil::nullopt);
    ASSERT_FALSE(one < one);
    ASSERT_TRUE(one < two);
    ASSERT_FALSE(one < butil::nullopt);
    ASSERT_FALSE(two < two);
    ASSERT_FALSE(two < butil::nullopt);

    ASSERT_TRUE(empty <= empty);
    ASSERT_TRUE(empty <= one);
    ASSERT_TRUE(empty <= two);
    ASSERT_TRUE(empty <= butil::nullopt);
    ASSERT_TRUE(one <= one);
    ASSERT_TRUE(one <= two);
    ASSERT_FALSE(one <= butil::nullopt);
    ASSERT_TRUE(two <= two);
    ASSERT_FALSE(two <= butil::nullopt);

    ASSERT_FALSE(empty > empty);
    ASSERT_FALSE(empty > one);
    ASSERT_FALSE(empty > two);
    ASSERT_FALSE(empty > butil::nullopt);
    ASSERT_FALSE(one > one);
    ASSERT_FALSE(one > two);
    ASSERT_TRUE(one > butil::nullopt);
    ASSERT_FALSE(two > two);
    ASSERT_TRUE(two > butil::nullopt);

    ASSERT_TRUE(empty >= empty);
    ASSERT_FALSE(empty >= one);
    ASSERT_FALSE(empty >= two);
    ASSERT_TRUE(empty >= butil::nullopt);
    ASSERT_TRUE(one >= one);
    ASSERT_FALSE(one >= two);
    ASSERT_TRUE(one >= butil::nullopt);
    ASSERT_TRUE(two >= two);
    ASSERT_TRUE(two >= butil::nullopt);
}

}  // namespace
