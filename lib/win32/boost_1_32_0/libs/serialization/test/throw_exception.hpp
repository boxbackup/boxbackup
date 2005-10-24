#ifndef BOOST_SERIALIZATION_EXCEPTION_HPP
#define BOOST_SERIALIZATION_EXCEPTION_HPP

// MS compatible compilers support #pragma once
#if defined(_MSC_VER) && (_MSC_VER >= 1020)
# pragma once
#endif

/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
//  throw_exception.hpp
//
//  Copyright (c) 2002 Peter Dimov and Multi Media Ltd.
//
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
//  http://www.boost.org/libs/utility/throw_exception.html
//

#include <boost/config.hpp>
#include <boost/throw_exception.hpp>

#ifndef BOOST_NO_EXCEPTIONS
# include <exception>
#endif

namespace boost {

#ifdef BOOST_NO_EXCEPTIONS
    // user defined
    void throw_exception(std::exception const & e); 
    void throw_exception();
    } // namespace boost
    #define BOOST_TRY
    #define BOOST_CATCH(x) if(0)
#else
//  inline template<class E> void throw_exception(E const & e)
//  {
//      throw e;
//  }
    inline void throw_exception()
    {
        throw;
    }
    #define BOOST_TRY try
    #define BOOST_CATCH(x) catch(x)
#endif

} // namespace boost

#endif // BOOST_SERIALIZATION_EXCEPTION_HPP
