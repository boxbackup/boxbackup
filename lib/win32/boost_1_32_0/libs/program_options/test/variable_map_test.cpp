// Copyright Vladimir Prus 2002-2004.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)


#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/detail/utf8_codecvt_facet.hpp>
using namespace boost::program_options;
// We'll use po::value everywhere to workaround vc6 bug.
namespace po = boost::program_options;

#include <boost/function.hpp>
using namespace boost;

#define BOOST_INCLUDE_MAIN  // for testing, include rather than link
#include <boost/test/test_tools.hpp>

#include <sstream>
using namespace std;

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

vector<string> sv(char* array[], unsigned size)
{
    vector<string> r;
    for (unsigned i = 0; i < size; ++i)
        r.push_back(array[i]);
    return r;
}

void test_variable_map()
{
    options_description desc;
    desc.add_options()
        ("foo,f", new untyped_value)
        ("bar,b", po::value<string>()->implicit())
        ("biz,z", po::value<string>()->implicit())
        ("baz", new untyped_value())
        ("output,o", new untyped_value(), "")
        ;
    char* cmdline3_[] = { "--foo='12'", "--bar=11", "-z3", "-ofoo" };
    vector<string> cmdline3 = sv(cmdline3_,
                                 sizeof(cmdline3_)/sizeof(cmdline3_[0]));
    parsed_options a3 = command_line_parser(cmdline3).options(desc).run();
    variables_map vm;
    store(a3, vm);
    notify(vm);
    BOOST_CRITICAL_TEST(vm.size() == 4);
    BOOST_TEST(vm["foo"].as<string>() == "'12'");
    BOOST_TEST(vm["bar"].as<string>() == "11");
    BOOST_TEST(vm.count("biz") == 1);
    BOOST_TEST(vm["biz"].as<string>() == "3");
    BOOST_TEST(vm["output"].as<string>() == "foo");

    int i;
    desc.add_options()
    ("zee", bool_switch(), "")
    ("zak", po::value<int>(&i), "")
    ("opt", bool_switch(), "");

    char* cmdline4_[] = { "--zee", "--zak=13" };
    vector<string> cmdline4 = sv(cmdline4_,
                                 sizeof(cmdline4_)/sizeof(cmdline4_[0]));
    parsed_options a4 = command_line_parser(cmdline4).options(desc).run();

    variables_map vm2;
    store(a4, vm2);
    notify(vm2);
    BOOST_CRITICAL_TEST(vm2.size() == 3);
    BOOST_TEST(vm2["zee"].as<bool>() == true);
    BOOST_TEST(vm2["zak"].as<int>() == 13);
    BOOST_TEST(vm2["opt"].as<bool>() == false);
    BOOST_TEST(i == 13);

    options_description desc2;
    desc2.add_options()
    ("vee", po::value<string>()->default_value("42"))
    ("voo", po::value<string>())
    ("iii", po::value<int>()->default_value(123))
    ;
    char* cmdline5_[] = {  "--voo=1" };
    vector<string> cmdline5 = sv(cmdline5_,
                                 sizeof(cmdline5_)/sizeof(cmdline5_[0]));
    parsed_options a5 = command_line_parser(cmdline5).options(desc2).run();

    variables_map vm3;
    store(a5, vm3);
    notify(vm3);
    BOOST_CRITICAL_TEST(vm3.size() == 3);
    BOOST_TEST(vm3["vee"].as<string>() == "42");
    BOOST_TEST(vm3["voo"].as<string>() == "1");
    BOOST_TEST(vm3["iii"].as<int>() == 123);
}

int stored_value;
void notifier(const vector<int>& v)
{
    stored_value = v.front();
}

void test_semantic_values()
{
    options_description desc;
    desc.add_options()
    ("foo", new untyped_value())
    ("bar", po::value<int>())
    ("biz", po::value< vector<string> >())
    ("baz", po::value< vector<string> >()->multitoken())
    ("int", po::value< vector<int> >()->notifier(&notifier))
    ;

       
    parsed_options parsed(&desc);
    vector<option>& options = parsed.options;
    vector<string> v;
    v.push_back("q");
    options.push_back(option("foo", vector<string>(1, "1")));
    options.push_back(option("biz", vector<string>(1, "a")));
    options.push_back(option("baz", v));
    options.push_back(option("bar", vector<string>(1, "1")));
    options.push_back(option("biz", vector<string>(1, "b x")));
    v.push_back("w");
    options.push_back(option("baz", v));

    variables_map vm;
    store(parsed, vm);
    notify(vm);
    BOOST_REQUIRE(vm.count("biz") == 1);
    BOOST_REQUIRE(vm.count("baz") == 1);
    const vector<string> av = vm["biz"].as< vector<string> >();
    const vector<string> av2 = vm["baz"].as< vector<string> >();
    string exp1[] = { "a", "b x" };
    BOOST_CHECK(av == vector<string>(exp1, exp1 + 2));
    string exp2[] = { "q", "q", "w" };
    BOOST_CHECK(av2 == vector<string>(exp2, exp2 + 3));

    options.push_back(option("int", vector<string>(1, "13")));

    variables_map vm2;
    store(parsed, vm2);
    notify(vm2);
    BOOST_REQUIRE(vm2.count("int") == 1);
    BOOST_CHECK(vm2["int"].as< vector<int> >() == vector<int>(1, 13));
    BOOST_CHECK_EQUAL(stored_value, 13);

    vector<option> saved_options = options;
    
    options.push_back(option("bar", vector<string>(1, "2")));
    variables_map vm3;
    BOOST_CHECK_THROW(store(parsed, vm3), multiple_occurrences);

    options = saved_options;
    // Now try passing two int in one 'argv' element.
    // This should not work.
    options.push_back(option("int", vector<string>(1, "2 3")));
    variables_map vm4;
    BOOST_CHECK_THROW(store(parsed, vm4), validation_error);
}

void test_priority()
{
    options_description desc;
    desc.add_options()
    // Value of this option will be specified in two sources,
    // and only first one should be used.
    ("first", po::value< vector<int > >())
    // Value of this option will have default value in the first source,
    // and explicit assignment in the second, so the second should be used.
    ("second", po::value< vector<int > >()->default_value(vector<int>(1, 1), ""))
    ("aux", po::value< vector<int > >())
     // This will have values in both sources, and values should be combined
    ("include", po::value< vector<int> >()->composing())
    ;

    char* cmdline1_[] = { "--first=1", "--aux=10", "--first=3", "--include=1" };
    vector<string> cmdline1 = sv(cmdline1_, 
                                 sizeof(cmdline1_)/sizeof(cmdline1_[0]));

    parsed_options p1 = command_line_parser(cmdline1).options(desc).run();

    char* cmdline2_[] = { "--first=12", "--second=7", "--include=7" };
    vector<string> cmdline2 = sv(cmdline2_, 
                                 sizeof(cmdline2_)/sizeof(cmdline2_[0]));

    parsed_options p2 = command_line_parser(cmdline2).options(desc).run();

    variables_map vm;
    store(p1, vm);

    BOOST_REQUIRE(vm.count("first") == 1);    
    BOOST_REQUIRE(vm["first"].as< vector<int> >().size() == 2);
    BOOST_CHECK_EQUAL(vm["first"].as< vector<int> >()[0], 1);
    BOOST_CHECK_EQUAL(vm["first"].as< vector<int> >()[1], 3);

    BOOST_REQUIRE(vm.count("second") == 1);    
    BOOST_REQUIRE(vm["second"].as< vector<int> >().size() == 1);
    BOOST_CHECK_EQUAL(vm["second"].as< vector<int> >()[0], 1);

    store(p2, vm);

    // Value should not change.
    BOOST_REQUIRE(vm.count("first") == 1);    
    BOOST_REQUIRE(vm["first"].as< vector<int> >().size() == 2);
    BOOST_CHECK_EQUAL(vm["first"].as< vector<int> >()[0], 1);
    BOOST_CHECK_EQUAL(vm["first"].as< vector<int> >()[1], 3);

    // Value should change to 7
    BOOST_REQUIRE(vm.count("second") == 1);    
    BOOST_REQUIRE(vm["second"].as< vector<int> >().size() == 1);
    BOOST_CHECK_EQUAL(vm["second"].as< vector<int> >()[0], 7);

    BOOST_REQUIRE(vm.count("include") == 1);
    BOOST_REQUIRE(vm["include"].as< vector<int> >().size() == 2);
    BOOST_CHECK_EQUAL(vm["include"].as< vector<int> >()[0], 1);
    BOOST_CHECK_EQUAL(vm["include"].as< vector<int> >()[1], 7);
}



int test_main(int, char* [])
{
    test_variable_map();
    test_semantic_values();
    test_priority();
    return 0;
}

