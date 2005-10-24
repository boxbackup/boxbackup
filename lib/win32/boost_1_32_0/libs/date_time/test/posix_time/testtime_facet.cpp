
/* Copyright (c) 2004 CrystalClear Software, Inc.
 * Use, modification and distribution is subject to the 
 * Boost Software License, Version 1.0. (See accompanying
 * file LICENSE-1.0 or http://www.boost.org/LICENSE-1.0)
 * Author: Jeff Garland
 * $Date: 2004/10/14 02:43:34 $
 */

#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/date_time/time_facet.hpp"
#include "boost/date_time/testfrmwk.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

template<class temporal_type, typename charT>
inline
void 
teststreaming(std::string testname, 
              temporal_type value,
              std::basic_string<charT> expected_result,
              const std::locale& locale = std::locale::classic())
{
  std::basic_stringstream<charT> ss;
  ss.imbue(locale);
  ss << value;
  check(testname, ss.str() == expected_result);
}



static const wchar_t* long_month_names[] =
  {L"Januar",L"Februar",L"Marz",L"April",L"Mai",L"Juni",L"Juli",L"August",
   L"September",L"Oktober",L"November",L"Dezember"};
static const wchar_t* short_month_names[]=
  {L"Jan",L"Feb",L"Mar",L"Apr",L"Mai",L"Jun",L"Jul",L"Aug",
   L"Sep",L"Okt",L"Nov",L"Dez"};

std::vector<std::basic_string<wchar_t> > de_short_month_names;
std::vector<std::basic_string<wchar_t> > de_long_month_names;

namespace boost { namespace gregorian {

  //temporary until these are in the library
  typedef boost::date_time::period_formatter<wchar_t> wperiod_formatter;
  typedef boost::date_time::period_formatter<char>    period_formatter;
  
}}

namespace boost { namespace posix_time {

  //temporary until these are in the library
  typedef boost::date_time::time_facet<ptime, wchar_t> wptime_facet;
  typedef boost::date_time::time_facet<ptime, char>     ptime_facet;

  //temporary replacements until in library
  /*--------------------- operators ----------------------------*/

  template <class CharT, class TraitsT>
  inline 
  std::basic_ostream<CharT, TraitsT>&
  operator<<(std::basic_ostream<CharT, TraitsT>& os, 
             const ptime& p) {
    typedef boost::date_time::time_facet<ptime, CharT> custom_ptime_facet;
    typedef std::time_put<CharT>                  std_ptime_facet;
    std::ostreambuf_iterator<CharT> oitr(os);
    if (std::has_facet<custom_ptime_facet>(os.getloc()))
      std::use_facet<custom_ptime_facet>(os.getloc()).put(oitr, os, os.fill(), p);
    else {
      //instantiate a custom facet for dealing with times since the user
      //has not put one in the stream so far.  This is for efficiency 
      //since we would always need to reconstruct for every time period
      //if the locale did not already exist.  Of course this will be overridden
      //if the user imbues as some later point.
      std::ostreambuf_iterator<CharT> oitr(os);
      custom_ptime_facet* f = new custom_ptime_facet();
      std::locale l = std::locale(os.getloc(), f);
      os.imbue(l);
      f->put(oitr, os, os.fill(), p);
    }
    return os;
  }

  template <class CharT, class TraitsT>
  inline 
  std::basic_ostream<CharT, TraitsT>&
  operator<<(std::basic_ostream<CharT, TraitsT>& os, 
             const boost::posix_time::time_period& p) {
    typedef boost::date_time::time_facet<ptime, CharT> custom_ptime_facet;
    typedef std::time_put<CharT>                  std_time_facet;
    std::ostreambuf_iterator<CharT> oitr(os);
    if (std::has_facet<custom_ptime_facet>(os.getloc())) {
      std::use_facet<custom_ptime_facet>(os.getloc()).put(oitr, os, os.fill(), p);
    }
    else {
      //instantiate a custom facet for dealing with periods since the user
      //has not put one in the stream so far.  This is for efficiency 
      //since we would always need to reconstruct for every time period
      //if the local did not already exist.  Of course this will be overridden
      //if the user imbues as some later point.
      std::ostreambuf_iterator<CharT> oitr(os);
      custom_ptime_facet* f = new custom_ptime_facet();
      std::locale l = std::locale(os.getloc(), f);
      os.imbue(l);
      f->put(oitr, os, os.fill(), p);
    }
    return os;
  }


  //! ostream operator for posix_time::time_duration 
  //  todo fix to use facet --  place holder for now...
  template <class charT, class traits>
  inline
  std::basic_ostream<charT, traits>&
  operator<<(std::basic_ostream<charT, traits>& os, const time_duration& td)
  {
    typedef boost::date_time::ostream_time_duration_formatter<time_duration, charT> duration_formatter;
    duration_formatter::duration_put(td, os);
    return os;
  }

  
}}


int main() {
  using namespace boost::gregorian;
  using namespace boost::posix_time;

  try {
    date d(2004,Oct,13);
    date min_date(min_date_time);
    date max_date(max_date_time);

    date_period dp(d, d + date_duration(7));
    ptime t(d, time_duration(18,01,56));
    ptime tf = t + microseconds(3);
    time_period tp(t, tf + days(7) + time_duration(1,1,1));
    time_duration td = hours(3) + minutes(2) + seconds(1) + milliseconds(9);

    std::copy(&short_month_names[0], 
              &short_month_names[12],
              std::back_inserter(de_short_month_names));
    
    std::copy(&long_month_names[0], 
              &long_month_names[12],
              std::back_inserter(de_long_month_names));

    
    std::wofstream ss("testoutput.txt");
    std::locale cloc(std::locale::classic());
    
    //
    ss.imbue(cloc); 


    teststreaming("widestream default classic time", t, 
                  std::wstring(L"Wed Oct 13 18:01:56 2004"));
    teststreaming("widestream default classic time with fractional seconds truncated", t, 
                  std::wstring(L"Wed Oct 13 18:01:56 2004"));
    teststreaming("widestream default time period with fractional seconds truncated", tp, 
                  std::wstring(L"[Wed Oct 13 18:01:56 2004/Wed Oct 20 19:02:57 2004]"));
    teststreaming("widestream time duration", td, 
                  std::wstring(L"03:02:01.009000000"));

    wptime_facet *timefacet = new wptime_facet();
    cloc = std::locale(cloc, timefacet);
    ss.imbue(cloc);
//     ss << L"classic date:               " << d << std::endl;
//     ss << L"classic dateperiod:         " << dp << std::endl;
    ss << L"classic time:               " << t << std::endl;
    ss << L"classic timefrac:           " << tf << std::endl;
    ss << L"classic timeperiod:         " << tp << std::endl;

    {
      wptime_facet* wtimefacet = new wptime_facet(L"day: %j date: %Y-%b-%d weekday: %A time: %H:%M:%s");
#ifdef BOOST_DATE_TIME_HAS_NANOSECONDS
      teststreaming("widestream custom time facet narly format", t, 
                    std::wstring(L"day: 287 date: 2004-Oct-13 weekday: Wednesday time: 18:01:56.000000000"), 
                    std::locale(std::locale::classic(), wtimefacet));
#else
      teststreaming("widestream custom time facet narly format", t, 
                    std::wstring(L"day: 287 date: 2004-Oct-13 weekday: Wednesday time: 18:01:56.000000"), 
                    std::locale(std::locale::classic(), wtimefacet));
#endif
    }
    {
      wptime_facet* wtimefacet = new wptime_facet(L"%Y-%b-%d %H:%M:%S,%f");
#ifdef BOOST_DATE_TIME_HAS_NANOSECONDS
      teststreaming("widestream custom time fractional seconds: %Y-%b-%d %H:%M:%S,%f", t, 
                    std::wstring(L"2004-Oct-13 18:01:56,000000000"), 
                    std::locale(std::locale::classic(), wtimefacet));
#else
      teststreaming("widestream custom time fractional seconds: %Y-%b-%d %H:%M:%S,%f", t, 
                    std::wstring(L"2004-Oct-13 18:01:56,000000"), 
                    std::locale(std::locale::classic(), wtimefacet));
#endif
    }

    {
      wptime_facet* wtimefacet = new wptime_facet(L"%Y-%b-%d %H:%M:%S");
      teststreaming("widestream custom time no frac seconds: %Y-%b-%d %H:%M:%S", t, 
                    std::wstring(L"2004-Oct-13 18:01:56"), 
                    std::locale(std::locale::classic(), wtimefacet));
    }

    {
      wptime_facet* wtimefacet = new wptime_facet(L"%Y-%b-%d %H:%M:%S");
      wtimefacet->short_month_names(de_short_month_names);
      teststreaming("widestream custom time no frac seconds, german months: %Y-%b-%d %H:%M:%S", t, 
                    std::wstring(L"2004-Okt-13 18:01:56"), 
                    std::locale(std::locale::classic(), wtimefacet));
    }

    {
      wptime_facet* wtimefacet = new wptime_facet();
      wtimefacet->format(L"%B %b %Y");
      wtimefacet->short_month_names(de_short_month_names);
      wtimefacet->long_month_names(de_long_month_names);
      teststreaming("widestream custom time no frac seconds, german months: %B %b %Y", t, 
                    std::wstring(L"Oktober Okt 2004"), 
                    std::locale(std::locale::classic(), wtimefacet));
    }

    {
      wptime_facet* wtimefacet = new wptime_facet(L"%Y-%b-%d %H:%M:%S%F");
      teststreaming("widestream custom time no frac seconds %F operator: %Y-%b-%d %H:%M:%S%F", t, 
                    std::wstring(L"2004-Oct-13 18:01:56"), 
                    std::locale(std::locale::classic(), wtimefacet));
    }

    {
      wptime_facet* wtimefacet = new wptime_facet(L"%Y-%b-%d %H:%M:%S%F");
      teststreaming("widestream custom time with frac seconds %F operator: %Y-%b-%d %H:%M:%S%F", tf, 
                    std::wstring(L"2004-Oct-13 18:01:56.000003000"), 
                    std::locale(std::locale::classic(), wtimefacet));
    }
    {
      wptime_facet* wtimefacet = new wptime_facet();
      wtimefacet->set_iso_format();
      teststreaming("widestream custom time iso format", tf, 
                    std::wstring(L"20041013T180156.000003000"), 
                    std::locale(std::locale::classic(), wtimefacet));
    }
    {
      wptime_facet* wtimefacet = new wptime_facet();
      wtimefacet->set_iso_extended_format();
      teststreaming("widestream custom time iso extended format", tf, 
                    std::wstring(L"2004-10-13 18:01:56.000003000"), 
                    std::locale(std::locale::classic(), wtimefacet));
    }


    {
      wptime_facet* wtimefacet = new wptime_facet(L"%Y-%b-%d %H:%M:%S%F");
      teststreaming("widestream time period frac seconds %F operator: %Y-%b-%d %H:%M:%S%F", tp, 
                    std::wstring(L"[2004-Oct-13 18:01:56/2004-Oct-20 19:02:57.000002999]"), 
                    std::locale(std::locale::classic(), wtimefacet));
    }

    {
      wptime_facet* wtimefacet = new wptime_facet(L"%Y-%b-%d %H:%M:%s");
      wperiod_formatter pf(wperiod_formatter::AS_OPEN_RANGE, L" / ", L"[ ", L" )", L" ]");
      wtimefacet->period_formatter(pf);

      teststreaming("widestream custom time : %Y-%b-%d %H:%M:%s", tp, 
                    std::wstring(L"[ 2004-Oct-13 18:01:56.000000000 / 2004-Oct-20 19:02:57.000003000 )"), 
                    std::locale(std::locale::classic(), wtimefacet));
    }


    {
      ptime nt(not_a_date_time);
      teststreaming("widestream custom time : not a datetime", nt,
                    std::wstring(L"not-a-date-time"));
    }




//     //Denmark English has iso extended default format...
//     std::locale gloc("en_DK"); 
//     ss.imbue(gloc);
//     ss << L"default english-denmark date:        " << d << std::endl;
//     ss << L"default english-denmark dateperiod:  " << dp << std::endl;
//     ss << L"default english-denmark time:        " << t << std::endl;
//     ss << L"default english-denmark timefrac:    " << tf << std::endl;
//     ss << L"default english-denmark timeperiod:  " << tp << std::endl;



  
  }
  catch(std::exception& e) {
    std::cout << "Caught std::exception: " << e.what() << std::endl;
  }
  catch(...) {
    std::cout << "bad exception" << std::endl;
  }

  return printTestStats();
}
