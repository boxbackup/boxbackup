#ifndef PORTABLE_BINARY_OARCHIVE_HPP
#define PORTABLE_BINARY_OARCHIVE_HPP

// MS compatible compilers support #pragma once
#if defined(_MSC_VER) && (_MSC_VER >= 1020)
# pragma once
#endif

/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// portable_binary_oarchive.hpp

// (C) Copyright 2002 Robert Ramey - http://www.rrsd.com . 
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

//  See http://www.boost.org for updates, documentation, and revision history.

#include <ostream>
#include <boost/archive/archive_exception.hpp>
#include <boost/archive/binary_oarchive.hpp>

/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// "Portable" output binary archive.  This is a variation of the native binary 
// archive. it addresses integer size and endienness so that binary archives can
// be passed across systems. Note:floating point types not addressed here

class portable_binary_oarchive :
    // don't derive from binary_oarchive !!!
    public boost::archive::binary_oarchive_impl<portable_binary_oarchive>
{
    typedef portable_binary_oarchive derived_t;
#ifndef BOOST_NO_MEMBER_TEMPLATE_FRIENDS
public:
#else
    friend class boost::archive::basic_binary_oarchive<derived_t>;
    friend class boost::archive::basic_binary_oprimitive<derived_t, std::ostream>;
    friend class boost::archive::save_access;
#endif
    void save_impl(long l){
        long ll = l;
        char size = 0;;
        do{
            ll >>= 8;
            ++size;
        }while(ll != -1 && ll != 0);

        os.put(size);

        // we choose to use litle endian
        #ifdef BOOST_BIG_ENDIAN
            char * first = static_cast<char *>(static_cast<void *>(& l));
            char * last = first + size - 1;
            for(;first < last;++first, --last){
                char x = *first;
                *last = *first;
                *first = x;
            }
        #endif
        save_binary(& l, size);
    }
    // add base class to the places considered when matching
    // save function to a specific set of arguments.  Note, this didn't
    // work on my MSVC 7.0 system so we use the sure-fire method below
    // using binary_oarchive_impl<derived_t>::save;

    // default fall through for any types not specified here
    template<class T>
    void save(const T & t){
        boost::archive::binary_oarchive_impl<derived_t>::save(t);
    }
    void save(const unsigned int t){
        save_impl(t);
    }
    void save(const int t){
        save_impl(t);
    }
    void save(const unsigned long t){
        save_impl(t);
    }
    void save(const long t){
        save_impl(t);
    }
public:
    portable_binary_oarchive(std::ostream & os, unsigned flags = 0) :
        boost::archive::binary_oarchive_impl<derived_t>(
            os, 
            flags | boost::archive::no_header // skip default header checking 
        )
    {
        // use our own header checking
        if(0 != (flags & boost::archive::no_header)){
            boost::archive::basic_binary_oarchive<derived_t>::init();
            // skip the following for "portable" binary archives
            // boost::archive::basic_binary_iprimitive<derived_t, std::ostream>::init();
        }
    }
};

#include <boost/archive/impl/archive_pointer_oserializer.ipp>
#include <boost/archive/impl/basic_binary_oprimitive.ipp>

namespace boost {
namespace archive {

// explicitly instantiate for this type of binary stream
template class basic_binary_oprimitive<portable_binary_oarchive, std::ostream> ;
template class binary_oarchive_impl<portable_binary_oarchive> ;
template class detail::archive_pointer_oserializer<portable_binary_oarchive> ;

} // namespace archive
} // namespace boost

#define BOOST_ARCHIVE_CUSTOM_OARCHIVE_TYPES portable_binary_oarchive

#endif // PORTABLE_BINARY_OARCHIVE_HPP
