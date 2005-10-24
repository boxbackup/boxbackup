// Copyright Vladimir Prus 2002-2004.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)


#include <boost/program_options/parsers.hpp>
#include <boost/program_options/options_description.hpp>
using namespace boost::program_options;
// We'll use po::value everywhere to workaround vc6 bug.
namespace po = boost::program_options;

#include <boost/function.hpp>
using namespace boost;

#define BOOST_INCLUDE_MAIN  // for testing, include rather than link
#include <boost/test/test_tools.hpp>

#include <sstream>
using namespace std;

#include <cstdlib> // for putenv

#define TEST_CHECK_THROW(expression, exception, description) \
    try \
    { \
        expression; \
        BOOST_ERROR(description);\
        throw 10; \
    } \
    catch(exception &) \
    { \
    }

pair<string, vector< vector<string> > > msp(const string& s1)
{
    return std::make_pair(s1, vector< vector<string> >());
}


pair<string, vector< vector<string> > > msp(const string& s1, const string& s2)
{
    vector< vector<string> > v(1);
    v[0].push_back(s2);
    return std::make_pair(s1, v);
}

void check_value(const option& option, const char* name, const char* value)
{
    BOOST_CHECK(option.string_key == name);
    BOOST_REQUIRE(option.value.size() == 1);
    BOOST_CHECK(option.value.front() == value);
}

vector<string> sv(char* array[], unsigned size)
{
    vector<string> r;
    for (unsigned i = 0; i < size; ++i)
        r.push_back(array[i]);
    return r;
}

void test_command_line()
{
    // The following commented out blocks used to test parsing
    // command line without syntax specification behaviour.
    // It is disabled now and probably will never be enabled again:
    // it is not possible to figure out what command line means without
    // user's help.
    #if 0
    char* cmdline1[] = { "--a", "--b=12", "-f", "-g4", "-", "file" };

    options_and_arguments a1 =
        parse_command_line(cmdline1,
                           cmdline1 + sizeof(cmdline1)/sizeof(cmdline1[0]));

    BOOST_CRITICAL_TEST(a1.options().size() == 4);
    BOOST_TEST(a1.options()[0] == msp("a", ""));
    BOOST_TEST(a1.options()[1] == msp("b", "12"));
    BOOST_TEST(a1.options()[2] == msp("-f", ""));
    BOOST_TEST(a1.options()[3] == msp("-g", "4"));
    BOOST_CRITICAL_TEST(a1.arguments().size() == 2);
    BOOST_TEST(a1.arguments()[0] == "-");
    BOOST_TEST(a1.arguments()[1] == "file");

    char* cmdline2[] = { "--a", "--", "file" };

    options_and_arguments a2 =
        parse_command_line(cmdline2,
                           cmdline2 + sizeof(cmdline2)/sizeof(cmdline2[0]));

    BOOST_CRITICAL_TEST(a2.options().size() == 1);
    BOOST_TEST(a2.options()[0] == msp("a", ""));
    BOOST_TEST(a2.arguments().size() == 1);
    BOOST_TEST(a2.arguments()[0] == "file");
    #endif
    
    options_description desc;
    desc.add_options()
        ("foo,f", new untyped_value(), "")
        // Explicit qualification is a workaround for vc6
        ("bar,b", po::value<std::string>()->implicit(), "")
        ("baz", new untyped_value())
        ("plug*", new untyped_value())
        ;
    char* cmdline3_[] = { "--foo=12", "-f4", "--bar=11", "--bar", "-b4", "-b",
                         "--plug3=10"};
    vector<string> cmdline3 = sv(cmdline3_,
                                 sizeof(cmdline3_)/sizeof(cmdline3_[0]));
    vector<option> a3 = 
        command_line_parser(cmdline3).options(desc).run().options;
                       
    BOOST_CRITICAL_TEST(a3.size() == 7);

    check_value(a3[0], "foo", "12");
    check_value(a3[1], "foo", "4");
    check_value(a3[2], "bar", "11");

    BOOST_TEST(a3[3].string_key == "bar");
    BOOST_CRITICAL_TEST(a3[3].value.size() == 0);

    check_value(a3[4], "bar", "4");

    BOOST_TEST(a3[5].string_key == "bar");
    BOOST_CRITICAL_TEST(a3[5].value.size() == 0);

    check_value(a3[6], "plug3", "10");
}

void test_config_file()
{
    options_description desc;
    desc.add_options()
        ("gv1", new untyped_value)
        ("gv2", new untyped_value)
        ("plug*", new untyped_value)
        ("m1.v1", new untyped_value)
        ("m1.v2", new untyped_value)
        ("b", bool_switch())
    ;

    const char content1[] =
    " gv1 = 0#asd\n"
    "plug3 = 7\n"
    "b = true\n"
    "[m1]\n"
    "v1 = 1\n"
    "\n"
    "v2 = 2\n"    
    ;

    stringstream ss(content1);
    vector<option> a1 = parse_config_file(ss, desc).options;
    BOOST_CRITICAL_TEST(a1.size() == 5);
    check_value(a1[0], "gv1", "0");
    check_value(a1[1], "plug3", "7");
    check_value(a1[2], "b", "true");
    check_value(a1[3], "m1.v1", "1");
    check_value(a1[4], "m1.v2", "2");

}

void test_environment()
{
    options_description desc;
    desc.add_options()
        ("foo", new untyped_value, "")
        ("bar", new untyped_value, "")
        ;

#if defined(_WIN32) && ! defined(__BORLANDC__)
    _putenv("PO_TEST_FOO=1");
#else
    putenv("PO_TEST_FOO=1");
#endif
    parsed_options p = parse_environment(desc, "PO_TEST_");

    BOOST_REQUIRE(p.options.size() == 1);
    BOOST_CHECK(p.options[0].string_key == "foo");
    BOOST_REQUIRE(p.options[0].value.size() == 1);
    BOOST_CHECK(p.options[0].value[0] == "1");

    //TODO: since 'bar' does not allow a value, it cannot appear in environemt,
    // which already has a value.
}

int test_main(int, char* [])
{
    test_command_line();
    test_config_file();
    test_environment();
    return 0;
}

