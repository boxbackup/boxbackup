/* Boost.MultiIndex test for composite_key.
 *
 * Copyright 2003-2004 Joaquín M López Muñoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See http://www.boost.org/libs/multi_index for library home page.
 */

#include "test_composite_key.hpp"

#include <boost/config.hpp> /* keep it first to prevent nasty warns in MSVC */
#include "pre_multi_index.hpp"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/test/test_tools.hpp>

using namespace boost::multi_index;
using namespace boost::tuples;

template<typename T>
struct is_composite_key_result_helper
{
  typedef char yes;
  struct no{char m[2];};

  static no test(void*);

  template<typename CompositeKey>
  static yes test(composite_key_result<CompositeKey>*);
};

template<typename T>
struct is_composite_key_result
{
  typedef is_composite_key_result_helper<T> helper;

  BOOST_STATIC_CONSTANT(bool,
    value=(
      sizeof(helper::test((T*)0))==
      sizeof(typename helper::yes)));
};

template<typename CompositeKeyResult>
struct composite_key_result_length
{
  BOOST_STATIC_CONSTANT(int,
    value=boost::tuples::length<
      BOOST_DEDUCED_TYPENAME 
      CompositeKeyResult::composite_key_type::key_extractor_tuple
    >::value);
};

template<typename T>
struct composite_object_length
{
  typedef typename boost::mpl::if_c<
    is_composite_key_result<T>::value,
    composite_key_result_length<T>,
    boost::tuples::length<T>
  >::type type;

  BOOST_STATIC_CONSTANT(int,value=type::value);
};

template<typename CompositeKeyResult,typename T2>
struct comparison_equal_length
{
  static bool is_less(const CompositeKeyResult& x,const T2& y)
  {
    composite_key_result_less<CompositeKeyResult>    lt;
    composite_key_result_greater<CompositeKeyResult> gt;

#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
    std::less<CompositeKeyResult>    std_lt;
    std::greater<CompositeKeyResult> std_gt;
#endif

    return  (x< y) && !(y< x)&&
           !(x==y) && !(y==x)&&
            (x!=y) &&  (y!=x)&&
           !(x> y) &&  (y> x)&&
           !(x>=y) &&  (y>=x)&&
            (x<=y) && !(y<=x)&&

           lt(x,y) && !lt(y,x)&&
          !gt(x,y) &&  gt(y,x)

#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
                   &&
       std_lt(x,y) && !std_lt(y,x)&&
      !std_gt(x,y) &&  std_gt(y,x)
#endif
                    ;
  }

  static bool is_greater(const CompositeKeyResult& x,const T2& y)
  {
    composite_key_result_less<CompositeKeyResult>    lt;
    composite_key_result_greater<CompositeKeyResult> gt;

#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
    std::less<CompositeKeyResult>    std_lt;
    std::greater<CompositeKeyResult> std_gt;
#endif

    return !(x< y) &&  (y< x)&&
           !(x==y) && !(y==x)&&
            (x!=y) &&  (y!=x)&&
            (x> y) && !(y> x)&&
            (x>=y) && !(y>=x)&&
           !(x<=y) &&  (y<=x)&&

          !lt(x,y) &&  lt(y,x)&&
           gt(x,y) && !gt(y,x)

#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
                   &&
      !std_lt(x,y) && std_lt(y,x)&&
       std_gt(x,y) && !std_gt(y,x)
#endif
                    ;
  }

  static bool is_equiv(const CompositeKeyResult& x,const T2& y)
  {
    composite_key_result_less<CompositeKeyResult>    lt;
    composite_key_result_greater<CompositeKeyResult> gt;

#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
    std::less<CompositeKeyResult>    std_lt;
    std::greater<CompositeKeyResult> std_gt;
#endif

    return !(x< y) && !(y< x)&&
            (x==y) &&  (y==x)&&
           !(x!=y) && !(y!=x)&&
           !(x> y) && !(y> x)&&
            (x>=y) &&  (y>=x)&&
            (x<=y) &&  (y<=x)&&

          !lt(x,y) && !lt(y,x)&&
          !gt(x,y) && !gt(y,x)

#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
                   &&
      !std_lt(x,y) && !std_lt(y,x)&&
      !std_gt(x,y) && !std_gt(y,x)
#endif
                    ;
  }
};

template<typename CompositeKeyResult,typename T2>
struct comparison_different_length
{
  static bool is_less(const CompositeKeyResult& x,const T2& y)
  {
    composite_key_result_less<CompositeKeyResult>    lt;
    composite_key_result_greater<CompositeKeyResult> gt;

#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
    std::less<CompositeKeyResult>    std_lt;
    std::greater<CompositeKeyResult> std_gt;
#endif

    return  (x< y) && !(y< x)&&
           !(x> y) &&  (y> x)&&
           !(x>=y) &&  (y>=x)&&
            (x<=y) && !(y<=x)&&

           lt(x,y) && !lt(y,x)&&
          !gt(x,y) &&  gt(y,x)
          
#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
                   &&
       std_lt(x,y) && !std_lt(y,x)&&
      !std_gt(x,y) &&  std_gt(y,x)
#endif
                    ;
  }

  static bool is_greater(const CompositeKeyResult& x,const T2& y)
  {
    composite_key_result_less<CompositeKeyResult>    lt;
    composite_key_result_greater<CompositeKeyResult> gt;

#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
    std::less<CompositeKeyResult>    std_lt;
    std::greater<CompositeKeyResult> std_gt;
#endif

    return !(x< y) &&  (y< x)&&
            (x> y) && !(y> x)&&
            (x>=y) && !(y>=x)&&
           !(x<=y) &&  (y<=x)&&

          !lt(x,y) &&  lt(y,x)&&
           gt(x,y) && !gt(y,x)

#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
                   &&
      !std_lt(x,y) && std_lt(y,x)&&
       std_gt(x,y) && !std_gt(y,x)
#endif
                    ;
  }

  static bool is_equiv(const CompositeKeyResult& x,const T2& y)
  {
    composite_key_result_less<CompositeKeyResult>    lt;
    composite_key_result_greater<CompositeKeyResult> gt;

#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
    std::less<CompositeKeyResult>    std_lt;
    std::greater<CompositeKeyResult> std_gt;
#endif

    return !(x< y) && !(y< x)&&
           !(x> y) && !(y> x)&&
            (x>=y) &&  (y>=x)&&
            (x<=y) &&  (y<=x)&&

          !lt(x,y) && !lt(y,x)&&
          !gt(x,y) && !gt(y,x)

#if !defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
                   &&
      !std_lt(x,y) && !std_lt(y,x)&&
      !std_gt(x,y) && !std_gt(y,x)
#endif
                    ;
  }
};

template<typename CompositeKeyResult,typename T2>
struct comparison_helper:
  boost::mpl::if_c<
    composite_key_result_length<CompositeKeyResult>::value==
      composite_object_length<T2>::value,
    comparison_equal_length<CompositeKeyResult,T2>,
    comparison_different_length<CompositeKeyResult,T2>
  >::type
{
};

template<typename CompositeKeyResult,typename T2>
static bool is_less(const CompositeKeyResult& x,const T2& y)
{
  return comparison_helper<CompositeKeyResult,T2>::is_less(x,y);
}

template<typename CompositeKeyResult,typename T2>
static bool is_greater(const CompositeKeyResult& x,const T2& y)
{
  return comparison_helper<CompositeKeyResult,T2>::is_greater(x,y);
}

template<typename CompositeKeyResult,typename T2>
static bool is_equiv(const CompositeKeyResult& x,const T2& y)
{
  return comparison_helper<CompositeKeyResult,T2>::is_equiv(x,y);
}

template<typename T1,typename T2,typename Compare>
static bool is_less(const T1& x,const T2& y,const Compare& c)
{
  return c(x,y)&&!c(y,x);
}

template<typename T1,typename T2,typename Compare>
static bool is_greater(const T1& x,const T2& y,const Compare& c)
{
  return c(y,x)&&!c(x,y);
}

template<typename T1,typename T2,typename Compare>
static bool is_equiv(const T1& x,const T2& y,const Compare& c)
{
  return !c(x,y)&&!c(y,x);
}

struct xyz
{
  xyz(int x_=0,int y_=0,int z_=0):x(x_),y(y_),z(z_){}

  int  x;
  int  y;
  int  z;
};

void test_composite_key()
{
  typedef composite_key<
    xyz,
    BOOST_MULTI_INDEX_MEMBER(xyz,int,x),
    BOOST_MULTI_INDEX_MEMBER(xyz,int,y),
    BOOST_MULTI_INDEX_MEMBER(xyz,int,z)
  > ckey_t1;

  typedef multi_index_container<
    xyz,
    indexed_by<
      ordered_unique<
        ckey_t1
#if defined(BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION)
        ,composite_key_result_less<ckey_t1::result_type>
#endif
      >
    >
  > indexed_t;

  indexed_t mc;
  mc.insert(xyz(0,0,0));
  mc.insert(xyz(0,0,1));
  mc.insert(xyz(0,1,0));
  mc.insert(xyz(0,1,1));
  mc.insert(xyz(1,0,0));
  mc.insert(xyz(1,0,1));
  mc.insert(xyz(1,1,0));
  mc.insert(xyz(1,1,1));

  BOOST_CHECK(mc.size()==8);
  BOOST_CHECK(
    std::distance(
      mc.find(mc.key_extractor()(xyz(0,0,0))),
      mc.find(mc.key_extractor()(xyz(1,0,0))))==4);
  BOOST_CHECK(
    std::distance(
      mc.find(make_tuple(0,0,0)),
      mc.find(make_tuple(1,0,0)))==4);
  BOOST_CHECK(
    std::distance(
      mc.lower_bound(make_tuple(0,0)),
      mc.upper_bound(make_tuple(1,0)))==6);

  ckey_t1 ck1;
  ckey_t1 ck2(ck1);
  ckey_t1 ck3(
    boost::make_tuple(
      BOOST_MULTI_INDEX_MEMBER(xyz,int,x)(),
      BOOST_MULTI_INDEX_MEMBER(xyz,int,y)(),
      BOOST_MULTI_INDEX_MEMBER(xyz,int,z)()));
  ckey_t1 ck4(get<0>(ck1.key_extractors()));

  ck3=ck3; /* prevent unused var */

  get<2>(ck4.key_extractors())=
    get<2>(ck2.key_extractors());

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),ck2(xyz(0,0,0))));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,1)),ck2(xyz(0,1,0))));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,1)),ck2(xyz(0,0,0))));

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0)));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(1)));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(-1)));
  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0,0)));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(0,1)));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(0,-1)));
  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0,0,0)));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(0,0,1)));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(0,0,-1)));
  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0,0,0,1)));

  typedef composite_key_result_less<ckey_t1::result_type> ckey_comp_t1;

  ckey_comp_t1 cp1;

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),ck2(xyz(0,0,0)),cp1));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,1)),ck2(xyz(0,1,0)),cp1));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,1)),ck2(xyz(0,0,0)),cp1));

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0),cp1));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(1),cp1));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(-1),cp1));
  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0,0),cp1));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(0,1),cp1));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(0,-1),cp1));
  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0,0,0),cp1));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(0,0,1),cp1));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(0,0,-1),cp1));

  typedef composite_key_result_greater<ckey_t1::result_type> ckey_comp_t2;

  ckey_comp_t2 cp2;

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),ck2(xyz(0,0,0)),cp2));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,1)),ck2(xyz(0,1,0)),cp2));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,1)),ck2(xyz(0,0,0)),cp2));

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0),cp2));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(1),cp2));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(-1),cp2));
  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0,0),cp2));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(0,1),cp2));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(0,-1),cp2));
  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0,0,0),cp2));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(0,0,1),cp2));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(0,0,-1),cp2));

  typedef composite_key_compare<
    std::less<int>,
    std::greater<int>, /* order reversed */
    std::less<int>
  > ckey_comp_t3;

  ckey_comp_t3 cp3;
  ckey_comp_t3 cp4(cp3);
  ckey_comp_t3 cp5(
    boost::make_tuple(
      std::less<int>(),
      std::greater<int>(),
      std::less<int>()));
  ckey_comp_t3 cp6(get<0>(cp3.key_comps()));

  cp4=cp5; /* prevent unused var */
  cp5=cp6; /* prevent unused var */
  cp6=cp4; /* prevent unused var */

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),ck2(xyz(0,0,0)),cp3));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,1)),ck2(xyz(0,1,0)),cp3));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,1)),ck2(xyz(0,0,0)),cp3));

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0),cp3));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(1),cp3));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(-1),cp3));
  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0,0),cp3));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(0,-1),cp3));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(0,1),cp3));
  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,0)),make_tuple(0,0,0),cp3));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),make_tuple(0,0,1),cp3));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),make_tuple(0,0,-1),cp3));

  typedef composite_key<
    xyz,
    BOOST_MULTI_INDEX_MEMBER(xyz,int,y), /* members reversed */
    BOOST_MULTI_INDEX_MEMBER(xyz,int,x)
  > ckey_t2;

  ckey_t2 ck5;

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,1)),ck5(xyz(0,0,0))));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),ck5(xyz(-1,1,0))));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),ck5(xyz(1,-1,0))));

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,1)),ck5(xyz(0,0,0)),cp1));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),ck5(xyz(-1,1,0)),cp1));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),ck5(xyz(1,-1,0)),cp1));

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,1)),ck5(xyz(0,0,0)),cp2));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),ck5(xyz(-1,1,0)),cp2));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),ck5(xyz(1,-1,0)),cp2));

  BOOST_CHECK(is_equiv  (ck1(xyz(0,0,1)),ck5(xyz(0,0,0)),cp3));
  BOOST_CHECK(is_less   (ck1(xyz(0,0,0)),ck5(xyz(-1,1,0)),cp3));
  BOOST_CHECK(is_greater(ck1(xyz(0,0,0)),ck5(xyz(1,-1,0)),cp3));
}
