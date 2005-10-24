
#ifndef BOOST_FSM_TRANSITION_INCLUDED
#define BOOST_FSM_TRANSITION_INCLUDED

// Copyright Aleksey Gurtovoy 2002-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/example/fsm/aux_/transition.hpp,v $
// $Date: 2004/09/02 15:41:30 $
// $Revision: 1.4 $

#include <cassert>

namespace fsm { namespace aux {

// represent a signle transition between states |From| and |To|

template<
      typename T
    , typename From
    , typename Event
    , typename To
    , bool (T::* transition_func)(Event const&)
    >
struct transition
{
    typedef T       fsm_t;
    typedef From    from_state_t;
    typedef Event   event_t;
    typedef To      to_state_t;

    typedef typename Event::base_t  base_event_t;
    static bool do_transition(T& x, base_event_t const& e)
    {
        assert(dynamic_cast<event_t const*>(&e) == &e);
        return (x.*transition_func)(static_cast<event_t const &>(e));
    }
};

}}

#endif // BOOST_FSM_TRANSITION_INCLUDED
