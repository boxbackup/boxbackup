
// Copyright Aleksey Gurtovoy 2001-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/contains.cpp,v $
// $Date: 2004/09/02 15:41:35 $
// $Revision: 1.3 $

#include <boost/mpl/contains.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/mpl/aux_/test.hpp>

MPL_TEST_CASE()
{    
    typedef vector<int,char,long,short,char,long,double,long>::type types;

    MPL_ASSERT(( contains< types,short > ));
    MPL_ASSERT_NOT(( contains< types,unsigned > ));
}
