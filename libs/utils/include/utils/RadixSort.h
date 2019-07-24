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

#ifndef TNT_UTILS_RADIXSORT_H
#define TNT_UTILS_RADIXSORT_H

#include <utils/compiler.h>

#include <algorithm>
#include <type_traits>

#include <assert.h>
#include <stddef.h>


namespace utils {

template<typename Iterator, typename IndexType, size_t DIGIT, size_t RADIX_BITS = 8>
struct RadixSort {
    using value_t = typename std::iterator_traits<Iterator>::value_type;
    using index_t = IndexType;

    static constexpr size_t BUCKET_SIZE = 1u << RADIX_BITS;
    static constexpr size_t RADIX_SHIFT = (DIGIT - 1u) * RADIX_BITS;
    static constexpr value_t RADIX_MASK = value_t(BUCKET_SIZE - 1u) << RADIX_SHIFT;
    static constexpr size_t CACHE_LINE_OFFSET = 64u / sizeof(value_t);


    struct Bucket {
        index_t offset;
        index_t next;
    };

    static constexpr size_t getRadix(value_t v) {
        return (v & RADIX_MASK) >> RADIX_SHIFT;
    }

    static void radixSort(Iterator begin, Iterator end) {
        const size_t size = end - begin;
        if (size <= 128) {
            std::sort(begin, end);
            return;
        }

        Bucket buckets[BUCKET_SIZE] = {};
        for (Iterator it = begin; it != end; ++it) {
            size_t radix = getRadix(*it);
            ++buckets[radix].offset;
        }

        index_t total = 0;
        for (Bucket& bucket : buckets) {
            index_t count = bucket.offset;
            bucket.offset = total;
            total += count;
            bucket.next = total;
        }

        // in-place sort
        for (Bucket* p = buckets; p != buckets + BUCKET_SIZE && p->next != size; ++p) {
            Bucket& bucket = *p;
            const index_t next = bucket.next;
            while (bucket.offset != next) {
                index_t offset = bucket.offset;
                for (Iterator it = begin + offset, last = begin + next; it != last; ++it) {
                    value_t v = *it;
                    size_t radix = getRadix(v);
                    size_t index = buckets[radix].offset++;
                    value_t r = begin[index];
                    begin[index] = v;
                    *it = r;
                }
            }
        }

        size_t first = 0;
        for (Bucket bucket : buckets) {
            const size_t bucketSize = bucket.next - first;
            if (bucketSize > 1) {
                RadixSort<Iterator, IndexType, DIGIT - 1u, RADIX_BITS>::radixSort(
                        begin + first, begin + bucket.next);
                first = bucket.next;
            }
        }
    }
};


template<typename Iterator, typename IndexType, size_t RADIX_BITS>
struct RadixSort<Iterator, IndexType, 0u, RADIX_BITS> {
    static void radixSort(Iterator begin, Iterator end) { }
};

template<typename Iterator, typename IndexType = uint32_t>
void radix_sort(Iterator begin, Iterator end) {
    using value_t = typename std::iterator_traits<Iterator>::value_type;
    RadixSort<Iterator, IndexType, sizeof(value_t)>::radixSort(begin, end);
}


} // namespace utils

#endif //TNT_UTILS_RADIXSORT_H
