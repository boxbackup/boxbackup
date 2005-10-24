/* Used in Boost.MultiIndex tests.
 *
 * Copyright 2003-2004 Joaquín M López Muñoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See http://www.boost.org/libs/multi_index for library home page.
 */

#ifndef BOOST_MULTI_INDEX_TEST_EMPLOYEE_HPP
#define BOOST_MULTI_INDEX_TEST_EMPLOYEE_HPP

#include <boost/config.hpp> /* keep it first to prevent nasty warns in MSVC */
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <cstddef>
#include <ostream>
#include <string>

struct employee
{
  int         id;
  std::string name;
  int         age;

  employee(int id_,std::string name_,int age_):id(id_),name(name_),age(age_){}

  bool operator==(const employee& x)const
  {
    return id==x.id&&name==x.name&&age==x.age;
  }

  bool operator<(const employee& x)const
  {
    return id<x.id;
  }

  bool operator!=(const employee& x)const{return !(*this==x);}
  bool operator> (const employee& x)const{return x<*this;}
  bool operator>=(const employee& x)const{return !(*this<x);}
  bool operator<=(const employee& x)const{return !(x<*this);}

  struct comp_id
  {
    bool operator()(int x,const employee& e2)const{return x<e2.id;}
    bool operator()(const employee& e1,int x)const{return e1.id<x;}
  };

  friend std::ostream& operator<<(std::ostream& os,const employee& e)
  {
    os<<e.id<<" "<<e.name<<" "<<e.age<<std::endl;
    return os;
  }
};

struct name{};
struct by_name{};
struct age{};
struct as_inserted{};

typedef
  boost::multi_index::multi_index_container<
    employee,
    boost::multi_index::indexed_by<
      boost::multi_index::ordered_unique<
        boost::multi_index::identity<employee> >,
      boost::multi_index::ordered_non_unique<
        boost::multi_index::tag<name,by_name>,
        BOOST_MULTI_INDEX_MEMBER(employee,std::string,name)>,
      boost::multi_index::ordered_non_unique<
        boost::multi_index::tag<age>,
        BOOST_MULTI_INDEX_MEMBER(employee,int,age)>,
      boost::multi_index::sequenced<
        boost::multi_index::tag<as_inserted> > > >
  employee_set;

#if defined(BOOST_NO_MEMBER_TEMPLATES)
typedef boost::multi_index::nth_index<
  employee_set,1>::type                       employee_set_by_name;
#else
typedef employee_set::nth_index<1>::type employee_set_by_name;
#endif

typedef boost::multi_index::index<
         employee_set,age>::type         employee_set_by_age;
typedef boost::multi_index::index<
         employee_set,as_inserted>::type employee_set_as_inserted;

#endif
