
// Copyright Aleksey Gurtovoy 2000-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/min_max.cpp,v $
// $Date: 2004/09/02 15:41:35 $
// $Revision: 1.2 $

#include <boost/mpl/min_max.hpp>
#include <boost/mpl/int.hpp>

#include <boost/mpl/aux_/test.hpp>


MPL_TEST_CASE()
{
    MPL_ASSERT(( is_same< mpl::min< int_<5>,int_<7> >::type,int_<5> > ));
    MPL_ASSERT(( is_same< mpl::max< int_<5>,int_<7> >::type,int_<7> > ));

    MPL_ASSERT(( is_same< mpl::min< int_<-5>,int_<-7> >::type,int_<-7> > ));
    MPL_ASSERT(( is_same< mpl::max< int_<-5>,int_<-7> >::type,int_<-5> > ));
}
