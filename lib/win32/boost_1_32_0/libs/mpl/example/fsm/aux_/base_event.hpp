
#ifndef BOOST_FSM_BASE_EVENT_INCLUDED
#define BOOST_FSM_BASE_EVENT_INCLUDED

// Copyright Aleksey Gurtovoy 2002-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/example/fsm/aux_/base_event.hpp,v $
// $Date: 2004/09/02 15:41:30 $
// $Revision: 1.3 $

#include <memory>

namespace fsm { namespace aux {

// represent an abstract base for FSM events

struct base_event
{
 public:
    virtual ~base_event() {};
    
    std::auto_ptr<base_event> clone() const
    {
        return do_clone();
    }
 
 private:
    virtual std::auto_ptr<base_event> do_clone() const = 0;
};

}}

#endif // BOOST_FSM_BASE_EVENT_INCLUDED
