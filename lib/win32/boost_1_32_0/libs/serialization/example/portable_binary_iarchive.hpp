#ifndef PORTABLE_BINARY_IARCHIVE_HPP
#define PORTABLE_BINARY_IARCHIVE_HPP

// MS compatible compilers support #pragma once
#if defined(_MSC_VER) && (_MSC_VER >= 1020)
# pragma once
#endif

/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// portable_binary_iarchive.hpp

// (C) Copyright 2002 Robert Ramey - http://www.rrsd.com . 
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

//  See http://www.boost.org for updates, documentation, and revision history.

#include <ostream>
#include <boost/archive/binary_iarchive.hpp>

/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// exception to be thrown if integer read from archive doesn't fit
// variable being loaded
class portable_binary_archive_exception : 
    public virtual boost::archive::archive_exception
{
public:
    typedef enum {
        incompatible_integer_size 
    } exception_code;
    portable_binary_archive_exception(exception_code c = incompatible_integer_size )
    {}
    virtual const char *what( ) const throw( )
    {
        const char *msg = "programmer error";
        switch(code){
        case incompatible_integer_size:
            msg = "integer cannot be represented";
        default:
            boost::archive::archive_exception::what();
        }
        return msg;
    }
};

/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// "Portable" input binary archive.  This is a variation of the native binary 
// archive. it addresses integer size and endienness so that binary archives can
// be passed across systems. Note:floating point types not addressed here
class portable_binary_iarchive :
    // don't derive from binary_oarchive !!!
    public boost::archive::binary_iarchive_impl<portable_binary_iarchive>
{
    typedef portable_binary_iarchive derived_t;
#ifndef BOOST_NO_MEMBER_TEMPLATE_FRIENDS
public:
#else
    friend class boost::archive::basic_binary_iarchive<derived_t>;
    friend class boost::archive::basic_binary_iprimitive<derived_t, std::istream>;
    friend class boost::archive::load_access;
#endif
    void load_impl(long & l, char maxsize){
        char size;
        size = is.get();
        if(size > maxsize)
            throw portable_binary_archive_exception() ;
        l = 0;
        load_binary(& l, size);
        // we choose to use litle endian
        #ifdef BOOST_BIG_ENDIAN
            char * first = static_cast<char *>(static_cast<void *>(& l));
            char * last = first + sizeof(l) - 1;
            for(;first < last;++first, --last){
                char x = *first;
                *last = *first;
                *first = x;
            }
        #endif
    }
    // default fall through for any types not specified here
    template<class T>
    void load(T & t){
        boost::archive::binary_iarchive_impl<derived_t>::load(t);
    }
    void load(unsigned int & t){
        long l;
        load_impl(l, sizeof(unsigned int));
        t = l;
    }
    void load(int & t){
        long l;
        load_impl(l, sizeof(int));
        t = l;
    }
    void load(unsigned long & t){
        long l;
        load_impl(l, sizeof(unsigned long));
        t = l;
    }
    void load(long & t){
        long l;
        load_impl(l, sizeof(long));
        t = l;
    }
public:
    portable_binary_iarchive(std::istream & is, unsigned flags = 0) :
        boost::archive::binary_iarchive_impl<derived_t>(
            is, 
            flags | boost::archive::no_header // skip default header checking 
        )
    {
        // use our own header checking
        if(0 != (flags & boost::archive::no_header)){
            boost::archive::basic_binary_iarchive<derived_t>::init();
            // skip the following for "portable" binary archives
            // boost::archive::basic_binary_oprimitive<derived_t, std::ostream>::init();
        }
    }
};

// explicitly instantiate for this type of text stream
#include <boost/archive/impl/archive_pointer_iserializer.ipp>
#include <boost/archive/impl/basic_binary_iprimitive.ipp>

namespace boost {
namespace archive {

template class basic_binary_iprimitive<portable_binary_iarchive, std::istream> ;
template class binary_iarchive_impl<portable_binary_iarchive> ;
template class detail::archive_pointer_iserializer<portable_binary_iarchive> ;

} // namespace archive
} // namespace boost

#define BOOST_ARCHIVE_CUSTOM_IARCHIVE_TYPES portable_binary_iarchive

#endif // PORTABLE_BINARY_IARCHIVE_HPP
