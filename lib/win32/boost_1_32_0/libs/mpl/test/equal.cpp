
// Copyright Aleksey Gurtovoy 2000-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/equal.cpp,v $
// $Date: 2004/09/02 15:41:35 $
// $Revision: 1.4 $

#include <boost/mpl/equal.hpp>

#include <boost/mpl/list.hpp>
#include <boost/mpl/aux_/test.hpp>

MPL_TEST_CASE()
{
    typedef list<int,float,long,double,char,long,double,float> list1;
    typedef list<int,float,long,double,char,long,double,float> list2;
    typedef list<int,float,long,double,char,long,double,short> list3;
    typedef list<int,float,long,double,char,long,double> list4;
    
    MPL_ASSERT(( equal<list1,list2> ));
    MPL_ASSERT(( equal<list2,list1> ));
    MPL_ASSERT_NOT(( equal<list2,list3> ));
    MPL_ASSERT_NOT(( equal<list3,list4> ));
    MPL_ASSERT_NOT(( equal<list4,list3> ));
}
