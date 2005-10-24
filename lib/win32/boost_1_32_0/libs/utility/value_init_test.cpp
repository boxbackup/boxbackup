// (C) 2002, Fernando Luis Cacciola Carballal.
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
// Test program for "boost/utility/value_init.hpp"
//
// Initial: 21 Agu 2002

#include <iostream>
#include <string>

#include "boost/utility/value_init.hpp"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#include "boost/test/minimal.hpp"

//
// Sample POD type
//
struct POD
{
  POD () : c(0), i(0), f(0) {}

  POD ( char c_, int i_, float f_ ) : c(c_), i(i_), f(f_) {}

  friend std::ostream& operator << ( std::ostream& os, POD const& pod )
    { return os << '(' << pod.c << ',' << pod.i << ',' << pod.f << ')' ; }

  friend bool operator == ( POD const& lhs, POD const& rhs )
    { return lhs.f == rhs.f && lhs.c == rhs.c && lhs.i == rhs.i ; }

  float f;
  char  c;
  int   i;
} ;

//
// Sample non POD type
//
struct NonPODBase
{
  virtual ~NonPODBase() {}
} ;
struct NonPOD : NonPODBase
{
  NonPOD () : id() {}
  NonPOD ( std::string const& id_) : id(id_) {}

  friend std::ostream& operator << ( std::ostream& os, NonPOD const& npod )
    { return os << '(' << npod.id << ')' ; }

  friend bool operator == ( NonPOD const& lhs, NonPOD const& rhs )
    { return lhs.id == rhs.id ; }

  std::string id ;
} ;

template<class T>
void test ( T const& y, T const& z )
{
  boost::value_initialized<T> x ;
  BOOST_TEST ( y == x ) ;
  BOOST_TEST ( y == boost::get(x) ) ;
  static_cast<T&>(x) = z ;
  boost::get(x) = z ;
  BOOST_TEST ( x == z ) ;

  boost::value_initialized<T> const x_c ;
  BOOST_TEST ( y == x_c ) ;
  BOOST_TEST ( y == boost::get(x_c) ) ;
  T& x_c_ref = x_c ;
  x_c_ref = z ;
  BOOST_TEST ( x_c == z ) ;

#if !BOOST_WORKAROUND(BOOST_MSVC, < 1300)
  boost::value_initialized<T const> cx ;
  BOOST_TEST ( y == cx ) ;
  BOOST_TEST ( y == boost::get(cx) ) ;

  boost::value_initialized<T const> const cx_c ;
  BOOST_TEST ( y == cx_c ) ;
  BOOST_TEST ( y == boost::get(cx_c) ) ;
#endif
}

int test_main(int, char **)
{
  test( 0,1234 ) ;
  test( 0.0,12.34 ) ;
  test( POD(0,0,0.0), POD('a',1234,56.78) ) ;
  test( NonPOD( std::string() ), NonPOD( std::string("something") ) ) ;

  return 0;
}


unsigned int expected_failures = 0;





