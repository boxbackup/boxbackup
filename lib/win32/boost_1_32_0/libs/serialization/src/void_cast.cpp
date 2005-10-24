/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// void_cast.cpp: implementation of run-time casting of void pointers

// (C) Copyright 2002 Robert Ramey - http://www.rrsd.com . 
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
// <gennadiy.rozental@tfn.com>

//  See http://www.boost.org for updates, documentation, and revision history.

#include <boost/config.hpp> // msvc needs this to suppress warning

// STL
#include <set>
#include <functional>
#include <algorithm>
#include <cassert>

// BOOST
#include <boost/serialization/void_cast.hpp>
#include <boost/serialization/extended_type_info.hpp>

namespace boost { 
namespace serialization {

namespace void_cast_detail {

struct void_caster_compare
{
    bool
    operator()( void_caster const* lhs, void_caster const* rhs ) const
    {
        if( lhs->m_derived_type < rhs->m_derived_type )
            return true;
        
        if( rhs->m_derived_type < lhs->m_derived_type)
            return false;
        
        if( lhs->m_base_type < rhs->m_base_type )
            return true;

        return false;
    }
};

struct void_caster_registry
{
    typedef std::set<void_caster*,void_caster_compare> set_type;
    typedef set_type::iterator iterator;
    set_type m_set;
};

// note: using accessing through this singleton guarentees that the 
// constructor for the set is invoked before any entries are added to it.
void_caster_registry & 
global_registry()
{
    static void_caster_registry instance;
    return instance;
}

void
void_caster::self_register() 
{ 
    // from/to pairs are registered when created
    // and there should only be one instance of each pair
    if(! global_registry().m_set.insert(this).second)
        assert(false);
}

} // void_cast_detail

// Given a void *, assume that it really points to an instance of one type
// and alter it so that it would point to an instance of a related type.
// Return the altered pointer. If there exists no sequence of casts that
// can transform from_type to to_type, return a NULL.  
const void *
void_upcast(
    const extended_type_info & derived_type,
    const extended_type_info & base_type,
    const void * t,
    bool top
){
    // same types - trivial case
    if (derived_type == base_type)
        return t;
    
    // check to see if base/derived pair is found in the registry
    void_cast_detail::void_caster ca(derived_type, base_type );
    void_cast_detail::void_caster_registry::iterator it;
    it = void_cast_detail::global_registry().m_set.find( &ca );
    
    // if so
    if (it != void_cast_detail::global_registry().m_set.end())
        // we're done
        return (*it)->upcast(t);

    const void * t_new = NULL;
    // try to find a chain that gives us what we want
    for(
        it = void_cast_detail::global_registry().m_set.begin();
        it != void_cast_detail::global_registry().m_set.end();
        ++it
    ){
        // if the current candidate doesn't cast to the desired target type
        if ((*it)->m_base_type == base_type){
            // if the current candidate casts from the desired source type
            if ((*it)->m_derived_type == derived_type){
                // we have a base/derived match - we're done
                // cast to the intermediate type
                t_new = (*it)->upcast(t);
                break;
            }
            t_new = void_upcast(derived_type, (*it)->m_derived_type, t, false);
            if (NULL != t_new){
                t_new = (*it)->upcast(t_new);
                if(top){
                    // register the this pair so we will have to go through
                    // keep this expensive search process more than once.
                    new void_cast_detail::void_caster_derived( 
                        derived_type,
                        base_type,
                        static_cast<const char*>(t_new) - static_cast<const char*>(t)
                    );
                }
                break;
            }
        }
    }
    return t_new;
}

const void *
void_downcast(
    const extended_type_info & derived_type,
    const extended_type_info & base_type,
    const void * t,
    bool top
){
    // same types - trivial case
    if (derived_type == base_type)
        return t;
    
    // check to see if base/derived pair is found in the registry
    void_cast_detail::void_caster ca(derived_type, base_type );
    void_cast_detail::void_caster_registry::iterator it;
    it = void_cast_detail::global_registry().m_set.find( &ca );
    
    // if so
    if (it != void_cast_detail::global_registry().m_set.end())
        // we're done
        return (*it)->downcast(t);

    const void * t_new = NULL;
    // try to find a chain that gives us what we want
    for(
        it = void_cast_detail::global_registry().m_set.begin();
        it != void_cast_detail::global_registry().m_set.end();
        ++it
    ){
        // if the current candidate doesn't cast from the desired target type
        if ((*it)->m_base_type == base_type){
            // if the current candidate casts to the desired source type
            if ((*it)->m_derived_type == derived_type){
                // we have a base/derived match - we're done
                // cast to the intermediate type
                t_new = (*it)->downcast(t);
                break;
            }
            t_new = void_downcast(derived_type, (*it)->m_derived_type, t, false);
            if (NULL != t_new){
                t_new = (*it)->downcast(t_new);
                if(top){
                    // register the this pair so we will have to go through
                    // keep this expensive search process more than once.
                    new void_cast_detail::void_caster_derived( 
                        derived_type,
                        base_type,
                        static_cast<const char*>(t) - static_cast<const char*>(t_new)
                    );
                }
                break;
            }
        }
    }
    return t_new;
}


} // namespace serialization
} // namespace boost

// EOF
