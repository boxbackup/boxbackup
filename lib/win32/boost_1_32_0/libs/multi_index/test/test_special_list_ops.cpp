/* Boost.MultiIndex test for special list operations.
 *
 * Copyright 2003-2004 Joaquín M López Muñoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See http://www.boost.org/libs/multi_index for library home page.
 */

#include "test_special_list_ops.hpp"

#include <boost/config.hpp> /* keep it first to prevent nasty warns in MSVC */
#include <algorithm>
#include <iterator>
#include "pre_multi_index.hpp"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/test/test_tools.hpp>

using namespace boost::multi_index;

#undef _
#define _ ,

#undef CHECK_EQUAL
#define CHECK_EQUAL(p,check_range) \
{\
  int v[]=check_range;\
  std::size_t size_v=sizeof(v)/sizeof(int);\
  BOOST_CHECK(std::size_t(std::distance((p).begin(),(p).end()))==size_v);\
  BOOST_CHECK(std::equal((p).begin(),(p).end(),v));\
}

#undef CHECK_VOID_RANGE
#define CHECK_VOID_RANGE(p) BOOST_CHECK((p).first==(p).second)

void test_special_list_ops()
{
  typedef multi_index_container<
    int,
    indexed_by<
      sequenced<>
    >
  > sequenced_container;

  sequenced_container sc;
  sc.push_back(0);
  sc.push_back(1);
  sc.push_back(2);
  sc.push_back(3);
  sc.push_back(4);
  sc.push_back(5);

  sequenced_container::iterator it;

  it=sc.begin();
  std::advance(it,3);
  sc.relocate(sc.begin(),it);
  CHECK_EQUAL(sc,{3 _ 0 _ 1 _ 2 _ 4 _ 5});
  BOOST_CHECK(it==sc.begin());

  sc.relocate(it,it);
  CHECK_EQUAL(sc,{3 _ 0 _ 1 _ 2 _ 4 _ 5});

  std::advance(it,3);
  sc.relocate(sc.end(),it,sc.end());
  CHECK_EQUAL(sc,{3 _ 0 _ 1 _ 2 _ 4 _ 5});

  sc.relocate(sc.begin(),it,it);
  CHECK_EQUAL(sc,{3 _ 0 _ 1 _ 2 _ 4 _ 5});

  sequenced_container::iterator it2;

  it2=sc.begin();
  ++it2;
  sc.relocate(it2,it,sc.end());
  CHECK_EQUAL(sc,{3 _ 2 _ 4 _ 5 _ 0 _ 1});
  BOOST_CHECK(std::distance(it,it2)==3);

  sc.relocate(--(sc.end()),it,it2);
  CHECK_EQUAL(sc,{3 _ 0 _ 2 _ 4 _ 5 _ 1});
}
