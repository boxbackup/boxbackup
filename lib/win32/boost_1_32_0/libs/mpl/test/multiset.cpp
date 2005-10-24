
// Copyright Aleksey Gurtovoy 2003-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/multiset.cpp,v $
// $Date: 2004/09/02 15:41:35 $
// $Revision: 1.2 $

#include <boost/mpl/multiset/multiset0.hpp>

#include <boost/mpl/insert.hpp>
#include <boost/mpl/count.hpp>
#include <boost/mpl/aux_/test.hpp>


MPL_TEST_CASE()
{
    typedef multiset0<> s0;
    typedef insert<s0,int>::type s1;
    typedef insert<s1,char&>::type s2;
    typedef insert<s2,int>::type s3;
    typedef insert<s3,abstract>::type s4;
    
    MPL_ASSERT_RELATION( (count<s0,int>::value), ==, 0 );
    MPL_ASSERT_RELATION( (count<s1,int>::value), ==, 1 );
    MPL_ASSERT_RELATION( (count<s2,int>::value), ==, 1 );
    MPL_ASSERT_RELATION( (count<s2,char&>::value), ==, 1 );
    MPL_ASSERT_RELATION( (count<s3,int>::value), ==, 2 );
    MPL_ASSERT_RELATION( (count<s3,char&>::value), ==, 1 );
    MPL_ASSERT_RELATION( (count<s4,abstract>::value), ==, 1 );
}
