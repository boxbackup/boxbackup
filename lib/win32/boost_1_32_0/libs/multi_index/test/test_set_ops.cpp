/* Boost.MultiIndex test for standard set operations.
 *
 * Copyright 2003-2004 Joaquín M López Muñoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See http://www.boost.org/libs/multi_index for library home page.
 */

#include "test_set_ops.hpp"

#include <boost/config.hpp> /* keep it first to prevent nasty warns in MSVC */
#include <algorithm>
#include <vector>
#include "pre_multi_index.hpp"
#include "employee.hpp"
#include <boost/test/test_tools.hpp>

using namespace boost::multi_index;

void test_set_ops()
{
  employee_set es;
  employee_set_by_name& i1=get<by_name>(es);
  const employee_set_by_age& i2=get<age>(es);

  es.insert(employee(0,"Joe",31));
  es.insert(employee(1,"Robert",27));
  es.insert(employee(2,"John",40));
  es.insert(employee(3,"Albert",20));
  es.insert(employee(4,"John",57));

  BOOST_CHECK(i1.find("John")->name=="John");
  BOOST_CHECK(i2.find(41)==i2.end());

  BOOST_CHECK(i1.count("John")==2);
  BOOST_CHECK(es.count(employee(10,"",-1))==0);

  BOOST_CHECK(i1.lower_bound("John")->name=="John");

  BOOST_CHECK(
    std::distance(
      i2.lower_bound(31),
      i2.upper_bound(60))==3);

  std::pair<employee_set_by_name::iterator,employee_set_by_name::iterator> p=
    i1.equal_range("John");
  BOOST_CHECK(std::distance(p.first,p.second)==2);
}
