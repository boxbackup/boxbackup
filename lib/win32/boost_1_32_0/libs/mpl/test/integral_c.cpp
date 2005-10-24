
// Copyright Aleksey Gurtovoy 2001-2004
//
// Distributed under the Boost Software License,Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/integral_c.cpp,v $
// $Date: 2004/09/02 15:41:35 $
// $Revision: 1.4 $

#include <boost/mpl/integral_c.hpp>
#include <boost/preprocessor/repeat.hpp>

#include "integral_wrapper_test.hpp"


MPL_TEST_CASE()
{
#   define WRAPPER(T, i) integral_c<T,i>
    BOOST_PP_REPEAT(10, INTEGRAL_WRAPPER_TEST, char)
    BOOST_PP_REPEAT(10, INTEGRAL_WRAPPER_TEST, short)
    BOOST_PP_REPEAT(10, INTEGRAL_WRAPPER_TEST, int)
    BOOST_PP_REPEAT(10, INTEGRAL_WRAPPER_TEST, long)
}
