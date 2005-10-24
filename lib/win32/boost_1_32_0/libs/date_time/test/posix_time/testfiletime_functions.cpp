/* Copyright (c) 2002,2003, 2004 CrystalClear Software, Inc.
 * Use, modification and distribution is subject to the 
 * Boost Software License, Version 1.0. (See accompanying
 * file LICENSE-1.0 or http://www.boost.org/LICENSE-1.0)
 * Author: Jeff Garland, Bart Garst
 */

#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/date_time/testfrmwk.hpp"
#include "boost/date_time/filetime_functions.hpp"
#include <cmath>

#if defined(BOOST_HAS_FTIME)
#include <windows.h>
#endif

int main() 
{
#if defined(BOOST_HAS_FTIME) // skip tests if no FILETIME

  using namespace boost::posix_time;

  // adjustor is used to truncate ptime's fractional seconds for 
  // comparison with SYSTEMTIME's milliseconds
  const int adjustor = time_duration::ticks_per_second() / 1000;
  
  for(int i = 0; i < 5; ++i){

    FILETIME ft;
    SYSTEMTIME st;
    GetSystemTime(&st);
    SystemTimeToFileTime(&st,&ft); 

    ptime pt = from_ftime<ptime>(ft);

    check("ptime year matches systemtime year", 
        st.wYear == pt.date().year());
    check("ptime month matches systemtime month", 
        st.wMonth == pt.date().month());
    check("ptime day matches systemtime day", 
        st.wDay == pt.date().day());
    check("ptime hour matches systemtime hour", 
        st.wHour == pt.time_of_day().hours());
    check("ptime minute matches systemtime minute", 
        st.wMinute == pt.time_of_day().minutes());
    check("ptime second matches systemtime second", 
        st.wSecond == pt.time_of_day().seconds());
    check("truncated ptime fractional second matches systemtime millisecond", 
        st.wMilliseconds == (pt.time_of_day().fractional_seconds() / adjustor)
         );

    // burn up a little time
    for (int j=0; j<100000; j++)
    {
      SYSTEMTIME tmp;
      GetSystemTime(&tmp);
    }

  } // for loop

#else // BOOST_HAS_FTIME
  // we don't want a forced failure here, not a shortcoming
  check("FILETIME not available for this compiler/platform", true);
#endif // BOOST_HAS_FTIME
  
  return printTestStats();

}

