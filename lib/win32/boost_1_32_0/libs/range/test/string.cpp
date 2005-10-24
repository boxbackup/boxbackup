// Boost.Range library
//
//  Copyright Thorsten Ottosen 2003-2004. Use, modification and
//  distribution is subject to the Boost Software License, Version
//  1.0. (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org/libs/range/
//


#include <boost/detail/workaround.hpp>

#if BOOST_WORKAROUND(__BORLANDC__, BOOST_TESTED_AT(0x564))
#  pragma warn -8091 // supress warning in Boost.Test
#  pragma warn -8057 // unused argument argc/argv in Boost.Test
#endif

#include <boost/range/functions.hpp>
#include <boost/range/metafunctions.hpp>
#include <boost/static_assert.hpp>
#include <boost/type_traits.hpp>
#include <boost/test/test_tools.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/config.hpp>
#include <vector>
#include <fstream>
#include <algorithm>

template< typename Container, typename T >
BOOST_DEDUCED_TYPENAME boost::range_iterator<Container>::type
find( Container& c,  T value )
{
    return std::find( boost::begin( c ), boost::end( c ), value );
}

template< typename Container, typename T >
BOOST_DEDUCED_TYPENAME boost::range_const_iterator<Container>::type
find( const Container& c, T value )
{
    return std::find( boost::begin( c ), boost::end( c ), value );
}

std::vector<char> 
check_rvalue_return()
{
    return std::vector<char>( 10, 'm' ); 
}

using namespace boost;


void check_char()
{
    typedef char*                  char_iterator_t;
    typedef char                   char_array_t[10];
    const char*      char_s           = "a string";
    char             my_string[]      = "another string";
    const unsigned   my_string_length = 14;
    char*            char_s2          = "a string";
    
    BOOST_STATIC_ASSERT(( is_same<  range_value<char_iterator_t>::type, 
                                    detail::iterator_traits<char_iterator_t>::value_type>::value ));
    BOOST_STATIC_ASSERT(( is_same<  range_iterator<char_iterator_t>::type, char_iterator_t >::value ));
    BOOST_STATIC_ASSERT(( is_same<  range_const_iterator<char_iterator_t>::type, const char* >::value ));
    BOOST_STATIC_ASSERT(( is_same<  range_difference<char_iterator_t>::type,                           
                                    ::std::ptrdiff_t >::value ));
    BOOST_STATIC_ASSERT(( is_same<  range_size<char_iterator_t>::type, std::size_t >::value ));
    BOOST_STATIC_ASSERT(( is_same<  range_result_iterator<char_iterator_t>::type, char_iterator_t >::value ));
    BOOST_STATIC_ASSERT(( is_same<  range_result_iterator<const char*>::type, const char* >::value ));
    
    BOOST_STATIC_ASSERT(( is_same< range_value<char_array_t>::type, 
                                    char>::value ));
    BOOST_STATIC_ASSERT(( is_same< range_iterator<char_array_t>::type, char* >::value ));
    BOOST_STATIC_ASSERT(( is_same< range_const_iterator<char_array_t>::type, const char* >::value ));
    BOOST_STATIC_ASSERT(( is_same< range_difference<char_array_t>::type,                           
                                    ::std::ptrdiff_t >::value ));
    BOOST_STATIC_ASSERT(( is_same< range_size<char_array_t>::type, std::size_t >::value ));
    BOOST_STATIC_ASSERT(( is_same< range_result_iterator<char_array_t>::type, char* >::value ));
    BOOST_STATIC_ASSERT(( is_same< range_result_iterator<const char_array_t>::type, const char* >::value ));

    BOOST_CHECK_EQUAL( begin( char_s ), char_s );
    std::size_t sz = size( char_s );
    const char* end1 = begin( char_s ) + sz; 
    BOOST_CHECK_EQUAL( end( char_s ), end1 );
    BOOST_CHECK_EQUAL( empty( char_s ), (char_s == 0 || char_s[0] == char()) );
    BOOST_CHECK_EQUAL( sz, std::char_traits<char>::length( char_s ) );
/*    
    BOOST_CHECK_EQUAL( begin( char_s2 ), char_s2 );
    std::size_t sz2 = size( char_s2 );
    const char* end12 = begin( char_s2 ) + sz;
    BOOST_CHECK_EQUAL( end( char_s2 ), end12 );
    BOOST_CHECK_EQUAL( empty( char_s2 ), (char_s2 == 0 || char_s2[0] == char()) );
    BOOST_CHECK_EQUAL( sz2, std::char_traits<char>::length( char_s2 ) );
*/
    BOOST_CHECK_EQUAL( begin( my_string ), my_string );
    range_iterator<char_array_t>::type end2 = begin( my_string ) + size( my_string );
    range_iterator<char_array_t>::type end3 = end( my_string );
    BOOST_CHECK_EQUAL( end3, end2 );
    BOOST_CHECK_EQUAL( empty( my_string ), (my_string == 0 || my_string[0] == char()) );
    BOOST_CHECK_EQUAL( size( my_string ), my_string_length );
    BOOST_CHECK_EQUAL( size( my_string ), std::char_traits<char>::length( my_string ) );

    char to_search = 'n';
    BOOST_CHECK( find( char_s, to_search ) != end( char_s ) );
    BOOST_CHECK( find( my_string, to_search ) != end( my_string ) );
   
}



void check_string()
{
    check_char();
//    check_char<volatile char>();
//    check_char<const char>();
//    check_char<const volatile char>();
    
#ifndef BOOST_NO_STD_WSTRING
    typedef wchar_t*               wchar_iterator_t;          
    const wchar_t*  char_ws      = L"a wide string";
    wchar_t         my_wstring[] = L"another wide string";
    wchar_t*        char_ws2     = L"a wide string";
            
    BOOST_STATIC_ASSERT(( is_same< range_value<wchar_iterator_t>::type, 
                                   detail::iterator_traits<wchar_iterator_t>::value_type>::value ));
    BOOST_STATIC_ASSERT(( is_same< range_iterator<wchar_iterator_t>::type, wchar_iterator_t >::value ));
    BOOST_STATIC_ASSERT(( is_same< range_const_iterator<wchar_iterator_t>::type, const wchar_t* >::value ));
    BOOST_STATIC_ASSERT(( is_same< range_difference<wchar_iterator_t>::type,                           
                                   detail::iterator_traits<wchar_iterator_t>::difference_type >::value ));
    BOOST_STATIC_ASSERT(( is_same< range_size<wchar_iterator_t>::type, std::size_t >::value ));
    BOOST_STATIC_ASSERT(( is_same< range_result_iterator<wchar_iterator_t>::type, wchar_iterator_t >::value ));
    BOOST_STATIC_ASSERT(( is_same< range_result_iterator<const wchar_t*>::type, const wchar_t* >::value ));
    
    std::size_t sz = size( char_ws );
    BOOST_CHECK_EQUAL( begin( char_ws ), char_ws );
    BOOST_CHECK_EQUAL( end( char_ws ), (begin( char_ws ) + sz) );
    BOOST_CHECK_EQUAL( empty( char_ws ), (char_ws == 0 || char_ws[0] == wchar_t()) );
    BOOST_CHECK_EQUAL( sz, std::char_traits<wchar_t>::length( char_ws ) );
 /*       
    std::size_t sz2 = size( char_ws2 );
    BOOST_CHECK_EQUAL( begin( char_ws2 ), char_ws2 );
    BOOST_CHECK_EQUAL( end( char_ws2 ), (begin( char_ws2 ) + sz2) );
    BOOST_CHECK_EQUAL( empty( char_ws2 ), (char_ws2 == 0 || char_ws2[0] == wchar_t()) );
    BOOST_CHECK_EQUAL( sz2, std::char_traits<wchar_t>::length( char_ws2 ) );
   */ 
    wchar_t to_search = L'n';
    BOOST_CHECK( find( char_ws, to_search ) != end( char_ws ) );    

#if BOOST_WORKAROUND(_MSC_VER, BOOST_TESTED_AT(1300))

    BOOST_CHECK( find( my_wstring, to_search ) != end( my_wstring ) );

#endif  
#endif
    
    find( check_rvalue_return(), 'n' );

}




#include <boost/test/included/unit_test_framework.hpp> 

using boost::unit_test_framework::test_suite;

test_suite* init_unit_test_suite( int argc, char* argv[] )
{
    test_suite* test = BOOST_TEST_SUITE( "Range Test Suite" );

    test->add( BOOST_TEST_CASE( &check_string ) );

    return test;
}






