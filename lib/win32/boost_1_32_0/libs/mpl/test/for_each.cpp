
// Copyright Aleksey Gurtovoy 2000-2004
//
// Distributed under the Boost Software License, Version 1.0. 
// (See accompanying file LICENSE_1_0.txt or copy at 
// http://www.boost.org/LICENSE_1_0.txt)
//
// See http://www.boost.org/libs/mpl for documentation.

// $Source: /cvsroot/boost/boost/libs/mpl/test/for_each.cpp,v $
// $Date: 2004/10/30 08:27:47 $
// $Revision: 1.12.2.2 $

#include <boost/mpl/for_each.hpp>

#include <boost/mpl/list.hpp>
#include <boost/mpl/range_c.hpp>
#include <boost/mpl/identity.hpp>
#include <boost/mpl/lambda.hpp>
#include <boost/bind.hpp>

#include <vector>
#include <iostream>
#include <algorithm>
#include <typeinfo>
#include <cassert>

namespace mpl = boost::mpl;

struct type_printer
{
    type_printer(std::ostream& s) : f_stream(&s) {}
    template< typename U > void operator()(mpl::identity<U>)
    {
        *f_stream << typeid(U).name() << '\n';
    }

 private:
    std::ostream* f_stream;
};

struct value_printer
{
    value_printer(std::ostream& s) : f_stream(&s) {}
    template< typename U > void operator()(U x)
    {
        *f_stream << x << '\n';
    }

 private:
    std::ostream* f_stream;
};

#ifdef __ICL
# pragma warning(disable:985)
#endif

int main()
{
    typedef mpl::list<char,short,int,long,float,double> types;
    mpl::for_each< types,mpl::make_identity<mpl::_1> >(type_printer(std::cout));

    typedef mpl::range_c<int,0,10> numbers;
    std::vector<int> v;

#if defined(__SGI_STL_PORT)
    void (std::vector<int>::* push_back)(int const&) = &std::vector<int>::push_back;
    mpl::for_each<numbers>(
          boost::bind(push_back, &v, _1)
        );
#else
    mpl::for_each<numbers>(
          boost::bind(&std::vector<int>::push_back, &v, _1)
        );    
#endif

    mpl::for_each< numbers >(value_printer(std::cout));
    
    for (unsigned i = 0; i < v.size(); ++i)
        assert(v[i] == (int)i);

    return 0;
}
