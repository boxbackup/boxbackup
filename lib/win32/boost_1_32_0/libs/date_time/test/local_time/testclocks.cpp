/* Copyright (c) 2003-2004 CrystalClear Software, Inc.
 * Subject to the Boost Software License, Version 1.0. 
 * (See accompanying file LICENSE-1.0 or http://www.boost.org/LICENSE-1.0)
 * Author: Jeff Garland, Bart Garst
 * $Date: 2004/10/13 18:56:29 $
 */

#include "boost/date_time/local_time/local_time.hpp"
#include <iostream>

int
main()
{
  using namespace boost::gregorian;
  using namespace boost::posix_time;
  using namespace boost::local_time;

  typedef boost::date_time::second_clock<date, local_date_time> local_sec_clock;

  boost::shared_ptr<time_zone_base> az_tz(new posix_time_zone("MST-07"));
  boost::shared_ptr<time_zone_base> ny_tz(new posix_time_zone("EST-05EDT,M4.1.0,M10.5.0"));
  ptime tl = second_clock::local_time();
  local_date_time ldt1 = local_sec_clock::local_time(az_tz);
  std::cout << ldt1.to_string() << std::endl;
  local_date_time ldt2 = local_sec_clock::local_time(ny_tz);
  std::cout << ldt2.to_string() << std::endl;

   return 0;
}
