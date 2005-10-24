// (C) Copyright Jeremy Siek 2001.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
// Sample output:
//  mask =      101010101010
//  Enter a 12-bit bitset in binary: 100110101101
//  x =        100110101101
//  As ulong:  2477
//  And with mask: 100010101000
//  Or with mask:  101110101111


#include <iostream>
#include <boost/dynamic_bitset.hpp>

int main(int, char*[]) {
  const boost::dynamic_bitset<> mask(12, 2730ul); 
  std::cout << "mask = " << mask << std::endl;

  boost::dynamic_bitset<> x(12);
  std::cout << "x.size()=" << x.size() << std::endl;

  std::cout << "Enter a 12-bit bitset in binary: " << std::flush;
  if (std::cin >> x) {
    std::cout << "input number:     " << x << std::endl;
    std::cout << "As unsigned long: " << x.to_ulong() << std::endl;
    std::cout << "And with mask:    " << (x & mask) << std::endl;
    std::cout << "Or with mask:     " << (x | mask) << std::endl;
    std::cout << "Shifted left:     " << (x << 1) << std::endl;
    std::cout << "Shifted right:    " << (x >> 1) << std::endl;
  }
  return EXIT_SUCCESS;
}
