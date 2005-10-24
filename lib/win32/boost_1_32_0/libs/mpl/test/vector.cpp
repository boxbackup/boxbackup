
// Copyright Aleksey Gurtovoy 2000-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/vector.cpp,v $
// $Date: 2004/09/02 15:41:35 $
// $Revision: 1.3 $

#include <boost/mpl/vector.hpp>
#include <boost/mpl/vector/vector10.hpp>
#include <boost/mpl/push_back.hpp>
#include <boost/mpl/pop_back.hpp>
#include <boost/mpl/push_front.hpp>
#include <boost/mpl/pop_front.hpp>
#include <boost/mpl/back.hpp>
#include <boost/mpl/front.hpp>
#include <boost/mpl/size.hpp>
#include <boost/mpl/empty.hpp>

#include <boost/mpl/aux_/test.hpp>


MPL_TEST_CASE()
{
    typedef vector0<> v0;
    typedef vector1<char> v1;
    typedef vector2<char,long> v2;
    typedef vector9<char,char,char,char,char,char,char,char,int> v9;

    MPL_ASSERT_RELATION( size<v0>::value, ==, 0 );
    MPL_ASSERT_RELATION( size<v1>::value, ==, 1 );
    MPL_ASSERT_RELATION( size<v2>::value, ==, 2 );
    MPL_ASSERT_RELATION( size<v9>::value, ==, 9 );

    MPL_ASSERT(( empty<v0> ));
    MPL_ASSERT_NOT(( empty<v1> ));
    MPL_ASSERT_NOT(( empty<v2> ));
    MPL_ASSERT_NOT(( empty<v9> ));

    MPL_ASSERT(( is_same< front<v1>::type,char > ));
    MPL_ASSERT(( is_same< back<v1>::type,char > ));
    MPL_ASSERT(( is_same< front<v2>::type,char > ));
    MPL_ASSERT(( is_same< back<v2>::type,long > ));
    MPL_ASSERT(( is_same< front<v9>::type,char > ));
    MPL_ASSERT(( is_same< back<v9>::type,int > ));
}


MPL_TEST_CASE()
{
    typedef vector2<char,long> v2;
    
    typedef begin<v2>::type i1;
    typedef next<i1>::type  i2;
    typedef next<i2>::type  i3;
    
    MPL_ASSERT(( is_same<deref<i1>::type,char> ));
    MPL_ASSERT(( is_same<deref<i2>::type,long> ));
    MPL_ASSERT(( is_same< i3, end<v2>::type > ));
}

MPL_TEST_CASE()
{
    typedef vector0<> v0;

    typedef push_back<v0,int>::type v1;
    MPL_ASSERT(( is_same< back<v1>::type,int > ));

    typedef push_front<v1,char>::type v2;
    MPL_ASSERT(( is_same< back<v2>::type,int > ));
    MPL_ASSERT(( is_same< front<v2>::type,char > ));

    typedef push_back<v2,long>::type v3;
    MPL_ASSERT(( is_same< back<v3>::type,long > ));
}

MPL_TEST_CASE()
{
    typedef vector<> v0;
    typedef vector<char> v1;
    typedef vector<char,long> v2;
    typedef vector<char,char,char,char,char,char,char,char,int> v9;

    MPL_ASSERT_RELATION( size<v0>::value, ==, 0 );
    MPL_ASSERT_RELATION( size<v1>::value, ==, 1 );
    MPL_ASSERT_RELATION( size<v2>::value, ==, 2 );
    MPL_ASSERT_RELATION( size<v9>::value, ==, 9 );
}
