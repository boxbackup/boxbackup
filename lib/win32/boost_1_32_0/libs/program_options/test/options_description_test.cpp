// Copyright Vladimir Prus 2002-2004.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)


#include <boost/program_options/options_description.hpp>
using namespace boost::program_options;

#include <boost/function.hpp>
using namespace boost;

#define BOOST_INCLUDE_MAIN  // for testing, include rather than link
#include <boost/test/test_tools.hpp>

#include <utility>
using namespace std;

/*  This is very wierd test case -- it tests trivial things. After writing it,
    I think that XP folks must be somehow wrong.
*/
void test_option_description_construction()
{
    option_description d1("a", new untyped_value(), "desc1");
    BOOST_TEST(d1.long_name() == "a");
    BOOST_TEST(d1.description() == "desc1");
    BOOST_TEST(d1.semantic()->name() == "arg");

    // It is not possible to compare boost::function
    #if 0
    function<string, string> f1;
    BOOST_TEST(&option_description("x", "y", "z").
               validator(f1).validator() ==
               &f1);
    function<void, string> f2;
    BOOST_TEST(&option_description("x", "y", "z").
               notify(f2).notify() ==
               &f2);
    #endif

    option_description d4("foo,f", new untyped_value(), "desc1");
    BOOST_CHECK(d4.long_name() == "foo");
    BOOST_CHECK(d4.short_name() == "f");
}

void test_options_description()
{
    options_description desc;

    shared_ptr<option_description> d1(
        new option_description("first,f", new untyped_value(), ""));
    desc.add(d1);
    BOOST_TEST(desc.count("first") == 1);
    BOOST_TEST(desc.count("-f") == 1);
    BOOST_TEST(desc.keys().size() == 2);
    BOOST_TEST(desc.keys().count("first") == 1);
    BOOST_TEST(desc.keys().count("-f") == 1);

    desc.add_options()
        ("second,s", new untyped_value())
        ("third,t", new untyped_value())
        ;
    BOOST_TEST(desc.count("second") == 1);
    BOOST_TEST(desc.count("-s") == 1);

    desc.add_options()
        (",x", new untyped_value)
        ;
    BOOST_TEST(desc.primary_keys().size() == 4);
    BOOST_TEST(desc.primary_keys().count("first") == 1);
    BOOST_TEST(desc.primary_keys().count("second") == 1);
    BOOST_TEST(desc.primary_keys().count("third") == 1);
    BOOST_TEST(desc.primary_keys().count("-x") == 1);
}

void test_approximation()
{
    options_description desc;
    desc.add_options()
    ("foo", new untyped_value())
    ("fee", new untyped_value())
    ("baz", new untyped_value());

    BOOST_TEST(desc.count_approx("f") == 2);
    BOOST_TEST(desc.count_approx("foo") == 1);
    set<string> a = desc.approximations("f");
    BOOST_TEST(a.size() == 2);
    BOOST_TEST(*a.begin() == "fee");
    BOOST_TEST(*(++a.begin()) == "foo");
}

int test_main(int, char* [])
{
    test_option_description_construction();
    test_options_description();
    test_approximation();
    return 0;
}
