
// Copyright Aleksey Gurtovoy 2001-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/integral_wrapper_test.hpp,v $
// $Date: 2004/10/13 14:27:35 $
// $Revision: 1.3 $

#include <boost/mpl/next_prior.hpp>
#include <boost/mpl/aux_/test.hpp>
#include <boost/mpl/aux_/config/workaround.hpp>

#include <cassert>

#if !BOOST_WORKAROUND(__BORLANDC__, < 0x600)
#   define INTEGRAL_WRAPPER_RUNTIME_TEST(i, T) \
    assert(( WRAPPER(T,i)() == i )); \
    assert(( WRAPPER(T,i)::value == i )); \
    /**/
#else
#   define INTEGRAL_WRAPPER_RUNTIME_TEST(i, T) \
    assert(( WRAPPER(T,i)::value == i )); \
    /**/
#endif

#define INTEGRAL_WRAPPER_TEST(unused1, i, T) \
    { MPL_ASSERT(( is_same< WRAPPER(T,i)::value_type, T > )); } \
    { MPL_ASSERT(( is_same< WRAPPER(T,i)::type, WRAPPER(T,i) > )); } \
    { MPL_ASSERT(( is_same< next< WRAPPER(T,i) >::type, WRAPPER(T,i+1) > )); } \
    { MPL_ASSERT(( is_same< prior< WRAPPER(T,i) >::type, WRAPPER(T,i-1) > )); } \
    { MPL_ASSERT_RELATION( (WRAPPER(T,i)::value), ==, i ); } \
    INTEGRAL_WRAPPER_RUNTIME_TEST(i, T) \
/**/
