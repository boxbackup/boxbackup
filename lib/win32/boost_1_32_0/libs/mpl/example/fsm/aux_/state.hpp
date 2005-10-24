
#ifndef BOOST_FSM_STATE_INCLUDED
#define BOOST_FSM_STATE_INCLUDED

// Copyright Aleksey Gurtovoy 2002-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/example/fsm/aux_/state.hpp,v $
// $Date: 2004/09/02 15:41:30 $
// $Revision: 1.3 $

#include <boost/mpl/integral_c.hpp>

namespace fsm { namespace aux {

namespace mpl = boost::mpl;

// represent a FSM state

template<
      typename T
    , long State
    , void (T::* invariant_func)() const
    >
struct state
    : mpl::integral_c<long,State>
{
    static long do_check_invariant(T const& x)
    {
        if (invariant_func) (x.*invariant_func)();
        return State;
    }
};

}}

#endif // BOOST_FSM_STATE_INCLUDED
