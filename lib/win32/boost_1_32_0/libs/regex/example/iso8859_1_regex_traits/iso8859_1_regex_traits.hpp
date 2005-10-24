/*
 * (C) Copyright Christain Engstrom 2001.
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#ifndef BOOST_ISO8859_1_REGEX_TRAITS_HPP
#define BOOST_ISO8859_1_REGEX_TRAITS_HPP
  
class iso8859_1_regex_traits : public boost::c_regex_traits<char> {
private:
  typedef boost::c_regex_traits<char> base;
public:
  iso8859_1_regex_traits() {}
  ~iso8859_1_regex_traits() {}

  bool is_class(char c, boost::uint_fast32_t f) const;
  char translate(char c, bool icase) const;
};

#endif


