/*
 * Copyright 2021 Soni L.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef WABT_SET_UTIL_H_
#define WABT_SET_UTIL_H_

#include <set>
#include <vector>
#include <algorithm>
#include <iterator>

namespace wabt {

template <typename T>
bool SetsOverlap(const std::set<T>& set1, const std::set<T>& set2) {
  std::vector<T> vec;
  std::set_intersection(set1.begin(), set1.end(), set2.begin(), set2.end(), std::back_inserter(vec));
  return !vec.empty();
}

}  // namespace wabt

#endif  // WABT_SET_UTIL_H_

