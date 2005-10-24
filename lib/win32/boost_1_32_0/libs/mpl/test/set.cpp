
// Copyright Aleksey Gurtovoy 2003-2004
// Copyright David Abrahams 2003-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/set.cpp,v $
// $Date: 2004/11/04 12:40:53 $
// $Revision: 1.7.2.1 $

#include <boost/mpl/set.hpp>
#include <boost/mpl/insert.hpp>
#include <boost/mpl/erase.hpp>
#include <boost/mpl/erase_key.hpp>
#include <boost/mpl/at.hpp>
#include <boost/mpl/clear.hpp>
#include <boost/mpl/has_key.hpp>
#include <boost/mpl/order.hpp>
#include <boost/mpl/size.hpp>
#include <boost/mpl/empty.hpp>
#include <boost/mpl/begin_end.hpp>

#include <boost/mpl/aux_/test.hpp>


MPL_TEST_CASE()
{
    typedef s_mask<char,s_item<int,s_item<char, set0<> > > > s;

    MPL_ASSERT_RELATION( size<s>::value, ==, 1 );
    MPL_ASSERT_NOT(( empty<s> ));
    
    MPL_ASSERT(( is_same< clear<s>::type, set0<> > ));
    MPL_ASSERT(( is_same< at<s,int>::type, int > ));
    MPL_ASSERT(( is_same< at<s,char>::type, void_ > ));

    MPL_ASSERT_NOT(( has_key<s,char> ));
    MPL_ASSERT(( has_key<s,int> ));
    MPL_ASSERT_RELATION( (order<s,int>::value), ==, 3 );
    MPL_ASSERT(( is_same< order<s,char>::type, void_ >  ));
    
    typedef begin<s>::type first;
    typedef end<s>::type last;

    MPL_ASSERT(( is_same< deref<first>::type, int > ));
    MPL_ASSERT(( is_same< next<first>::type, last > ));

    typedef s_unmask<char,s> s2;

    MPL_ASSERT_RELATION( size<s2>::value, ==, 2 );
    MPL_ASSERT_NOT(( empty<s2> ));
    MPL_ASSERT(( is_same<clear<s2>::type, set0<> > ));
    MPL_ASSERT(( is_same<at<s2,int>::type, int > ));
    MPL_ASSERT(( is_same<at<s2,char>::type, char > ));

    MPL_ASSERT(( has_key<s2,char> ));
    MPL_ASSERT_NOT(( has_key<s2,long> ));
    MPL_ASSERT_RELATION( (order<s2,int>::value), ==, 3 );
    MPL_ASSERT_RELATION( (order<s2,char>::value), ==, 2 );

    typedef begin<s2>::type first2;
    typedef end<s2>::type last2;

    MPL_ASSERT(( is_same< first2::type, int > ));
    typedef next<first2>::type iter;
    MPL_ASSERT(( is_same< iter::type, char > ));
    MPL_ASSERT(( is_same< next<iter>::type, last2 > ));

    typedef insert<s2,int>::type s2_1;
    MPL_ASSERT(( is_same<s2, s2_1> ));

    typedef insert<s2,long>::type s3;
    MPL_ASSERT_RELATION( size<s3>::value, ==, 3 );
    MPL_ASSERT(( has_key<s3,long> ));
    MPL_ASSERT(( has_key<s3,int> ));
    MPL_ASSERT(( has_key<s3,char> ));

    typedef insert<s,char>::type s1;
    MPL_ASSERT_RELATION( size<s1>::value, ==, 2 );
    MPL_ASSERT(( is_same<at<s1,int>::type, int > ));
    MPL_ASSERT(( is_same<at<s1,char>::type, char > ));
    MPL_ASSERT_NOT(( is_same<s1, s2> ));

    typedef erase_key<s1,char>::type s_1;
    MPL_ASSERT(( is_same<s, s_1> ));
    MPL_ASSERT_RELATION( size<s_1>::value, ==, 1 );
    MPL_ASSERT(( is_same< at<s_1,char>::type, void_ > ));
    MPL_ASSERT(( is_same< at<s_1,int>::type, int > ));
    
}


MPL_TEST_CASE()
{
    typedef set0<> s;
    
    MPL_ASSERT_RELATION( size<s>::value, ==, 0 );
    MPL_ASSERT(( empty<s> ));
    MPL_ASSERT(( is_same< clear<s>::type, set0<> > ));
    MPL_ASSERT(( is_same< at<s,char>::type, void_ > ));

    MPL_ASSERT_NOT(( has_key<s,char> ));
    MPL_ASSERT_NOT(( has_key<s,int> ));
    MPL_ASSERT_NOT(( has_key<s,UDT> ));
    MPL_ASSERT_NOT(( has_key<s,incomplete> ));

    MPL_ASSERT_NOT(( has_key<s,char const> ));
    MPL_ASSERT_NOT(( has_key<s,int const> ));
    MPL_ASSERT_NOT(( has_key<s,UDT const> ));
    MPL_ASSERT_NOT(( has_key<s,incomplete const> ));

    MPL_ASSERT_NOT(( has_key<s,int*> ));
    MPL_ASSERT_NOT(( has_key<s,UDT*> ));
    MPL_ASSERT_NOT(( has_key<s,incomplete*> ));

    MPL_ASSERT_NOT(( has_key<s,int&> ));
    MPL_ASSERT_NOT(( has_key<s,UDT&> ));
    MPL_ASSERT_NOT(( has_key<s,incomplete&> ));

    typedef insert<s,char>::type s1;
    MPL_ASSERT_RELATION( size<s1>::value, ==, 1 );
    MPL_ASSERT(( is_same< at<s1,char>::type, char > ));

    typedef erase_key<s,char>::type s0_1;
    MPL_ASSERT_RELATION( size<s0_1>::value, ==, 0 );
    MPL_ASSERT(( is_same< at<s0_1,char>::type, void_ > ));
}

MPL_TEST_CASE()
{
    typedef set<
          char,int const,long*,UDT* const,incomplete,abstract
        , incomplete volatile&,abstract const&
        > s;
    
    MPL_ASSERT_RELATION( size<s>::value, ==, 8 );
    MPL_ASSERT_NOT(( empty<s> ));
    MPL_ASSERT(( is_same< clear<s>::type, set0<> > ));
    MPL_ASSERT(( is_same< at<s,bool>::type, void_ >  ));

    MPL_ASSERT(( has_key<s,char> ));
    MPL_ASSERT(( has_key<s,int const> ));
    MPL_ASSERT(( has_key<s,long*> ));
    MPL_ASSERT(( has_key<s,UDT* const> ));
    MPL_ASSERT(( has_key<s,incomplete> ));
    MPL_ASSERT(( has_key<s,abstract> ));
    MPL_ASSERT(( has_key<s,incomplete volatile&> ));
    MPL_ASSERT(( has_key<s,abstract const&> ));

    MPL_ASSERT_NOT(( has_key<s,char const> ));
    MPL_ASSERT_NOT(( has_key<s,int> ));
    MPL_ASSERT_NOT(( has_key<s,long* const> ));
    MPL_ASSERT_NOT(( has_key<s,UDT*> ));
    MPL_ASSERT_NOT(( has_key<s,incomplete const> ));
    MPL_ASSERT_NOT(( has_key<s,abstract volatile> ));
    MPL_ASSERT_NOT(( has_key<s,incomplete&> ));
    MPL_ASSERT_NOT(( has_key<s,abstract&> ));
}
