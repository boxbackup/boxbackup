/* Used in Boost.MultiIndex tests.
 *
 * Copyright 2003-2004 Joaquín M López Muñoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See http://www.boost.org/libs/multi_index for library home page.
 */

#ifndef BOOST_MULTI_INDEX_TEST_PAIR_OF_INTS_HPP
#define BOOST_MULTI_INDEX_TEST_PAIR_OF_INTS_HPP

#include <boost/config.hpp> /* keep it first to prevent nasty warns in MSVC */
#include <utility>

typedef std::pair<int,int> pair_of_ints;

inline void increment_first(pair_of_ints& p)
{
  ++p.first;
}

inline void increment_second(pair_of_ints& p)
{
  ++p.second;
}

inline void increment_int(int& x)
{
  ++x;
}

#endif
