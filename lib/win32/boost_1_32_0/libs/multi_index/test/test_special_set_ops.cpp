/* Boost.MultiIndex test for special set operations.
 *
 * Copyright 2003-2004 Joaquín M López Muñoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See http://www.boost.org/libs/multi_index for library home page.
 */

#include "test_special_set_ops.hpp"

#include <boost/config.hpp> /* keep it first to prevent nasty warns in MSVC */
#include <algorithm>
#include "pre_multi_index.hpp"
#include "employee.hpp"
#include <boost/test/test_tools.hpp>

using namespace boost::multi_index;

struct comp_initial
{
  bool operator()(char ch,const std::string& s)const
  {
    if(s.empty())return false;
    return ch<s[0];
  }

  bool operator()(const std::string& s,char ch)const
  {
    if(s.empty())return true;
    return s[0]<ch;
  }
};

void test_special_set_ops()
{
  employee_set es;

  es.insert(employee(0,"Joe",31));
  es.insert(employee(1,"Robert",27));
  es.insert(employee(2,"John",40));
  es.insert(employee(3,"Albert",20));
  es.insert(employee(4,"John",57));

  BOOST_CHECK(std::distance(
    get<name>(es).lower_bound('J',comp_initial()),
    get<name>(es).upper_bound('J',comp_initial()))==3);

  BOOST_CHECK(get<name>(es).find('A',comp_initial())->name[0]=='A');

  BOOST_CHECK(
    std::distance(
      get<age>(es).lower_bound(27),
      get<age>(es).upper_bound(40))==3);

  BOOST_CHECK(es.count(2,employee::comp_id())==1);
  BOOST_CHECK(es.count(5,employee::comp_id())==0);
}
