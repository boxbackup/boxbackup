/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// xml_woarchive.cpp:

// (C) Copyright 2002 Robert Ramey - http://www.rrsd.com . 
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

//  See http://www.boost.org for updates, documentation, and revision history.

#include <boost/config.hpp>
#ifdef BOOST_NO_STD_WSTREAMBUF
#error "wide char i/o not supported on this platform"
#else

#include <boost/archive/xml_woarchive.hpp>

#include <boost/archive/impl/basic_xml_oarchive.ipp>
#include <boost/archive/impl/xml_woarchive_impl.ipp>
#include <boost/archive/impl/archive_pointer_oserializer.ipp>

namespace boost {
namespace archive {

// explicitly instantiate for this type of xml stream
template class basic_xml_oarchive<xml_woarchive> ;
template class xml_woarchive_impl<xml_woarchive> ;
template class detail::archive_pointer_oserializer<xml_woarchive> ;

} // namespace archive
} // namespace boost

#endif // BOOST_NO_STD_WSTREAMBUF
