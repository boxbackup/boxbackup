
// Copyright Aleksey Gurtovoy 2002-2004
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/zip_view.cpp,v $
// $Date: 2004/09/02 15:41:35 $
// $Revision: 1.4 $

#include <boost/mpl/zip_view.hpp>

#include <boost/mpl/transform_view.hpp>
#include <boost/mpl/filter_view.hpp>
#include <boost/mpl/range_c.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/mpl/at.hpp>
#include <boost/mpl/equal.hpp>
#include <boost/mpl/equal_to.hpp>
#include <boost/mpl/unpack_args.hpp>
#include <boost/mpl/math/is_even.hpp>

#include <boost/mpl/aux_/test.hpp>


MPL_TEST_CASE()
{
    typedef transform_view<
          zip_view< vector< range_c<int,0,10>, range_c<int,10,20> > >
        , unpack_args< plus<> >
        > result;

    MPL_ASSERT(( equal< 
          result
        , filter_view< range_c<int,10,30>, is_even<_> >
        , equal_to<_,_>
        > ));
}
