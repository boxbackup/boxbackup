/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// test_set.cpp

// (C) Copyright 2002 Robert Ramey - http://www.rrsd.com . 
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

// should pass compilation and execution

#include <algorithm>
#include <vector>
#include <fstream>

#include <cstdio> // remove
#include <boost/config.hpp>

#if defined __COMO__
#error - this test fails on comeau with disatrous results
#endif

#include <boost/detail/workaround.hpp>
#if defined(BOOST_NO_STDC_NAMESPACE)
namespace std{ 
    using ::remove;
}
#endif

#include <boost/serialization/set.hpp>
#ifdef BOOST_HAS_HASH
#include <boost/serialization/hash_set.hpp>
#endif
#include <boost/archive/archive_exception.hpp>

#include "test_tools.hpp"

#include "A.hpp"

#if defined(__LIBCOMO__) || (defined(__SGI_STL_PORT) || defined(_STLPORT_VERSION))

namespace std {
    template<>
    struct less<A> {
        bool operator()(const A & lhs, const A & rhs) const {
            return lhs < rhs;
        }
    };
    template<>
    struct equal_to<A> {
        bool operator()(const A & lhs, const A & rhs) const {
            return lhs == rhs;
        }
    };
}   // namespace std

namespace BOOST_STD_EXTENSION_NAMESPACE {
    template<>
    struct hash<A> {
        std::size_t operator()(const A& a) const {
            return (std::size_t)a;
        }
    };
}   // BOOST_STD_EXTENSION_NAMESPACE

#endif

int test_main( int /* argc */, char* /* argv */[] )
{
    const char * testfile = boost::archive::tmpnam(NULL);
    BOOST_REQUIRE(NULL != testfile);

    // test array of objects
    std::set<A> aset;
    aset.insert(A());
    aset.insert(A());
    {   
        test_ostream os(testfile, TEST_STREAM_FLAGS);
        test_oarchive oa(os);
        oa << boost::serialization::make_nvp("aset", aset);
    }
    std::set<A> aset1;
    {
        test_istream is(testfile, TEST_STREAM_FLAGS);
        test_iarchive ia(is);
        ia >> boost::serialization::make_nvp("aset", aset1);
    }
    BOOST_CHECK(aset == aset1);
    
    std::multiset<A> amultiset;
    amultiset.insert(A());
    amultiset.insert(A());
    {   
        test_ostream os(testfile, TEST_STREAM_FLAGS);
        test_oarchive oa(os);
        oa << boost::serialization::make_nvp("amultiset", amultiset);
    }
    std::multiset<A> amultiset1;
    {
        test_istream is(testfile, TEST_STREAM_FLAGS);
        test_iarchive ia(is);
        ia >> boost::serialization::make_nvp("amultiset", amultiset1);
    }
    BOOST_CHECK(amultiset == amultiset1);
    
    #ifdef BOOST_HAS_HASH
    // test array of objects
    BOOST_STD_EXTENSION_NAMESPACE::hash_set<A> ahash_set;
    ahash_set.insert(A());
    ahash_set.insert(A());
    {   
        test_ostream os(testfile, TEST_STREAM_FLAGS);
        test_oarchive oa(os);
        oa << boost::serialization::make_nvp("ahash_set", ahash_set);
    }
    BOOST_STD_EXTENSION_NAMESPACE::hash_set<A> ahash_set1;
    {
        test_istream is(testfile, TEST_STREAM_FLAGS);
        test_iarchive ia(is);
        ia >> boost::serialization::make_nvp("ahash_set", ahash_set1);
    }
    // at least one library - MSL notes: it doesn't make much sense
    // to implement the == operator for hash collections - but goes ahead
    // does it anyway even though it doesn't seem to work.  So sort into
    // vectors and then compare.
    std::vector<A> tvec, tvec1;
    std::copy(ahash_set.begin(), ahash_set.end(), std::back_inserter(tvec));
    std::sort(tvec.begin(), tvec.end());
    std::copy(ahash_set1.begin(), ahash_set1.end(), std::back_inserter(tvec1));
    std::sort(tvec1.begin(), tvec1.end());
    BOOST_CHECK(tvec == tvec1);
    
    BOOST_STD_EXTENSION_NAMESPACE::hash_multiset<A> ahash_multiset;
    ahash_multiset.insert(A());
    ahash_multiset.insert(A());
    {   
        test_ostream os(testfile, TEST_STREAM_FLAGS);
        test_oarchive oa(os);
        oa << boost::serialization::make_nvp("ahash_multiset", ahash_multiset);
    }
    BOOST_STD_EXTENSION_NAMESPACE::hash_multiset<A> ahash_multiset1;
    {
        test_istream is(testfile, TEST_STREAM_FLAGS);
        test_iarchive ia(is);
        ia >> boost::serialization::make_nvp("ahash_multiset", ahash_multiset1);
    }
    // at least one library - MSL notes: it doesn't make much sense
    // to implement the == operator for hash collections - but goes ahead
    // does it anyway even though it doesn't seem to work.  So sort into
    // vectors and then compare.
    tvec.clear();
    tvec1.clear();
    std::copy(ahash_multiset.begin(), ahash_multiset.end(), std::back_inserter(tvec));
    std::sort(tvec.begin(), tvec.end());
    std::copy(ahash_multiset1.begin(), ahash_multiset1.end(), std::back_inserter(tvec1));
    std::sort(tvec1.begin(), tvec1.end());
    BOOST_CHECK(tvec == tvec1);
    
    #endif

    std::remove(testfile);
    return boost::exit_success;
}
