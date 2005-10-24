/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// extended_type_info.cpp: implementation for portable version of type_info

// (C) Copyright 2002 Robert Ramey - http://www.rrsd.com . 
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

//  See http://www.boost.org for updates, documentation, and revision history.

#include <boost/config.hpp> // msvc needs this to suppress warning

#include <cstring>
#if defined(BOOST_NO_STDC_NAMESPACE)
namespace std{ using ::strcmp; }
#endif

#include <set>
#include <cassert>

#include <boost/serialization/extended_type_info.hpp>

namespace boost { 
namespace serialization {

namespace { // anonymous

struct type_info_compare
{
    bool
    operator()(const extended_type_info * lhs, const extended_type_info * rhs) const
    {
        return *lhs < *rhs;
    }
};
struct key_compare
{
    bool
    operator()(const extended_type_info * lhs, const extended_type_info * rhs) const
    {
        // shortcut to exploit string pooling
        if(lhs->key == rhs->key)
            return false;
        if(NULL == lhs->key)
            return true;
        if(NULL == rhs->key)
            return false;
        return std::strcmp(lhs->key, rhs->key) < 0; 
    }
};

// use static local variables to ensure that collections are
// initialized before being used.
typedef std::set<const extended_type_info *, type_info_compare> tkmap_type;
static tkmap_type &
tkmap(){
    static tkmap_type map;
    return map;
}

typedef std::set<const extended_type_info *, key_compare> ktmap_type;
static ktmap_type &
ktmap(){
    static ktmap_type map;
    return map;
}

} // anonymous

void extended_type_info::self_register()
{
    tkmap().insert(this);
}

void extended_type_info::key_register(const char *key_) {
    if(NULL == key_)
        return;
    key = key_;
    ktmap().insert(this);
}

namespace {

class extended_type_info_arg : public extended_type_info
{
public:
    extended_type_info_arg(const char * search_key_)
        : extended_type_info(NULL)
    {
        key = search_key_;
    }
    virtual bool
    less_than(const extended_type_info &rhs) const
    {
        assert(false);
        return false;   // to prevent a syntax error
    }
    virtual bool
    equal_to(const extended_type_info &rhs) const{
        assert(false);
        return false;   // to prevent a syntax error
    }
    virtual bool
    not_equal_to(const extended_type_info &rhs) const{
        assert(false);
        return false;   // to prevent a syntax error
    }
};
} // anonymous

const extended_type_info * extended_type_info::find(const char *key)
{

    extended_type_info_arg arg(key);
    ktmap_type::const_iterator it;
    it = ktmap().find(&arg);
    if(it == ktmap().end())
        return NULL;

    return *it;
}

const extended_type_info * extended_type_info::find(const extended_type_info * t)
{
    tkmap_type::const_iterator it;
    it = tkmap().find(t);
    if(it == tkmap().end())
        return NULL;
    return *it;
}

namespace { // anonymous
int type_info_key_cmp(
    const extended_type_info & lhs, 
    const extended_type_info & rhs
) {
    if(lhs.type_info_key == rhs.type_info_key)
        return 0;
    //return strcmp(lhs.type_info_key, rhs.type_info_key);
    // all we require is that the type_info_key be unique
    // so just compare the addresses
    return lhs.type_info_key < rhs.type_info_key ? -1 : 1;
}
} // anonymous

bool operator<(
    const extended_type_info &lhs,
    const extended_type_info &rhs
){
    int i = type_info_key_cmp(lhs, rhs);
    if(i < 0)
        return true;
    if(i > 0)
        return false;
    return lhs.less_than(rhs);
}

bool operator==(
    const extended_type_info &lhs,
    const extended_type_info &rhs
){
    int i = type_info_key_cmp(lhs, rhs);
    if(i != 0)
        return false;
    return lhs.equal_to(rhs);
}

bool operator!=(
    const extended_type_info &lhs,
    const extended_type_info &rhs
){
    int i = type_info_key_cmp(lhs, rhs);
    if(i != 0)
        return true;
    return lhs.not_equal_to(rhs);
}

} // namespace serialization
} // namespace boost
