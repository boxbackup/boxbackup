// Copyright Douglas Gregor 2002-2003. Use, modification and
// distribution is subject to the Boost Software License, Version
// 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/logic/tribool.hpp>
#include <boost/test/minimal.hpp>
#include <iostream>

int test_main(int, char*[])
{
  using namespace boost::logic;

  tribool x; // false
  tribool y(true); // true
  tribool z(indeterminate); // indeterminate

  BOOST_TEST(!x);
  BOOST_TEST(x == false);
  BOOST_TEST(false == x);
  BOOST_TEST(x != true);
  BOOST_TEST(true != x);
  BOOST_TEST(indeterminate(x == indeterminate));
  BOOST_TEST(indeterminate(indeterminate == x));
  BOOST_TEST(indeterminate(x != indeterminate));
  BOOST_TEST(indeterminate(indeterminate != x));
  BOOST_TEST(x == x);
  BOOST_TEST(!(x != x));
  BOOST_TEST(!(x && true));
  BOOST_TEST(!(true && x));
  BOOST_TEST(x || true);
  BOOST_TEST(true || x);

  BOOST_TEST(y);
  BOOST_TEST(y == true);
  BOOST_TEST(true == y);
  BOOST_TEST(y != false);
  BOOST_TEST(false != y);
  BOOST_TEST(indeterminate(y == indeterminate));
  BOOST_TEST(indeterminate(indeterminate == y));
  BOOST_TEST(indeterminate(y != indeterminate));
  BOOST_TEST(indeterminate(indeterminate != y));
  BOOST_TEST(y == y);
  BOOST_TEST(!(y != y));

  BOOST_TEST(indeterminate(z || !z));
  BOOST_TEST(indeterminate(z == true));
  BOOST_TEST(indeterminate(true == z));
  BOOST_TEST(indeterminate(z == false));
  BOOST_TEST(indeterminate(false == z));
  BOOST_TEST(indeterminate(z == indeterminate));
  BOOST_TEST(indeterminate(indeterminate == z));
  BOOST_TEST(indeterminate(z != indeterminate));
  BOOST_TEST(indeterminate(indeterminate != z));
  BOOST_TEST(indeterminate(z == z));
  BOOST_TEST(indeterminate(z != z));

  BOOST_TEST(!(x == y));
  BOOST_TEST(x != y);
  BOOST_TEST(indeterminate(x == z));
  BOOST_TEST(indeterminate(x != z));
  BOOST_TEST(indeterminate(y == z));
  BOOST_TEST(indeterminate(y != z));

  BOOST_TEST(!(x && y));
  BOOST_TEST(x || y);
  BOOST_TEST(!(x && z));
  BOOST_TEST(indeterminate(y && z));
  BOOST_TEST(indeterminate(z && z));
  BOOST_TEST(indeterminate(z || z));
  BOOST_TEST(indeterminate(x || z));
  BOOST_TEST(y || z);

  BOOST_TEST(indeterminate(y && indeterminate));
  BOOST_TEST(indeterminate(indeterminate && y));
  BOOST_TEST(!(x && indeterminate));
  BOOST_TEST(!(indeterminate && x));

  BOOST_TEST(indeterminate || y);
  BOOST_TEST(y || indeterminate);
  BOOST_TEST(indeterminate(x || indeterminate));
  BOOST_TEST(indeterminate(indeterminate || x));

  // Test the if (z) ... else (!z) ... else ... idiom
  if (z) {
    BOOST_TEST(false);
  }
  else if (!z) {
    BOOST_TEST(false);
  }
  else {
    BOOST_TEST(true);
  }

  z = true;
  if (z) {
    BOOST_TEST(true);
  }
  else if (!z) {
    BOOST_TEST(false);
  }
  else {
    BOOST_TEST(false);
  }

  z = false;
  if (z) {
    BOOST_TEST(false);
  }
  else if (!z) {
    BOOST_TEST(true);
  }
  else {
    BOOST_TEST(false);
  }

  std::cout << "no errors detected\n";
  return 0;
}
