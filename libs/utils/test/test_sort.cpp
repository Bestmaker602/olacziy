/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <utils/RadixSort.h>

#include <random>

using namespace utils;


TEST(SortTest, radix) {

    constexpr size_t SIZE = 4096;
    uint32_t data[SIZE];

    std::default_random_engine r(0);
    //std::uniform_int_distribution<int> ur(1, 6);
    std::generate(std::begin(data), std::end(data), [&](){ return r(); });

    ASSERT_FALSE(std::is_sorted(std::begin(data), std::end(data)));

    radix_sort(data, data + SIZE);
    EXPECT_TRUE(std::is_sorted(std::begin(data), std::end(data)));

    radix_sort(data, data + SIZE);
    EXPECT_TRUE(std::is_sorted(std::begin(data), std::end(data)));
}
