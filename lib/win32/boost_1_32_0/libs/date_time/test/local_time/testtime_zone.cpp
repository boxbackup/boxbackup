/* Copyright (c) 2003-2004 CrystalClear Software, Inc.
 * Subject to the Boost Software License, Version 1.0. 
 * (See accompanying file LICENSE-1.0 or http://www.boost.org/LICENSE-1.0)
 * Author: Jeff Garland, Bart Garst
 * $Date: 2004/09/13 16:44:10 $
 */

#include "boost/date_time/local_time/time_zone.hpp"
#include "boost/date_time/testfrmwk.hpp"
#include <iostream>

int
main()
{

  using namespace boost::gregorian;
  using namespace boost::posix_time;
  using namespace boost::local_time;

  //  typedef boost::date_time::time_zone_base<ptime> ptime_tz_base;


  boost::shared_ptr<dst_calc_rule> 
    rule1(new partial_date_dst_rule(partial_date(30,Apr),
                                    partial_date(30,Oct)));
  
  boost::shared_ptr<dst_calc_rule> 
    rule2(new first_last_dst_rule(first_last_dst_rule::start_rule(Sunday,Apr),
                                  first_last_dst_rule::end_rule(Sunday,Oct)));
  boost::shared_ptr<dst_calc_rule> 
    rule3(new last_last_dst_rule(last_last_dst_rule::start_rule(Sunday,Mar),
                                 last_last_dst_rule::end_rule(Sunday,Oct)));
  
  time_zone_names pst("Pacific Standard Time",
                      "PST", 
                      "Pacific Daylight Time" ,
                      "PDT");
  
  dst_adjustment_offsets of(hours(1), hours(2), hours(2));

  boost::shared_ptr<time_zone_base> tz1(new time_zone(pst, hours(8), of, rule1));

  check("out string", 
        tz1->dst_zone_abbrev() == std::string("PDT"));
  check("out string", 
        tz1->std_zone_abbrev() == std::string("PST"));
  check("out string", 
        tz1->std_zone_name() == std::string("Pacific Standard Time"));
  check("out string", 
        tz1->dst_zone_name() == std::string("Pacific Daylight Time"));

  check("dst offset", tz1->dst_offset() == hours(1));
  check("base offset", tz1->base_utc_offset() == hours(8));
  check("has dst", tz1->has_dst());

  check("dst start time", 
        tz1->dst_local_start_time(2003) == ptime(date(2003,Apr,30),hours(2)));
  check("dst end time", 
        tz1->dst_local_end_time(2003) == ptime(date(2003,Oct,30),hours(2)));
//   check("dst end time", 
//         tz1->dst_zone_name() == std::string("Pacific Daylight Time"));

  
//   time_zone* tz3 = new time_zone(pst, hours(8), of, rule2);
//   time_zone* tz4 = new time_zone(pst, hours(8), of, rule3);



  return printTestStats();
}


