#ifndef BOOST_SERIALIZATION_TEST_TOOLS_HPP
#define BOOST_SERIALIZATION_TEST_TOOLS_HPP

// MS compatible compilers support #pragma once
#if defined(_MSC_VER) && (_MSC_VER >= 1020)
# pragma once
#endif

/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// test_tools.hpp
//
// (C) Copyright 2002 Robert Ramey - http://www.rrsd.com . 
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

//  See http://www.boost.org for updates, documentation, and revision history.

#include <boost/config.hpp>
#include <cstdio> // remove, tmpnam

// win32 has a brain-dead tmpnam implementation.
// which leaves temp files in root directory 
// regardless of environmental settings
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)

#include <cstdlib>
#include <cstring>
#if defined(BOOST_NO_STDC_NAMESPACE)
namespace std{ 
    using ::remove;
    using ::strcpy;
    using ::strcat;
    using ::tmpnam;
}
#endif // defined(BOOST_NO_STDC_NAMESPACE)

#include <direct.h>
#include <boost/archive/tmpdir.hpp>

#if defined(__COMO__)
    #define chdir _chdir
#endif

#if defined(NDEBUG) && defined(__BORLANDC__)
    #define STRCPY strcpy
#else
    #define STRCPY std::strcpy
#endif

namespace boost {
namespace archive {
    char * tmpnam(char * buffer){
        char old_dir[256];
        _getcwd(old_dir, sizeof(old_dir) - 1);

        char * temp_dir = boost::archive::tmpdir();
        chdir(temp_dir);

        char temp_name[256];
        std::tmpnam(temp_name);

        chdir(old_dir);
        static char ibuffer [512];

        if(NULL == buffer)
            buffer = ibuffer;

        STRCPY(buffer, temp_dir);
        std::strcat(buffer, temp_name);
        return buffer;
    }
} // archive
} // boost

#else

namespace boost {
namespace archive {
    using std::tmpnam;
} // archive
} // boost

#endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)

#if defined(BOOST_MSVC)

/////////////////////////////////////////////////
// VC has a really nice free memory leak detector
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

struct leak_reporter {
    static leak_reporter instance;
    leak_reporter(){
        _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
    }
    ~leak_reporter(){
//        _CrtDumpMemoryLeaks(); // replaced by the above
    }
};
leak_reporter leak_reporter::instance;

#endif // defined(BOOST_MSVC)

/////////////////////////////////////////////
// invoke header for a custom archive test.
#if ! defined(BOOST_ARCHIVE_TEST)
#define BOOST_ARCHIVE_TEST text_archive.hpp
#endif

#include <boost/preprocessor/stringize.hpp>
#include BOOST_PP_STRINGIZE(BOOST_ARCHIVE_TEST)

#include <boost/test/test_tools.hpp>

#endif // BOOST_SERIALIZATION_TEST_TOOLS_HPP
