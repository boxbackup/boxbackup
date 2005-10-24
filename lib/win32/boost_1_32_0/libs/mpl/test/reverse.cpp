
// Copyright Aleksey Gurtovoy 2000-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/reverse.cpp,v $
// $Date: 2004/09/02 15:41:35 $
// $Revision: 1.5 $

#include <boost/mpl/reverse.hpp>

#include <boost/mpl/list_c.hpp>
#include <boost/mpl/range_c.hpp>
#include <boost/mpl/equal.hpp>
#include <boost/mpl/equal_to.hpp>
#include <boost/mpl/at.hpp>

#include <boost/mpl/aux_/test.hpp>

MPL_TEST_CASE()
{
    typedef list_c<int,9,8,7,6,5,4,3,2,1,0> numbers;
    typedef reverse< numbers >::type result;

    typedef range_c<int,0,10> answer;
    
    MPL_ASSERT(( equal< result,answer,equal_to<_1,_2> > ));
}
