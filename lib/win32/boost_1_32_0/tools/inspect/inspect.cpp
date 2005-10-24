//  inspect program  ---------------------------------------------------------//

//  Copyright Beman Dawes 2002.
//  Copyright Rene Rivera 2004.
//  Distributed under the Boost Software License, Version 1.0.
//  (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

//  This program recurses through sub-directories looking for various problems.
//  It contains some Boost specific features, like ignoring "CVS" and "bin",
//  and the code that identifies library names assumes the Boost directory
//  structure.

//  See http://www.boost.org/tools/inspect for more information.

#include <boost/shared_ptr.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>

#include <iostream>
#include <cassert>
#include <vector>
#include <list>
#include <utility>
#include <algorithm>
#include <cstring>

#include "inspector.hpp" // includes <string>, <boost/filesystem/path.hpp>,
                         // <iostream>, <set>
                         // and gives using for string and path.
#include "copyright_check.hpp"
#include "crlf_check.hpp"
#include "license_check.hpp"
#include "link_check.hpp"
#include "long_name_check.hpp"
#include "tab_check.hpp"
#include "cvs_iterator.hpp"

namespace fs = boost::filesystem;

namespace
{
  typedef boost::shared_ptr< boost::inspect::inspector > inspector_ptr;

  struct inspector_element
  {
    inspector_ptr  inspector;

    inspector_element( boost::inspect::inspector * p ) : inspector(p) {}
  };

  typedef std::list< inspector_element > inspector_list;

  long file_count;
  long directory_count;
  long error_count;

  boost::inspect::string_set content_signatures;

  struct error_msg
  {
    string library;
    string rel_path;
    string msg;

    bool operator<( const error_msg & rhs ) const
    {
      if ( library < rhs.library ) return true;
      if ( library > rhs.library ) return false;
      if ( rel_path < rhs.rel_path ) return true;
      if ( rel_path > rhs.rel_path ) return false;
      return msg < rhs.msg;
    }
  };

  typedef std::vector< error_msg > error_msg_vector;
  error_msg_vector msgs;

//  visit_predicate (determines which directories are visited)  --------------//

  typedef bool(*pred_type)(const path&);

  bool visit_predicate( const path & pth )
  {
    string leaf( pth.leaf() );
    return
      leaf != "CVS"
      && leaf != "bin"
      && leaf != "jam_src" // this really out of our hands
      && leaf != "status"  // too many issues with generated HTML files
      ;
  }

//  library_from_content  ----------------------------------------------------//

  string library_from_content( const string & content )
  {
    string::size_type pos( content.find( "www.boost.org/libs/" ) );

    if ( pos == string::npos ) return "unknown";

    string lib;
    pos += 19;
    while ( content[pos] != ' '
      && content[pos] != '/'
      && content[pos] != '\n'
      && content[pos] != '\r'
      && content[pos] != '\t' ) lib += content[pos++];
    return lib;
  }

//  find_signature  ----------------------------------------------------------//

  bool find_signature( const path & file_path,
    const boost::inspect::string_set & signatures )
  {
    string name( file_path.leaf() );
    if ( signatures.find( name ) == signatures.end() )
    {
      string::size_type pos( name.rfind( '.' ) );
      if ( pos == string::npos
        || signatures.find( name.substr( pos ) )
          == signatures.end() ) return false;
    }
    return true;
  }

//  load_content  ------------------------------------------------------------//

  void load_content( const path & file_path, string & target )
  {
    target = "";

    if ( !find_signature( file_path, content_signatures ) ) return;

    fs::ifstream fin( file_path, std::ios_base::in|std::ios_base::binary );
    if ( !fin )
      throw string( "could not open input file: " ) + file_path.string();
    std::getline( fin, target, '\0' ); // read the whole file
  }

//  check  -------------------------------------------------------------------//

  void check( const string & lib,
    const path & pth, const string & content, const inspector_list & insp_list )
  {
    // invoke each inspector
    for ( inspector_list::const_iterator itr = insp_list.begin();
      itr != insp_list.end(); ++itr )
    {
      itr->inspector->inspect( lib, pth ); // always call two-argument form
      if ( find_signature( pth, itr->inspector->signatures() ) )
      {
          itr->inspector->inspect( lib, pth, content );
      }
    }
  }

//  visit_all  ---------------------------------------------------------------//

  template< class DirectoryIterator >
  void visit_all( const string & lib,
    const path & dir_path, const inspector_list & insps )
  {
    static DirectoryIterator end_itr;
    ++directory_count;

    for ( DirectoryIterator itr( dir_path ); itr != end_itr; ++itr )
    {

      if ( fs::is_directory( *itr ) )
      {
        if ( visit_predicate( *itr ) )
        {
          string cur_lib( boost::inspect::impute_library( *itr ) );
          check( cur_lib, *itr, "", insps );
          visit_all<DirectoryIterator>( cur_lib, *itr, insps );
        }
      }
      else
      {
        ++file_count;
        string content;
        load_content( *itr, content );
        check( lib == "unknown"
          ? library_from_content( content ) : lib, *itr, content, insps );
      }
    }
  }

//  display_summary_helper  --------------------------------------------------//

  void display_summary_helper( const string & current_library, int err_count )
  {
    std::cout << "  <tr><td><a href=\"#" 
              << current_library
              << "\">" << current_library
              << "</a></td><td align=\"center\">"
              << err_count << "</td></tr>\n";
  }
  
//  display_summary  ---------------------------------------------------------//

  void display_summary()
  {
    std::cout << "</pre>\n"
            "<h2>Summary</h2>\n"
            "<table border=\"1\" cellpadding=\"5\" cellspacing=\"0\">\n"
            "  <tr>\n"
            "    <td><b>Library</b></td>\n"
            "    <td><b>Problems</b></td>\n"
            "  </tr>\n"
            ;
    string current_library( msgs.begin()->library ); 
    int err_count = 0;
    for ( error_msg_vector::iterator itr ( msgs.begin() );
      itr != msgs.end(); ++itr )
    {
      if ( current_library != itr->library )
      {
        display_summary_helper( current_library, err_count );
        current_library = itr->library;
        err_count = 0;
      }
      ++err_count;
    }
    display_summary_helper( current_library, err_count );

    std::cout << "</table>\n";
  }


//  display_details  ---------------------------------------------------------//

  void display_details()
  {
    std::cout << "<h2>Details</h2>\n";

    // display error messages with group indication
    error_msg current;
    string sep;
    bool first = true;
    for ( error_msg_vector::iterator itr ( msgs.begin() );
      itr != msgs.end(); ++itr )
    {
      if ( current.library != itr->library )
      {
        if ( !first ) std::cout << "</pre>\n";
        std::cout << "\n<h3><a name=\"" << itr->library
                  << "\">" << itr->library << "</a></h3>\n<pre>";
      }
      if ( current.library != itr->library
        || current.rel_path != itr->rel_path )
      {
        std::cout << "\n";
        std::cout << itr->rel_path;
        sep = ": ";
      }
      if ( current.library != itr->library
        || current.rel_path != itr->rel_path
        || current.msg != itr->msg )
      {
        std::cout << sep << itr->msg;
        sep = ", ";
      }
      current.library = itr->library;
      current.rel_path = itr->rel_path;
      current.msg = itr->msg;
      first = false;
   }
   std::cout << "</pre>\n";
  }

  const char * options()
  {
    return
         "  -license\n"
         "  -copyright\n"
         "  -crlf\n"
         "  -link\n"
         "  -long_name\n"
         "  -tab\n"
         "default is all checks on; otherwise options specify desired checks\n";
  }

} // unnamed namespace

namespace boost
{
  namespace inspect
  {

//  register_signature  ------------------------------------------------------//

    void inspector::register_signature( const string & signature )
    {
      m_signatures.insert( signature );
      content_signatures.insert( signature );
    }

//  error  -------------------------------------------------------------------//

    void inspector::error( const string & library_name,
      const path & full_path, const string & msg )
    {
      ++error_count;
      error_msg err_msg;
      err_msg.library = library_name;
      err_msg.rel_path = relative_to( full_path, fs::initial_path() );
      err_msg.msg = msg;
      msgs.push_back( err_msg );

//     std::cout << library_name << ": "
//        << full_path.string() << ": "
//        << msg << '\n';

    }
    
    source_inspector::source_inspector()
    {
      // C/C++ source code...
      register_signature( ".c" );
      register_signature( ".cpp" );
      register_signature( ".css" );
      register_signature( ".cxx" );
      register_signature( ".h" );
      register_signature( ".hpp" );
      register_signature( ".hxx" );
      register_signature( ".inc" );
      register_signature( ".ipp" );
      
      // Boost.Build BJam source code...
      register_signature( "Jamfile" );
      register_signature( ".jam" );
      register_signature( ".v2" );
      
      // Other scripts; Python, shell, autoconfig, etc.
      register_signature( "configure.in" );
      register_signature( "GNUmakefile" );
      register_signature( "Makefile" );
      register_signature( ".bat" );
      register_signature( ".mak" );
      register_signature( ".pl" );
      register_signature( ".py" );
      register_signature( ".sh" );
      
      // Hypertext, Boost.Book, and other text...
      register_signature( "news" );
      register_signature( "readme" );
      register_signature( "todo" );
      register_signature( "NEWS" );
      register_signature( "README" );
      register_signature( "TODO" );
      register_signature( ".boostbook" );
      register_signature( ".htm" );
      register_signature( ".html" );
      register_signature( ".rst" );
      register_signature( ".sgml" );
      register_signature( ".shtml" );
      register_signature( ".txt" );
      register_signature( ".xml" );
      register_signature( ".xsd" );
      register_signature( ".xsl" );
    }
    
    hypertext_inspector::hypertext_inspector()
    {
      register_signature( ".htm" );
      register_signature( ".html" );
      register_signature( ".shtml" );
    }

//  impute_library  ----------------------------------------------------------//

    string impute_library( const path & full_dir_path )
    {
      path relative( relative_to( full_dir_path, fs::initial_path() ),
        fs::no_check );
      if ( relative.empty() ) return "boost-root";
      string first( *relative.begin() );
      string second =  // borland 5.61 requires op=  
        ++relative.begin() == relative.end()
          ? string() : *++relative.begin();

      if ( first == "boost" )
        return second.empty() ? string( "unknown" ) : second;

      return (( first == "libs" || first == "tools" ) && !second.empty())
        ? second : first;
    }

  } // namespace inspect
} // namespace boost

//  cpp_main()  --------------------------------------------------------------//

#include <boost/test/included/prg_exec_monitor.hpp>

int cpp_main( int argc, char * argv[] )
{
  fs::initial_path();

  if ( argc > 1 && (std::strcmp( argv[1], "-help" ) == 0
    || std::strcmp( argv[1], "--help" ) == 0 ) )
  {
    std::clog << "Usage: inspect [-cvs] [options...]\n"
      "options:\n"
      << options();
    return 1;
  }

  bool license_ck = true;
  bool copyright_ck = true;
  bool crlf_ck = true;
  bool link_ck = true;
  bool long_name_ck = true;
  bool tab_ck = true;
  bool cvs = false;

  if ( argc > 1 && std::strcmp( argv[1], "-cvs" ) == 0 )
  {
    cvs = true;
    --argc; ++argv;
  }

  if ( argc > 1 && *argv[1] == '-' )
  {
    license_ck = false;
    copyright_ck = false;
    crlf_ck = false;
    link_ck = false;
    long_name_ck = false;
    tab_ck = false;
  }

  for(; argc > 1; --argc, ++argv )
  {
    if ( std::strcmp( argv[1], "-license" ) == 0 )
      license_ck = true;
    else if ( std::strcmp( argv[1], "-copyright" ) == 0 )
      copyright_ck = true;
    else if ( std::strcmp( argv[1], "-crlf" ) == 0 )
      crlf_ck = true;
    else if ( std::strcmp( argv[1], "-link" ) == 0 )
      link_ck = true;
    else if ( std::strcmp( argv[1], "-long_name" ) == 0 )
      long_name_ck = true;
    else if ( std::strcmp( argv[1], "-tab" ) == 0 )
      tab_ck = true;
    else
    {
      std::cerr << "unknown option: " << argv[1]
      << "\nvalid options are:\n"
      << options();
      return 1;
    }
  }

  inspector_list inspectors;

  if ( license_ck )
    inspectors.push_back( inspector_element( new boost::inspect::license_check ) );
  if ( copyright_ck )
    inspectors.push_back( inspector_element( new boost::inspect::copyright_check ) );
  if ( crlf_ck )
    inspectors.push_back( inspector_element( new boost::inspect::crlf_check ) );
  if ( link_ck )
    inspectors.push_back( inspector_element( new boost::inspect::link_check ) );
  if ( long_name_ck )
    inspectors.push_back( inspector_element( new boost::inspect::long_name_check ) );
  if ( tab_ck )
    inspectors.push_back( inspector_element( new boost::inspect::tab_check ) );

  // perform the actual inspection, using the requested type of iteration
  if ( cvs )
    visit_all<hack::cvs_iterator>( "boost-root",
      fs::initial_path(), inspectors );
  else
    visit_all<fs::directory_iterator>( "boost-root",
      fs::initial_path(), inspectors );

  // close
  for ( inspector_list::iterator itr = inspectors.begin();
        itr != inspectors.end(); ++itr )
  {
    itr->inspector->close();
  }

  char run_date[128];
  std::time_t tod;
  std::time( &tod );
  std::strftime( run_date, sizeof(run_date),
    "%X UTC, %A %d %B %Y", std::gmtime( &tod ) );

  std::cout << "<html>\n"
          "<head>\n"
          "<title>Boost Inspection Report</title>\n"
          "</head>\n"
          "<body bgcolor=\"#ffffff\" text=\"#000000\">\n"
          "<table border=\"0\">\n"
          "<tr>\n"
          "<td><img border=\"0\" src=\"../boost.png\" width=\"277\" "
          "height=\"86\"></td>\n"
          "<td align=\"center\">\n"
          "<h1>Boost Inspection Report</h1>\n"
          "<b>Run Date:</b> " << run_date  << "\n"
          "</td>\n"
          "</table>\n"
          "<p>An <a href=\"http://www.boost.org/tools/inspect/index.html\">inspection\n" 
          "program</a> checks each file in the current Boost CVS for various problems,\n" 
          "generating this web page as output. Problems detected include tabs in files,\n" 
          "missing copyrights, broken URL's, and similar misdemeanors.</p>\n"
          ;

  std::cout << "<h2>Totals</h2>\n<pre>"
            << file_count << " files scanned\n"
            << directory_count << " directories scanned\n"
            << error_count << " problems reported\n";

  std::cout << "\nproblem counts:\n";

  for ( inspector_list::iterator itr = inspectors.begin();
        itr != inspectors.end(); ++itr )
  {
    itr->inspector.reset();
  }

  std::sort( msgs.begin(), msgs.end() );

  if ( !msgs.empty() )
  {
    display_summary();
    display_details();
  }

  std::cout << "</body>\n"
               "</html>\n";
  return 0;
}
