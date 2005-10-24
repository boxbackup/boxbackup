

/* Copyright (c) 2004 CrystalClear Software, Inc.
 * Use, modification and distribution is subject to the 
 * Boost Software License, Version 1.0. (See accompanying
 * file LICENSE-1.0 or http://www.boost.org/LICENSE-1.0)
 * Author: Jeff Garland
 * $Date: 2004/10/14 00:04:34 $
 */

#include "boost/date_time/gregorian/gregorian.hpp"
#include "boost/date_time/date_facet.hpp"
#include "boost/date_time/testfrmwk.hpp"
#include <iostream>
#include <sstream>

//temporary output code in the test file.  This will eventually move into the core
//of the library...
namespace boost { namespace gregorian {

  typedef boost::date_time::period_formatter<wchar_t> wperiod_formatter;
  typedef boost::date_time::period_formatter<char>    period_formatter;
  
  typedef boost::date_time::date_facet<date,wchar_t> wdate_facet;
  typedef boost::date_time::date_facet<date,char>    date_facet;


  template <class CharT, class TraitsT>
  inline std::basic_ostream<CharT, TraitsT>&
  operator<<(std::basic_ostream<CharT, TraitsT>& os, const boost::gregorian::date& d) {
    typedef boost::date_time::date_facet<date, CharT> custom_date_facet;
    typedef std::time_put<CharT>                std_date_facet;
    std::ostreambuf_iterator<CharT> oitr(os);
    if (std::has_facet<custom_date_facet>(os.getloc()))
      std::use_facet<custom_date_facet>(os.getloc()).put(oitr, os, os.fill(), d);
    else {
      //instantiate a custom facet for dealing with dates since the user
      //has not put one in the stream so far.  This is for efficiency 
      //since we would always need to reconstruct for every date
      //if the locale did not already exist.  Of course this will be overridden
      //if the user imbues at some later point.  With the default settings
      //for the facet the resulting format will be the same as the
      //std::time_facet settings.
      std::ostreambuf_iterator<CharT> oitr(os);
      custom_date_facet* f = new custom_date_facet();
      std::locale l = std::locale(os.getloc(), f);
      os.imbue(l);
      f->put(oitr, os, os.fill(), d);
    }
    return os;
  }


  template <class charT, class traits>
  inline
  std::basic_ostream<charT, traits>&
  operator<<(std::basic_ostream<charT, traits>& os, const date_duration& dd)
  {
    os << dd.get_rep();
    return os;
  }


  template <class CharT, class TraitsT>
  inline std::basic_ostream<CharT, TraitsT>&
  operator<<(std::basic_ostream<CharT, TraitsT>& os, const boost::gregorian::date_period& dp) {
    typedef boost::date_time::date_facet<date, CharT> custom_date_facet;
    typedef std::time_put<CharT>                std_date_facet;
    std::ostreambuf_iterator<CharT> oitr(os);
    if (std::has_facet<custom_date_facet>(os.getloc()))
      std::use_facet<custom_date_facet>(os.getloc()).put(oitr, os, os.fill(), dp);
    else {
      //instantiate a custom facet for dealing with date periods since the user
      //has not put one in the stream so far.  This is for efficiency 
      //since we would always need to reconstruct for every time period
      //if the local did not already exist.  Of course this will be overridden
      //if the user imbues at some later point.  With the default settings
      //for the facet the resulting format will be the same as the
      //std::time_facet settings.
      std::ostreambuf_iterator<CharT> oitr(os);
      custom_date_facet* f = new custom_date_facet();
      std::locale l = std::locale(os.getloc(), f);
      os.imbue(l);
      f->put(oitr, os, os.fill(), dp);

    }
    return os;
  }



} } // gregorian



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



const char* const month_short_names[]={"*jan*","*feb*","*mar*",
                                       "*apr*","*may*","*jun*",
                                       "*jul*","*aug*","*sep*",
                                       "*oct*","*nov*","*dec*"};

const char* const month_long_names[]={"**January**","**February**","**March**",
                                      "**April**","**May**","**June**",
                                      "**July**","**August**","**September**",
                                      "**October**","**November**","**December**"};

const char* const weekday_short_names[]={"day1", "day2","day3","day4",
                                         "day5","day6","day7"};

const char* const weekday_long_names[]= {"Sun-0", "Mon-1", "Tue-2", 
                                         "Wed-3", "Thu-4", 
                                         "Fri-5", "Sat-6"};

std::vector<std::basic_string<char> > short_weekday_names;
std::vector<std::basic_string<char> > long_weekday_names;
std::vector<std::basic_string<char> > short_month_names;
std::vector<std::basic_string<char> > long_month_names;


int main() {

  std::copy(&month_short_names[0], 
            &month_short_names[12],
            std::back_inserter(short_month_names));

  std::copy(&month_long_names[0], 
            &month_long_names[12],
            std::back_inserter(long_month_names));

  std::copy(&weekday_short_names[0], 
            &weekday_short_names[7],
            std::back_inserter(short_weekday_names));

  std::copy(&weekday_long_names[0], 
            &weekday_long_names[7],
            std::back_inserter(long_weekday_names));

  using namespace boost::gregorian;

  {
    date d(2004,Oct, 13);
    teststreaming("widestream default classic date", d, std::wstring(L"10/13/04"));
    teststreaming("default classic date", d, std::string("10/13/04"));
    date_period dp(d, d + days(7));
    teststreaming("widestream default classic date period", dp, 
                  std::wstring(L"[10/13/04/10/19/04]"));
    teststreaming("default classic date period", dp, 
                  std::string("[10/13/04/10/19/04]"));
    {
      wdate_facet* wdatefacet = new wdate_facet();
      wdatefacet->format(L"%Y-%d-%b %a");
      teststreaming("widestream custom date facet", d, 
                    std::wstring(L"2004-13-Oct Wed"), 
                    std::locale(std::locale::classic(), wdatefacet));
    }
    {
      wdate_facet* wdatefacet = new wdate_facet();
      wdatefacet->format(L"%Y-%d-%b %a");
      teststreaming("widestream custom date facet date period", dp, 
                    std::wstring(L"[2004-13-Oct Wed/2004-19-Oct Tue]"), 
                    std::locale(std::locale::classic(), wdatefacet));
    }

    {
      date_facet* datefacet = new date_facet();
      datefacet->format("%Y-%d-%b %a");
      teststreaming("custom date facet date period", dp, 
                    std::string("[2004-13-Oct Wed/2004-19-Oct Tue]"), 
                    std::locale(std::locale::classic(), datefacet));
    }

    {
      date_facet* datefacet = new date_facet();
      datefacet->set_iso_format();
      teststreaming("custom date facet date", d, 
                    std::string("20041013"), 
                    std::locale(std::locale::classic(), datefacet));

    }
    {
      date_facet* datefacet = new date_facet();
      datefacet->set_iso_format();
      teststreaming("custom date facet date period", dp, 
                    std::string("[20041013/20041019]"), 
                    std::locale(std::locale::classic(), datefacet));
    }

    {
      date_facet* datefacet = new date_facet();
      datefacet->set_iso_extended_format();
      teststreaming("custom date facet date", d, 
                    std::string("2004-10-13"), 
                    std::locale(std::locale::classic(), datefacet));

    }
    {
      date_facet* datefacet = new date_facet();
      datefacet->set_iso_extended_format();
      teststreaming("custom date facet date period", dp, 
                    std::string("[2004-10-13/2004-10-19]"), 
                    std::locale(std::locale::classic(), datefacet));
    }

    {
      date_facet* datefacet = new date_facet();
      datefacet->set_iso_extended_format();
      period_formatter pf(period_formatter::AS_OPEN_RANGE, " / ", "[ ", " )", " ]");
      datefacet->period_formatter(pf);
      teststreaming("custom date facet date period - open range custom delimeters", dp, 
                    std::string("[ 2004-10-13 / 2004-10-20 )"), 
                    std::locale(std::locale::classic(), datefacet));
    }

    {
      wdate_facet* wdatefacet = new wdate_facet();
      wdatefacet->set_iso_extended_format();
      wperiod_formatter pf(wperiod_formatter::AS_OPEN_RANGE, L" / ", L"[ ", L" )", L" ]");
      wdatefacet->period_formatter(pf);
      teststreaming("custom date facet date period - open range custom delimeters", dp, 
                    std::wstring(L"[ 2004-10-13 / 2004-10-20 )"), 
                    std::locale(std::locale::classic(), wdatefacet));
    }
    {
      date_facet* datefacet = new date_facet("%A %b %d, %Y");
      datefacet->short_month_names(short_month_names);
      teststreaming("custom date facet -- custom short month names", d, 
                    std::string("Wednesday *oct* 13, 2004"), 
                    std::locale(std::locale::classic(), datefacet));
    }

    {
      date_facet* datefacet = new date_facet("%B %A %d, %Y");
      datefacet->long_month_names(long_month_names);
      teststreaming("custom date facet -- custom long month names", d, 
                    std::string("**October** Wednesday 13, 2004"), 
                    std::locale(std::locale::classic(), datefacet));
    }

    {
      date_facet* datefacet = new date_facet("%a - %b %d, %Y");
      datefacet->short_weekday_names(short_weekday_names);
      std::cout.imbue(std::locale(std::locale::classic(), datefacet));
      std::cout << d << std::endl;
      teststreaming("custom date facet -- custom short weekday names", d, 
                    std::string("day4 - Oct 13, 2004"), 
                    std::locale(std::locale::classic(), datefacet));
    }

    {
      date_facet* datefacet = new date_facet("%b %d, %Y ++ %A");
      datefacet->long_weekday_names(long_weekday_names);
      teststreaming("custom date facet -- custom short weekday names", d, 
                    std::string("Oct 13, 2004 ++ Wed-3"), 
                    std::locale(std::locale::classic(), datefacet));
    }



    date d_not_date(not_a_date_time);
    teststreaming("special value, no special facet", d_not_date, std::string("not-a-date-time"));
    teststreaming("special value, no special facet wide", d_not_date, 
                  std::wstring(L"not-a-date-time"));


//       std::cout.imbue(std::locale(std::locale::classic(), datefacet));
//       std::cout << d << std::endl;


  }

  return printTestStats();
}
