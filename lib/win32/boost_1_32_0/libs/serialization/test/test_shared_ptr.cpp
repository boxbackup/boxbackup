/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// test_shared_ptr.cpp

// (C) Copyright 2002 Robert Ramey- http://www.rrsd.com - David Tonge  . 
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
//  See http://www.boost.org for updates, documentation, and revision history.

#include <fstream>

#include <cstdio> // remove
#include <boost/config.hpp>
#if defined(BOOST_NO_STDC_NAMESPACE)
namespace std{ 
    using ::remove;
}
#endif

#include <boost/serialization/shared_ptr.hpp>
#include "test_tools.hpp"
#include <boost/serialization/nvp.hpp>
#include <boost/serialization/export.hpp>
#include <boost/type_traits/broken_compiler_spec.hpp>


// This is a simple class.  It contains a counter of the number
// of objects of this class which have been instantiated.
class A
{
private:
    friend class boost::serialization::access;
    int x;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int /* file_version */){
        ar & BOOST_SERIALIZATION_NVP(x);
    }
public:
    static int count;
    A(){++count;}    // default constructor
    virtual ~A(){--count;}   // default destructor
};

BOOST_BROKEN_COMPILER_TYPE_TRAITS_SPECIALIZATION(A)

// B is a subclass of A
class B : public A
{
private:
    friend class boost::serialization::access;
    int y;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int /* file_version */){
        ar & BOOST_SERIALIZATION_BASE_OBJECT_NVP(A);
    }
public:
    static int count;
    B() : A() {};
    virtual ~B() {};
};

// B needs to be exported because its serialized via a base class pointer
BOOST_SHARED_POINTER_EXPORT(B)

int A::count = 0;

void test_save_and_load(boost::shared_ptr<A>& spa)
{
    const char * testfile = boost::archive::tmpnam(NULL);
    BOOST_REQUIRE(NULL != testfile);

    // trivial test
    {
        test_ostream os(testfile, TEST_STREAM_FLAGS);
        test_oarchive oa(os);
        oa << BOOST_SERIALIZATION_NVP(spa);
    }
    spa.reset();
    {
        test_istream is(testfile, TEST_STREAM_FLAGS);
        test_iarchive ia(is);
        ia >> BOOST_SERIALIZATION_NVP(spa);
    }
}

// Run tests by serializing two shared_ptrs into an archive,
// clearing them (deleting the objects) and then reloading the
// objects back from an archive.
void test_save_and_load2(boost::shared_ptr<A>& first, boost::shared_ptr<A>& second)
{
    const char * testfile = boost::archive::tmpnam(NULL);
    BOOST_REQUIRE(NULL != testfile);

    // Save
    {
        test_ostream os(testfile, TEST_STREAM_FLAGS);
        test_oarchive oa(os);
        oa << BOOST_SERIALIZATION_NVP(first);
        oa << BOOST_SERIALIZATION_NVP(second);
    }

    // Clear the pointers, thereby destroying the objects they contain
    first.reset();
    second.reset();

    // Load
    {
        test_istream is(testfile, TEST_STREAM_FLAGS);
        test_iarchive ia(is);
        ia >> BOOST_SERIALIZATION_NVP(first);
        ia >> BOOST_SERIALIZATION_NVP(second);
    }
    BOOST_CHECK(boost::get_pointer(first) == boost::get_pointer(second));
    std::remove(testfile);
}

// This does the tests
int test_main(int /* argc */, char * /* argv */[])
{
    // These are our shared_ptrs
    boost::shared_ptr<A> spa;
    boost::shared_ptr<A> spa1;

    // trivial test 1
    test_save_and_load(spa);

    //trivival test 2
    spa = boost::shared_ptr<A>(new A);
    test_save_and_load(spa);

    // Try to save and load pointers to As, to a text archive
    spa = boost::shared_ptr<A>(new A);
    spa1 = spa;
    test_save_and_load2(spa, spa1);

    spa.reset();
    spa1.reset();

    // Try to save and load pointers to Bs, to a text archive
    spa = boost::shared_ptr<A>(new B);
    spa1 = spa;
    test_save_and_load2(spa, spa1);

    // obj of type B gets destroyed
    // as smart_ptr goes out of scope
    return boost::exit_success;
}
