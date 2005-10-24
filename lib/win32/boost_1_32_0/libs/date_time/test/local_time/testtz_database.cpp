/* Copyright (c) 2003-2004 CrystalClear Software, Inc.
 * Subject to the Boost Software License, Version 1.0. 
 * (See accompanying file LICENSE-1.0 or http://www.boost.org/LICENSE-1.0)
 * Author: Jeff Garland, Bart Garst
 * $Date: 2004/09/07 21:23:18 $
 */


#include "boost/date_time/testfrmwk.hpp"
#include "boost/date_time/gregorian/gregorian.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/date_time/local_time/time_zone.hpp"
#include "boost/date_time/local_time/tz_database.hpp"
#include "boost/date_time/local_time/posix_time_zone.hpp"
#include <iostream>

int main(){
  using namespace boost::gregorian;
  using namespace boost::posix_time;
  using namespace boost::local_time;
  
  try{
    tz_database tz_db;
    tz_db.load_from_file("missing_file.csv");
  }catch(data_not_accessible e){
    check("Caught Missing data file exception", true);
  }catch(...){
    check("Caught first unexpected exception", false);
  }

  // create tz_database for testing
  // path to data is relative to the libs/date_time/test directory which
  // is where tests get run from when compile with bjam
  tz_database tz_db;
  tz_db.load_from_file("../data/date_time_zonespec.csv");
  try{
    //This path is relative to where bjam runs when in date-time dir -- this is probably an
    //issue for the regression tests.  Probably needs to be fully qualified path from BOOST_ROOT
    tz_db.load_from_file("local_time/poorly_formed_zonespec.csv");
  }catch(bad_field_count e){
    check("Caught Bad field count exception", true);
  }catch(...){
    check("Caught second unexpected exception", false);
  }


  boost::shared_ptr<time_zone_base> bad_tz = tz_db.time_zone_from_region("Invalid/name");
  check("Expected null pointer return", bad_tz == 0 ); 

  boost::shared_ptr<time_zone_base> nyc_test = tz_db.time_zone_from_region("America/New_York");
  check("nyc Valid pointer", nyc_test != 0 ); 
  check("nyc Abbreviations",nyc_test->std_zone_abbrev() == std::string("EST")); 
  check("nyc Full Name", nyc_test->std_zone_name() == std::string("EST")); 
  check("nyc Abbreviations",nyc_test->dst_zone_abbrev() == std::string("EDT")); 
  //std::cout << nyc_test->std_zone_name() << std::endl;
  check("nyc Full Name", nyc_test->dst_zone_name() == std::string("EDT")); 
  check("nyc GMT Offset", nyc_test->base_utc_offset() == hours(-5));
  check("nyc DST Offset", nyc_test->dst_offset() == hours(1));
  //std::cout << nyc_test->dst_local_start_time(2004) << std::endl;
  check("nyc dst start",  nyc_test->dst_local_start_time(2004) == ptime(date(2004, Apr, 4), hours(2)));
  check("nyc dst end", nyc_test->dst_local_end_time(2004) == ptime(date(2004, Oct, 31), hours(2)));
  check("nyc has dst", nyc_test->has_dst());
  
  boost::shared_ptr<time_zone_base> phx_test = tz_db.time_zone_from_region("America/Phoenix");
  check("az Valid pointer", phx_test != 0 ); 
  check("az Abbreviations",phx_test->std_zone_abbrev() == std::string("MST"));  
  check("az Full Name", phx_test->std_zone_name() == std::string("MST"));  
  check("az Abbreviations", phx_test->dst_zone_abbrev() == std::string(""));  
  check("az Full Name", phx_test->dst_zone_name() == std::string(""));  
  check("az GMT Offset", phx_test->base_utc_offset() == hours(-7));
  check("az DST Offset", phx_test->dst_offset() == hours(0));
  //std::cout << phx_test->dst_local_start_time(2004) << std::endl;
  check("az has dst", phx_test->has_dst() == false);

  //Now add and retriev a Posix tz specs from the database
  boost::shared_ptr<time_zone_base> eastern(new posix_time_zone("EST-05:00:00EDT+01:00:00,M4.1.0/02:00:00,M10.5.0/02:00:00"));
  tz_db.add_record("United States/Eastern", eastern);
  boost::shared_ptr<time_zone_base> eastern_test = tz_db.time_zone_from_region("United States/Eastern");
  check("eastern Valid pointer", eastern_test != 0 );
  check("eastern Abbreviations", 
      eastern_test->std_zone_abbrev() == std::string("EST"));  
  check("eastern Abbreviations", 
      eastern_test->std_zone_name() == std::string("EST"));  
  check("eastern Abbreviations", 
      eastern_test->dst_zone_abbrev() == std::string("EDT"));  
  check("eastern Abbreviations", 
      eastern_test->dst_zone_name() == std::string("EDT"));  
  check("eastern GMT Offset", eastern_test->base_utc_offset() == hours(-5));
  check("eastern dst start",  eastern_test->dst_local_start_time(2004) == ptime(date(2004, Apr, 4), hours(2)));
  check("eastern dst end", eastern_test->dst_local_end_time(2004) == ptime(date(2004, Oct, 31), hours(2)));
  check("eastern  has dst", eastern_test->has_dst() == true);


  return printTestStats();
}
