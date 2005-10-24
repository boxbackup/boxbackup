/* Boost.MultiIndex test for modifier memfuns.
 *
 * Copyright 2003-2004 Joaquín M López Muñoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See http://www.boost.org/libs/multi_index for library home page.
 */

#include "test_modifiers.hpp"

#include <boost/config.hpp> /* keep it first to prevent nasty warns in MSVC */
#include <vector>
#include "pre_multi_index.hpp"
#include "employee.hpp"
#include <boost/test/test_tools.hpp>

using namespace boost::multi_index;

void test_modifiers()
{
  employee_set              es;
  employee_set_by_age&      i2=get<age>(es);
  employee_set_as_inserted& i3=get<as_inserted>(es);

  es.insert(employee(0,"Joe",31));
  BOOST_CHECK(es.insert(employee(0,"Joe",31)).second==false);
  BOOST_CHECK(i2.insert(employee(0,"Joe Jr.",5)).second==false);
  BOOST_CHECK(i3.insert(i3.begin(),employee(0,"Joe Jr.",5)).second==false);
  BOOST_CHECK(i3.push_front(employee(0,"Joe Jr.",5)).second==false);
  BOOST_CHECK(i3.push_back(employee(0,"Joe Jr.",5)).second==false);

  employee_set_by_age::iterator it=i2.find(31);
  i2.insert(it,employee(1,"Joe Jr.",5));
  BOOST_CHECK(es.size()==2);

  employee_set_as_inserted::iterator it2=i3.begin();
  i3.insert(it2,100,employee(2,"Grandda Joe",64));
  BOOST_CHECK(es.size()==3);

  es.erase(employee(1,"Joe Jr.",5));
  BOOST_CHECK(i2.size()==2&&i3.size()==2);

  i2.erase(it);
  BOOST_CHECK(es.size()==1&&i3.size()==1);

  i3.pop_front();
  BOOST_CHECK(es.size()==0&&i2.size()==0);

  es.insert(employee(0,"Joe",31));
  es.insert(employee(1,"Jack",31));
  i2.erase(31);
  BOOST_CHECK(i2.size()==0);

  i3.push_front(employee(1,"Jack",31));
  i3.push_back(employee(0,"Joe",31));
  BOOST_CHECK(i3.front()==employee(1,"Jack",31));
  BOOST_CHECK(i3.back()==employee(0,"Joe",31));

  i3.pop_back();
  BOOST_CHECK(i3.back()==employee(1,"Jack",31));
  BOOST_CHECK(es.size()==1);

  i3.pop_front();
  BOOST_CHECK(es.size()==0);

  std::vector<employee> ve;
  ve.push_back(employee(3,"Anna",31));
  ve.push_back(employee(1,"Rachel",27));
  ve.push_back(employee(2,"Agatha",40));

  i2.insert(ve.begin(),ve.end());
  BOOST_CHECK(i3.size()==3);

  BOOST_CHECK(*(i3.erase(i3.begin()))==employee(1,"Rachel",27));
  BOOST_CHECK(i3.erase(i3.begin(),i3.end())==i3.end());
  BOOST_CHECK(es.size()==0);

  i3.insert(i3.end(),ve.begin(),ve.end());
  BOOST_CHECK(es.size()==3);

  es.erase(es.begin(),es.end());
  BOOST_CHECK(i2.size()==0);

  es.insert(employee(0,"Joe",31));
  es.insert(employee(1,"Robert",27));
  es.insert(employee(2,"John",40));
  es.insert(employee(3,"Albert",20));
  es.insert(employee(4,"John",57));

  employee_set es_backup(es);

  employee_set es2;
  es2.insert(employee(3,"Anna",31));
  es2.insert(employee(1,"Rachel",27));
  es2.insert(employee(2,"Agatha",40));

  employee_set es2_backup(es2);

  i2.swap(get<2>(es2));
  BOOST_CHECK(es==es2_backup&&es2==es_backup);

  i3.swap(get<3>(es2));
  BOOST_CHECK(es==es_backup&&es2==es2_backup);

#if defined(BOOST_FUNCTION_SCOPE_USING_DECLARATION_BREAKS_ADL)
  ::boost::multi_index::detail::swap(i2,get<2>(es2));
#else
  using std::swap;
  swap(i2,get<2>(es2));
#endif

  BOOST_CHECK(es==es2_backup&&es2==es_backup);

#if defined(BOOST_FUNCTION_SCOPE_USING_DECLARATION_BREAKS_ADL)
  ::boost::multi_index::detail::swap(i3,get<3>(es2));
#else
  using std::swap;
  swap(i3,get<3>(es2));
#endif

  BOOST_CHECK(es==es_backup&&es2==es2_backup);

  i3.clear();
  BOOST_CHECK(i3.size()==0);

  es2.clear();
  BOOST_CHECK(es2.size()==0);
}
