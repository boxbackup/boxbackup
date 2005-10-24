//  long_name_check implementation  ------------------------------------------//

//  Copyright Beman Dawes 2002.
//  Distributed under the Boost Software License, Version 1.0.
//  (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#include "long_name_check.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/bind.hpp>
#include <boost/next_prior.hpp>

#include <locale>
#include <algorithm>

namespace { namespace aux {

bool starts_with_nonalpha( std::string const& x )
{
  return !std::isalpha( *x.begin(), std::locale::classic() )
      && *x.begin() != '_'
      && x != ".cvsignore"
      ;
}

bool contains_dot( std::string const& x )
{
    return x.find( '.' ) != std::string::npos;
}

}}


namespace boost
{
  namespace inspect
  {
    long_name_check::long_name_check() : m_long_name_errors(0) {}
     
    void long_name_check::inspect(
      const string & library_name,
      const path & full_path )
    {
      std::string const leaf( full_path.leaf() );

      if ( leaf.size() > 31 )
      {
        ++m_long_name_errors;
        error( library_name, full_path, "filename > 31 chars" );
      }

      if ( std::count( leaf.begin(), leaf.end(), '.' ) > 1 )
      {
        ++m_long_name_errors;
        error( library_name, full_path, "filename contains more than one dot character ('.')" );
      }

      if ( *leaf.rbegin() == '.' )
      {
        ++m_long_name_errors;
        error( library_name, full_path, "filename ends with the dot character ('.')" );
      }
      
      path const relative_path( relative_to( full_path, filesystem::initial_path() ), &filesystem::no_check );

      if ( std::find_if( relative_path.begin(), relative_path.end(), boost::bind( &aux::starts_with_nonalpha, _1 ) )
            != relative_path.end() )
      {
        ++m_long_name_errors;
        error( library_name, full_path, "leading character of one of the path compontents is not alphabetic" );
      }

      if ( std::find_if( relative_path.begin(), boost::prior( relative_path.end() )
            , boost::bind( &aux::contains_dot, _1 ) ) != boost::prior( relative_path.end() ) )
      {
        ++m_long_name_errors;
        error( library_name, full_path, "directory name contains the dot character ('.')" );
      }

      if ( std::distance( relative_path.begin(), boost::prior( relative_path.end() ) ) >= 8 )
      {
        ++m_long_name_errors;
        error( library_name, full_path, "file's directory depth will exceed 8 levels if placed on a CD" );
      }

      if ( relative_path.string().size() > ( 100 - string( "boost_X_XX_X/" ).size() ) )
      {
        ++m_long_name_errors;
        error( library_name, full_path, "file path will be > 100 chars if placed on a CD" );
      }

      try
      {
        path const check_portability( relative_path.string(), &filesystem::portable_name );
      }
      catch ( filesystem::filesystem_error const& )
      {
        ++m_long_name_errors;
        error( library_name, full_path, "nonportable path" );
      }

    }

    long_name_check::~long_name_check()
    {
      std::cout << "  " << m_long_name_errors << " " << desc() << '\n';
    }


  } // namespace inspect
} // namespace boost
