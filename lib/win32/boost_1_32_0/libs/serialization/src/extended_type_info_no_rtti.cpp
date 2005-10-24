/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// extended_type_info_no_rtti.cpp: specific implementation of type info
// that is NOT based on typeid

// (C) Copyright 2002 Robert Ramey - http://www.rrsd.com . 
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

//  See http://www.boost.org for updates, documentation, and revision history.

#include <boost/serialization/extended_type_info_no_rtti.hpp>

namespace boost { 
namespace serialization { 

const char * extended_type_info_no_rtti_base::type_info_key
    = "extended_type_info_no_rtti";

} // namespace serialization
} // namespace boost
