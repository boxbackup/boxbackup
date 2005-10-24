
#ifndef BOOST_MPL_ACCUMULATE_HPP_INCLUDED
#define BOOST_MPL_ACCUMULATE_HPP_INCLUDED

// Copyright Aleksey Gurtovoy 2001-2004
// Copyright David Abrahams 2001-2002
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/boost/mpl/Attic/accumulate.hpp,v $
// $Date: 2004/11/15 04:55:58 $
// $Revision: 1.1.4.1 $

#include <boost/mpl/fold.hpp>
#include <boost/mpl/aux_/na_spec.hpp>
#include <boost/mpl/aux_/lambda_support.hpp>

namespace boost { namespace mpl {

template<
      typename BOOST_MPL_AUX_NA_PARAM(Sequence)
    , typename BOOST_MPL_AUX_NA_PARAM(State)
    , typename BOOST_MPL_AUX_NA_PARAM(ForwardOp)
    >
struct accumulate
    : fold<Sequence,State,ForwardOp>
{
    BOOST_MPL_AUX_LAMBDA_SUPPORT(3,accumulate,(Sequence,State,ForwardOp))
};

BOOST_MPL_AUX_NA_SPEC(3, accumulate)

}}

#endif // BOOST_MPL_ACCUMULATE_HPP_INCLUDED
