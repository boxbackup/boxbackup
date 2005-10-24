// (C) Copyright Jeremy Siek 2000.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <list>
#include <algorithm>

int main() {
  std::list<int> v;
  std::stable_sort(v.begin(), v.end());
  return 0;
}
