//  long_name_check header  --------------------------------------------------//

//  Copyright Beman Dawes 2002.
//  Distributed under the Boost Software License, Version 1.0.
//  (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_FILE_NAME_CHECK_HPP
#define BOOST_FILE_NAME_CHECK_HPP

#include "inspector.hpp"

namespace boost
{
  namespace inspect
  {
    class long_name_check : public inspector
    {
      long m_long_name_errors;
    public:
      long_name_check();
      virtual ~long_name_check();

      virtual const char * name() const { return "long-name-check"; }
      virtual const char * desc() const { return "file names too long"; }

      virtual void inspect(
        const std::string & library_name,
        const path & full_path );
    };
  }
}

#endif // BOOST_FILE_NAME_CHECK_HPP
