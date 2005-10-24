/* Copyright (c) 2002,2003,2004 CrystalClear Software, Inc.
 * Use, modification and distribution is subject to the
 * Boost Software License, Version 1.0. (See accompanying
 * file LICENSE-1.0 or http://www.boost.org/LICENSE-1.0)
 * Author: Jeff Garland, Bart Garst
 */
 
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/gregorian/greg_serialize.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/archive/xml_iarchive.hpp>
#include <boost/date_time/testfrmwk.hpp>
#include <fstream>

using namespace boost;
using namespace gregorian;

int main(){
  std::ofstream ofs("tmp_file");
  
  // NOTE: DATE_TIME_XML_SERIALIZE is only used in testing and is
  // defined in the testing Jamfile
#if defined(DATE_TIME_XML_SERIALIZE)
  std::cout << "Running xml archive tests" << std::endl;
  archive::xml_oarchive oa(ofs);
#else
  std::cout << "Running text archive tests" << std::endl;
  archive::text_oarchive oa(ofs);
#endif
  
  date d(2002,Feb,12);
  date_duration dd(11);
  date_period dp(d,dd);
  greg_month gm(Feb);
  greg_day gd(14);
  greg_weekday gwd(Friday);
  partial_date pd(26,Jul);
  nth_kday_of_month nkd(nth_kday_of_month::second,Tuesday,Mar);
  first_kday_of_month fkd(Saturday,Apr);
  last_kday_of_month lkd(Saturday,Apr);
  first_kday_before fkdb(Thursday);
  first_kday_after fkda(Thursday);

  // load up the archive
#if defined(DATE_TIME_XML_SERIALIZE)
  try{
    oa << BOOST_SERIALIZATION_NVP(d);
    oa << BOOST_SERIALIZATION_NVP(dd);
    oa << BOOST_SERIALIZATION_NVP(dp);
    oa << BOOST_SERIALIZATION_NVP(gm);
    oa << BOOST_SERIALIZATION_NVP(gd);
    oa << BOOST_SERIALIZATION_NVP(gwd);
    oa << BOOST_SERIALIZATION_NVP(pd);
    oa << BOOST_SERIALIZATION_NVP(nkd);
    oa << BOOST_SERIALIZATION_NVP(fkd);
    oa << BOOST_SERIALIZATION_NVP(lkd);
    oa << BOOST_SERIALIZATION_NVP(fkdb);
    oa << BOOST_SERIALIZATION_NVP(fkda);

  }catch(archive::archive_exception ae){
    std::string s(ae.what());
    check("Error writing to archive: " + s, false);
    ofs.close();
    return printTestStats();
  }
#else
  try{
    oa << d;
    oa << dd;
    oa << dp;
    oa << gm;
    oa << gd;
    oa << gwd;
    oa << pd;
    oa << nkd;
    oa << fkd;
    oa << lkd;
    oa << fkdb;
    oa << fkda;
  }catch(archive::archive_exception ae){
    std::string s(ae.what());
    check("Error writing to archive: " + s, false);
    ofs.close();
    return printTestStats();
  }
#endif // DATE_TIME_XML_SERIALIZE
  ofs.close();

  std::ifstream ifs("tmp_file");
#if defined(DATE_TIME_XML_SERIALIZE)
  archive::xml_iarchive ia(ifs);
#else
  archive::text_iarchive ia(ifs);
#endif
  
  // read from the archive
  date d2(not_a_date_time);
  date_duration dd2(not_a_date_time);
  date_period dp2(date(2000,Jan,1),date_duration(1));
  greg_month gm2(Jan);
  greg_day gd2(1);
  greg_weekday gwd2(Monday);
  partial_date pd2(1);
  nth_kday_of_month nkd2(nth_kday_of_month::first,Monday,Jan);
  first_kday_of_month fkd2(Monday,Jan);
  last_kday_of_month lkd2(Monday,Jan);
  first_kday_before fkdb2(Monday);
  first_kday_after fkda2(Monday);
#if defined(DATE_TIME_XML_SERIALIZE)
  try{
    ia >> BOOST_SERIALIZATION_NVP(d2);
    ia >> BOOST_SERIALIZATION_NVP(dd2);
    ia >> BOOST_SERIALIZATION_NVP(dp2);
    ia >> BOOST_SERIALIZATION_NVP(gm2);
    ia >> BOOST_SERIALIZATION_NVP(gd2);
    ia >> BOOST_SERIALIZATION_NVP(gwd2);
    ia >> BOOST_SERIALIZATION_NVP(pd2);
    ia >> BOOST_SERIALIZATION_NVP(nkd2);
    ia >> BOOST_SERIALIZATION_NVP(fkd2);
    ia >> BOOST_SERIALIZATION_NVP(lkd2);
    ia >> BOOST_SERIALIZATION_NVP(fkdb2);
    ia >> BOOST_SERIALIZATION_NVP(fkda2);
 
  }catch(archive::archive_exception ae){
    std::string s(ae.what());
    check("Error reading from archive: " + s, false);
    ifs.close();
    return printTestStats();
  }
#else
  try{
    ia >> d2;
    ia >> dd2;
    ia >> dp2;
    ia >> gm2;
    ia >> gd2;
    ia >> gwd2;
    ia >> pd2;
    ia >> nkd2;
    ia >> fkd2;
    ia >> lkd2;
    ia >> fkdb2;
    ia >> fkda2;
  }catch(archive::archive_exception ae){
    std::string s(ae.what());
    check("Error reading from archive: " + s, false);
    ifs.close();
    return printTestStats();
  }
#endif // DATE_TIME_XML_SERIALIZE
  ifs.close();
  
  check("date", d == d2);
  check("date_duration", dd == dd2);
  check("date_period", dp == dp2);
  check("greg_month", gm == gm2);
  check("greg_day", gd == gd2);
  check("greg_weekday", gwd == gwd2);
  check("date_generator: partial_date", pd == pd2);
  check("date_generator: nth_kday_of_month", nkd.get_date(2002) == nkd2.get_date(2002)); // no operator== for nth_kday_of_week - yet
  check("date_generator: first_kday_of_month", fkd.get_date(2002) == fkd2.get_date(2002)); // no operator== for first_kday_of_week - yet
  check("date_generator: last_kday_of_month", lkd.get_date(2002) == lkd2.get_date(2002)); // no operator== for last_kday_of_week - yet
  check("date_generator: first_kday_before", fkdb.get_date(d) == fkdb2.get_date(d)); // no operator== for first_kday_before - yet
  check("date_generator: first_kday_after", fkda.get_date(d) == fkda2.get_date(d)); // no operator== for first_kday_after - yet
  

  return printTestStats();
}
