/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "PerformanceCounters.h"

#include <utils/algorithm.h>
#include <utils/compiler.h>
#include <utils/RadixSort.h>

#include <benchmark/benchmark.h>

#include <random>
#include <vector>

using namespace utils;

using value_type = uint64_t;

static void BM_sort(benchmark::State& state) {
    std::vector<value_type> data;
    std::default_random_engine gen{123};
    std::uniform_int_distribution<value_type> nd;
    data.resize(state.range(0));
    std::generate(data.begin(), data.end(), [&](){ return nd(gen); });

    {
        PerformanceCounters pc(state);
        for (auto _ : state) {
            std::sort(data.begin(), data.end());
        }
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}

static void BM_sort_radix(benchmark::State& state) {
    std::vector<value_type> data;
    std::default_random_engine gen{123};
    std::uniform_int_distribution<value_type> nd;
    data.resize(state.range(0));
    std::generate(data.begin(), data.end(), [&](){ return nd(gen); });

    {
        PerformanceCounters pc(state);
        for (auto _ : state) {
            radix_sort(data.data(), data.data() + data.size());
        }
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}

BENCHMARK(BM_sort)->Range(8, 8<<20);
BENCHMARK(BM_sort_radix)->Range(8, 8<<20);
