/* Copyright (c) 2003-2004 CrystalClear Software, Inc.
 * Subject to the Boost Software License, Version 1.0. 
 * (See accompanying file LICENSE-1.0 or http://www.boost.org/LICENSE-1.0)
 * Author: Jeff Garland, Bart Garst
 * $Date: 2004/09/07 21:23:17 $
 */


#include "boost/date_time/gregorian/gregorian.hpp"
//#include "boost/date_time/local_time/time_zone.hpp"
#include "boost/date_time/testfrmwk.hpp"

#include "boost/date_time/local_time/posix_time_zone.hpp"

#include <string>
#include <iostream>

int main(){
  using namespace boost::local_time;
  using namespace boost::posix_time;
  using namespace boost::gregorian;
  std::string specs[] = {"MST-07", "MST-07:00:00","EST-05EDT,M4.1.0,M10.5.0", "EST-05:00:00EDT+01:00:00,M4.1.0/02:00:00,M10.5.0/02:00:00","PST-08PDT,J124/1:30,J310","PST-08PDT,124,310/0:30:00"};

  posix_time_zone nyc1(specs[2]);
  posix_time_zone nyc2(specs[3]);
  time_duration td = hours(-5);

  check("Has DST", nyc1.has_dst() && nyc2.has_dst());
  check("UTC offset", nyc1.base_utc_offset() == td);
  check("UTC offsets match", nyc1.base_utc_offset() == nyc2.base_utc_offset());
  check("Abbrevs", nyc1.std_zone_abbrev() == std::string("EST"));
  check("Abbrevs", nyc2.std_zone_abbrev() == std::string("EST"));
  check("Abbrevs", nyc1.dst_zone_abbrev() == std::string("EDT"));
  check("Abbrevs", nyc2.dst_zone_abbrev() == std::string("EDT"));
  // names not available for posix_time_zone, abbrevs used in their place
  check("Names", nyc1.std_zone_name() == std::string("EST"));
  check("Names", nyc2.std_zone_name() == std::string("EST"));
  check("Names", nyc1.dst_zone_name() == std::string("EDT"));
  check("Names", nyc2.dst_zone_name() == std::string("EDT"));
  td = hours(1);
  check("dst offset", nyc1.dst_offset() == td);
  check("dst offsets match", nyc1.dst_offset() == nyc2.dst_offset());
  check("dst start", nyc1.dst_local_start_time(2003) == 
      ptime(date(2003,Apr,6),time_duration(2,0,0)));
  check("dst starts match", nyc1.dst_local_start_time(2003) == 
      nyc2.dst_local_start_time(2003)); 
  check("dst end", nyc1.dst_local_end_time(2003) == 
      ptime(date(2003,Oct,26),time_duration(2,0,0)));
  check("dst ends match", nyc1.dst_local_end_time(2003) == 
      nyc2.dst_local_end_time(2003)); 
  

  posix_time_zone az1(specs[0]);
  posix_time_zone az2(specs[1]);
  td = hours(-7);

  check("Has DST", !az1.has_dst() && !az2.has_dst());
  check("UTC offset", az1.base_utc_offset() == td);
  check("UTC offsets match", az1.base_utc_offset() == az2.base_utc_offset());
  check("Abbrevs", az1.std_zone_abbrev() == std::string("MST"));
  check("Abbrevs", az2.std_zone_abbrev() == std::string("MST"));
  // non-dst zones default to empty strings for dst names & abbrevs
  check("Abbrevs", az1.dst_zone_abbrev() == std::string(""));
  check("Abbrevs", az2.dst_zone_abbrev() == std::string(""));
  check("Names", az1.std_zone_name() == std::string("MST"));
  check("Names", az2.std_zone_name() == std::string("MST"));
  check("Names", az1.dst_zone_name() == std::string(""));
  check("Names", az2.dst_zone_name() == std::string(""));
 

  // bizzar time zone spec to fully test parsing
  std::cout << "\nFictitious time zone" << std::endl;
  posix_time_zone bz("BST+11:21:15BDT-00:28,M2.2.4/03:15:42,M11.5.2/01:08:53");
  check("hast dst", bz.has_dst());
  check("UTC offset", bz.base_utc_offset() == time_duration(11,21,15));
  check("Abbrev", bz.std_zone_abbrev() == std::string("BST"));
  check("Abbrev", bz.dst_zone_abbrev() == std::string("BDT"));
  check("dst offset", bz.dst_offset() == time_duration(0,-28,0));
  check("dst start", bz.dst_local_start_time(1962) ==
      ptime(date(1962,Feb,8),time_duration(3,15,42)));
  check("dst end", bz.dst_local_end_time(1962) ==
      ptime(date(1962,Nov,27),time_duration(1,8,53)));
  
  // only checking start & end rules w/ 'J' notation
  std::cout << "\n'J' notation Start/End rule tests..." << std::endl;
  posix_time_zone la1(specs[4]); // "PST-08PDT,J124,J310"
  check("dst start", la1.dst_local_start_time(2003) == 
      ptime(date(2003,May,4),time_duration(1,30,0)));
  check("dst end", la1.dst_local_end_time(2003) == 
      ptime(date(2003,Nov,6),time_duration(2,0,0)));
  
  // only checking start & end rules w/ 'n' notation
  std::cout << "\n'n' notation Start/End rule tests..." << std::endl;
  posix_time_zone la2(specs[5]); // "PST-08PDT,124,310"
  //posix_time_zone la2("PST-08PDT,0,365");// Jan1/Dec31 
  check("dst start", la2.dst_local_start_time(2003) == 
      ptime(date(2003,May,4),time_duration(2,0,0)));
  check("dst end", la2.dst_local_end_time(2003) == 
      ptime(date(2003,Nov,6),time_duration(0,30,0)));

  // bad posix time zone strings tests
  std::cout << "\nInvalid time zone string tests..." << std::endl;
  try {
    posix_time_zone badz("EST-13");
    check("Exception not thrown: bad UTC offset", false);
  }catch(bad_offset boff){
    std::string msg(boff.what());
    check("Exception caught: "+msg , true);
  }
  try {
    posix_time_zone badz("EST-5EDT24:00:01,J124/1:30,J310");
    check("Exception not thrown: bad DST adjust", false);
  }catch(bad_adjustment badj){
    std::string msg(badj.what());
    check("Exception caught: "+msg , true);
  }
  try {
    posix_time_zone badz("EST-5EDT01:00:00,J124/-1:30,J310");
    check("Exception not thrown: bad DST start/end offset", false);
  }catch(bad_offset boff){
    std::string msg(boff.what());
    check("Exception caught: "+msg , true);
  }
  try {
    posix_time_zone badz("EST-5EDT01:00:00,J124/1:30,J370");
    check("Exception not thrown: invalid date spec", false);
  }catch(boost::gregorian::bad_day_of_month boff){
    std::string msg(boff.what());
    check("Exception caught: "+msg , true);
  }catch(boost::gregorian::bad_month boff){
    std::string msg(boff.what());
    check("Exception caught: "+msg , true);
  }catch(...){
    check("Unexpected exception caught: ", false);
  }

  std::cout << "\nTest some Central Europe specs" << std::endl;

  //Test a timezone spec on the positive side of the UTC line.
  //This is the time for central europe which is one hour in front of UTC
  //Note these Summer time transition rules aren't actually correct.
  posix_time_zone cet_tz("CET+01:00:00EDT+01:00:00,M4.1.0/02:00:00,M10.5.0/02:00:00");
  check("Has DST", cet_tz.has_dst());
  check("UTC offset", cet_tz.base_utc_offset() == hours(1));
  check("Abbrevs", cet_tz.std_zone_abbrev() == std::string("CET"));
//   check("Abbrevs", nyc2.std_zone_abbrev() == std::string("EST"));

  std::cout << "\nTest some Central Austrialia UTC+8:30" << std::endl;

  //Test a timezone spec on the positive side of the UTC line.
  //This is the time for central europe which is one hour in front of UTC
  //Note these Summer time transition rules aren't actually correct.
  posix_time_zone caus_tz("CAS+08:30:00CDT+01:00:00,M4.1.0/02:00:00,M10.5.0/02:00:00");
  check("Has DST", caus_tz.has_dst());
  check("UTC offset", caus_tz.base_utc_offset() == hours(8)+minutes(30));
  check("Abbrevs", caus_tz.std_zone_abbrev() == std::string("CAS"));
//   check("Abbrevs", nyc2.std_zone_abbrev() == std::string("EST"));

  printTestStats();
  return 0;
}

