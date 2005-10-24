// Copyright David Abrahams 2003.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
#ifndef STATIC_ASSERT_SAME_DWA2003530_HPP
# define STATIC_ASSERT_SAME_DWA2003530_HPP

# include <boost/type.hpp>
# include <boost/static_assert.hpp>

#ifndef BOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION
template <class T, class U>
struct static_assert_same_base;

template <class T>
struct static_assert_same_base<T,T>
{
    enum { value = 1 };
};

template <class T, class U>
struct static_assert_same : static_assert_same_base<T,U> {};

#else
# include <boost/mpl/if.hpp>
# include <boost/mpl/bool.hpp>
# include <boost/type_traits/is_same.hpp>

template <class T, class U>
struct static_assert_same
    : boost::mpl::if_<boost::is_same<T,U>,boost::mpl::true_,void>::type
{};
#endif

#define STATIC_ASSERT_SAME( T1,T2 )                   \
   enum { BOOST_JOIN(boost_static_assert_enum_, __LINE__) \
          = static_assert_same<T1,T2>::value }

#endif // STATIC_ASSERT_SAME_DWA2003530_HPP
