/* Boost.MultiIndex test for projection capabilities.
 *
 * Copyright 2003-2004 Joaquín M López Muñoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See http://www.boost.org/libs/multi_index for library home page.
 */

#include "test_projection.hpp"

#include <boost/config.hpp> /* keep it first to prevent nasty warns in MSVC */
#include "pre_multi_index.hpp"
#include "employee.hpp"
#include <boost/test/test_tools.hpp>

using namespace boost::multi_index;

void test_projection()
{
  employee_set          es;
  es.insert(employee(0,"Joe",31));
  es.insert(employee(1,"Robert",27));
  es.insert(employee(2,"John",40));
  es.insert(employee(3,"Albert",20));
  es.insert(employee(4,"John",57));

  employee_set::iterator             it,itbis;
  employee_set_by_name::iterator     it1;
  employee_set_by_age::iterator      it2;
  employee_set_as_inserted::iterator it3;

  BOOST_STATIC_ASSERT((boost::is_same<
    employee_set::iterator,
    nth_index_iterator<employee_set,0>::type >::value));
  BOOST_STATIC_ASSERT((boost::is_same<
    employee_set_by_name::iterator,
    nth_index_iterator<employee_set,1>::type >::value));
#if defined(BOOST_NO_MEMBER_TEMPLATES)
  BOOST_STATIC_ASSERT((boost::is_same<
    employee_set_by_age::iterator,
    index_iterator<employee_set,age>::type >::value));
#else
  BOOST_STATIC_ASSERT((boost::is_same<
    employee_set_by_age::iterator,
    employee_set::index_iterator<age>::type >::value));
#endif
  BOOST_STATIC_ASSERT((boost::is_same<
    employee_set_as_inserted::iterator,
    nth_index_iterator<employee_set,3>::type >::value));

  it=   es.find(employee(1,"Robert",27));
  it1=  project<name>(es,it);
  it2=  project<age>(es,it1);
  it3=  project<as_inserted>(es,it2);
#if defined(BOOST_NO_MEMBER_TEMPLATES)
  itbis=project<0>(es,it3);
#else
  itbis=es.project<0>(it3);
#endif

  BOOST_CHECK(*it==*it1&&*it1==*it2&&*it2==*it3&&itbis==it);

  const employee_set& ces=es;

  employee_set::const_iterator             cit,citbis;
  employee_set_by_name::const_iterator     cit1;
  employee_set_by_age::const_iterator      cit2;
  employee_set_as_inserted::const_iterator cit3;

  BOOST_STATIC_ASSERT((boost::is_same<
    employee_set::const_iterator,
    nth_index_const_iterator<employee_set,0>::type >::value));
  BOOST_STATIC_ASSERT((boost::is_same<
    employee_set_by_name::const_iterator,
    nth_index_const_iterator<employee_set,1>::type >::value));
#if defined(BOOST_NO_MEMBER_TEMPLATES)
  BOOST_STATIC_ASSERT((boost::is_same<
    employee_set_by_age::const_iterator,
    index_const_iterator<employee_set,age>::type >::value));
#else
  BOOST_STATIC_ASSERT((boost::is_same<
    employee_set_by_age::const_iterator,
    employee_set::index_const_iterator<age>::type >::value));
#endif
  BOOST_STATIC_ASSERT((boost::is_same<
    employee_set_as_inserted::const_iterator,
    nth_index_const_iterator<employee_set,3>::type >::value));

  BOOST_CHECK(project<age>(es,es.end())==get<age>(es).end());
  BOOST_CHECK(project<as_inserted>(es,es.end())==get<as_inserted>(es).end());

  cit=   ces.find(employee(4,"John",57));
#if defined(BOOST_NO_MEMBER_TEMPLATES)
  cit1=  project<by_name>(ces,cit);
#else
  cit1=  ces.project<by_name>(cit);
#endif
  cit2=  project<age>(ces,cit1);
#if defined(BOOST_NO_MEMBER_TEMPLATES)
  cit3=  project<as_inserted>(ces,cit2);
#else
  cit3=  ces.project<as_inserted>(cit2);
#endif
  citbis=project<0>(ces,cit3);

  BOOST_CHECK(*cit==*cit1&&*cit1==*cit2&&*cit2==*cit3&&citbis==cit);
}
