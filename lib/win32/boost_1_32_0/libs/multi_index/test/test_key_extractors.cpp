/* Boost.MultiIndex test for key extractors.
 *
 * Copyright 2003-2004 Joaquín M López Muñoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See http://www.boost.org/libs/multi_index for library home page.
 */

#include "test_key_extractors.hpp"

#include <boost/config.hpp> /* keep it first to prevent nasty warns in MSVC */
#include "pre_multi_index.hpp"
#include <boost/multi_index/key_extractors.hpp>
#include <boost/ref.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/test/test_tools.hpp>
#include <list>
#include <memory>

using namespace boost::multi_index;
using namespace boost::tuples;

struct test_class
{
  int       int_member;
  const int int_cmember;

  bool bool_mem_fun_const()const{return true;}
  bool bool_mem_fun(){return false;}

  test_class(int i=0):int_member(i),int_cmember(i){}
  test_class(int i,int j):int_member(i),int_cmember(j){}

  test_class& operator=(const test_class& x)
  {
    int_member=x.int_member;
    return *this;
  }

  bool operator<(const test_class& x)const
  {
    if(int_member<x.int_member)return true;
    if(x.int_member<int_member)return false;
    return int_cmember<x.int_cmember;
  }

  bool operator==(const test_class& x)const
  {
    return int_member==x.int_member&&int_cmember==x.int_cmember;
  }
};

BOOST_BROKEN_COMPILER_TYPE_TRAITS_SPECIALIZATION(test_class)

typedef identity<test_class>                                       idn;
typedef identity<const test_class>                                 cidn;
typedef BOOST_MULTI_INDEX_MEMBER(test_class,int,int_member)        key_m;
typedef BOOST_MULTI_INDEX_MEMBER(test_class,const int,int_member)  ckey_m;
typedef BOOST_MULTI_INDEX_MEMBER(test_class,const int,int_cmember) key_cm;
typedef BOOST_MULTI_INDEX_CONST_MEM_FUN(
          test_class,bool,bool_mem_fun_const)                      key_cmf;
typedef BOOST_MULTI_INDEX_MEM_FUN(test_class,bool,bool_mem_fun)    key_mf;
typedef composite_key<
          test_class,
          idn,
          key_m,
          key_cm,
          key_cmf
        >                                                          compkey;
typedef composite_key<
          test_class,
          cidn,
          ckey_m
        >                                                          ccompkey;
typedef composite_key<
          boost::reference_wrapper<test_class>,
          key_mf
          >                                                        ccompw_key;

void test_key_extractors()
{
  idn        id;
  cidn       cid;
  key_m      k_m;
  ckey_m     ck_m;
  key_cm     k_cm;
  key_cmf    k_cmf;
  key_mf     k_mf;
  compkey    cmpk;
  ccompkey   ccmpk;
  ccompw_key ccmpk_w;

  test_class                                 t;
  const test_class&                          ctr=t;

  test_class*                                tp=&t;
  const test_class*                          ctp=&t;

  test_class**                               tpp=&tp;
  const test_class**                         ctpp=&ctp;

  std::auto_ptr<test_class*>                 tap(new test_class*(tp));
  std::auto_ptr<const test_class*>           ctap(new const test_class*(ctp));

  boost::reference_wrapper<test_class>       tw(t);
  boost::reference_wrapper<const test_class> ctw(t);

  id(t).int_member=0;
  BOOST_CHECK(id(t).int_member==0);
  BOOST_CHECK(cid(t).int_member==0);
  BOOST_CHECK(k_m(t)==0);
  BOOST_CHECK(ck_m(t)==0);
  BOOST_CHECK(cmpk(t)==make_tuple(test_class(0,0),0,0,true));
  BOOST_CHECK(ccmpk(t)==make_tuple(test_class(0,0),0));
  BOOST_CHECK(id(ctr).int_member==0);
  BOOST_CHECK(cid(ctr).int_member==0);
  BOOST_CHECK(k_m(ctr)==0);
  BOOST_CHECK(ck_m(ctr)==0);
  BOOST_CHECK(cmpk(ctr)==make_tuple(test_class(0,0),0,0,true));
  BOOST_CHECK(ccmpk(ctr)==make_tuple(test_class(0,0),0));

  k_m(t)=1;
  BOOST_CHECK(id(tp).int_member==1);
  BOOST_CHECK(cid(tp).int_member==1);
  BOOST_CHECK(k_m(tp)==1);
  BOOST_CHECK(ck_m(tp)==1);
  BOOST_CHECK(cmpk(tp)==make_tuple(test_class(1,0),1,0,true));
  BOOST_CHECK(ccmpk(tp)==make_tuple(test_class(1,0),1));
  BOOST_CHECK(cid(ctp).int_member==1);
  BOOST_CHECK(ck_m(ctp)==1);
  BOOST_CHECK(cmpk(ctp)==make_tuple(test_class(1,0),1,0,true));
  BOOST_CHECK(ccmpk(ctp)==make_tuple(test_class(1,0),1));

  k_m(tp)=2;
  BOOST_CHECK(id(tpp).int_member==2);
  BOOST_CHECK(cid(tpp).int_member==2);
  BOOST_CHECK(k_m(tpp)==2);
  BOOST_CHECK(ck_m(tpp)==2);
  BOOST_CHECK(cmpk(tpp)==make_tuple(test_class(2,0),2,0,true));
  BOOST_CHECK(ccmpk(tpp)==make_tuple(test_class(2,0),2));
  BOOST_CHECK(cid(ctpp).int_member==2);
  BOOST_CHECK(ck_m(ctpp)==2);
  BOOST_CHECK(cmpk(ctpp)==make_tuple(test_class(2,0),2,0,true));
  BOOST_CHECK(ccmpk(ctpp)==make_tuple(test_class(2,0),2));

  k_m(tpp)=3;
  BOOST_CHECK(id(tap).int_member==3);
  BOOST_CHECK(cid(tap).int_member==3);
  BOOST_CHECK(k_m(tap)==3);
  BOOST_CHECK(ck_m(tap)==3);
  BOOST_CHECK(cmpk(tap)==make_tuple(test_class(3,0),3,0,true));
  BOOST_CHECK(ccmpk(tap)==make_tuple(test_class(3,0),3));
  BOOST_CHECK(cid(ctap).int_member==3);
  BOOST_CHECK(ck_m(ctap)==3);
  BOOST_CHECK(cmpk(ctap)==make_tuple(test_class(3,0),3,0,true));
  BOOST_CHECK(ccmpk(ctap)==make_tuple(test_class(3,0),3));

  k_m(tap)=4;
  BOOST_CHECK(id(tw).int_member==4);
  BOOST_CHECK(cid(tw).int_member==4);
  BOOST_CHECK(k_m(tw)==4);
  BOOST_CHECK(ck_m(tw)==4);
  BOOST_CHECK(cmpk(tw)==make_tuple(test_class(4,0),4,0,true));
  BOOST_CHECK(ccmpk(tw)==make_tuple(test_class(4,0),4));

  k_m(tw)=5;
  BOOST_CHECK(id(ctw).int_member==5);
  BOOST_CHECK(cid(ctw).int_member==5);
  BOOST_CHECK(k_m(ctw)==5);
  BOOST_CHECK(ck_m(ctw)==5);
  BOOST_CHECK(cmpk(ctw)==make_tuple(test_class(5,0),5,0,true));
  BOOST_CHECK(ccmpk(ctw)==make_tuple(test_class(5,0),5));

  BOOST_CHECK(k_cm(t)==0);

  BOOST_CHECK(k_cm(tp)==0);
  BOOST_CHECK(k_cm(ctp)==0);
  BOOST_CHECK(k_cm(tpp)==0);
  BOOST_CHECK(k_cm(ctpp)==0);
  BOOST_CHECK(k_cm(tap)==0);
  BOOST_CHECK(k_cm(ctap)==0);

  BOOST_CHECK(k_cm(tw)==0);
  BOOST_CHECK(k_cm(ctw)==0);

  BOOST_CHECK(k_cmf(t));

  BOOST_CHECK(k_cmf(tp));
  BOOST_CHECK(k_cmf(ctp));
  BOOST_CHECK(k_cmf(tpp));
  BOOST_CHECK(k_cmf(ctpp));
  BOOST_CHECK(k_cmf(tap));
  BOOST_CHECK(k_cmf(ctap));

  BOOST_CHECK(k_cmf(tw));
  BOOST_CHECK(k_cmf(ctw));

  BOOST_CHECK(!k_mf(t));

  BOOST_CHECK(!k_mf(tp));
  BOOST_CHECK(!k_mf(tpp));
  BOOST_CHECK(!k_mf(tap));
  BOOST_CHECK(!k_mf(tw));
  BOOST_CHECK(ccmpk_w(tw)==make_tuple(false));
  
  std::list<test_class> tl;
  for(int i=0;i<20;++i)tl.push_back(test_class(i));

  int j=0;
  for(std::list<test_class>::iterator it=tl.begin();it!=tl.end();++it){
    BOOST_CHECK(k_m(it)==j);
    BOOST_CHECK(k_cm(it)==j);
    BOOST_CHECK(k_cmf(it));
    BOOST_CHECK(!k_mf(it));
    BOOST_CHECK(cmpk(it)==make_tuple(test_class(j),j,j,true));
    BOOST_CHECK(ccmpk(it)==make_tuple(test_class(j),j));
    ++j;
  }
}
