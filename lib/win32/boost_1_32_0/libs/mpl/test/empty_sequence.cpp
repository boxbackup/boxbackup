
// Copyright Aleksey Gurtovoy 2004
// Copyright Alexander Nasonov 2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/Attic/empty_sequence.cpp,v $
// $Date: 2004/11/13 20:19:18 $
// $Revision: 1.1.2.1 $

#include <boost/mpl/empty_sequence.hpp>
#include <boost/mpl/size.hpp>
#include <boost/mpl/distance.hpp>
#include <boost/mpl/advance.hpp>
#include <boost/mpl/begin_end.hpp>
#include <boost/mpl/aux_/test.hpp>

#include <boost/type_traits/is_same.hpp>
#include <boost/type_traits/add_pointer.hpp>

MPL_TEST_CASE()
{
    typedef begin<empty_sequence>::type begin;
    typedef end<empty_sequence>::type end;

    MPL_ASSERT(( is_same<begin,end> ));
    MPL_ASSERT_RELATION( (mpl::distance<begin,end>::value), ==, 0 );
    MPL_ASSERT_RELATION( size<empty_sequence>::value, ==, 0 );

    typedef advance_c<begin,0>::type advanced;
    MPL_ASSERT(( is_same<advanced,end> ));
}
