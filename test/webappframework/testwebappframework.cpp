// --------------------------------------------------------------------------
//
// File
//		Name:    testwebappframework.cpp
//		Purpose: Test simple web application framework
//		Created: 30/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "Test.h"
#include "autogen_webapp/TestWebAppServer.h"
#include "autogen_db/TestWebApp_schema.h"
#include "DatabaseConnection.h"
#include "WAFUtilityFns.h"
#include "WAFUKPostcode.h"

#include "MemLeakFindOn.h"

void test_ukpostcode_validate(const char *in, bool willValidate, const char *expected)
{
	WAFUKPostcode postcode;
	bool result = postcode.ParseAndValidate(std::string(in));
	std::string normalised(postcode.NormalisedPostcode());
	TEST_THAT(result == willValidate);
	TEST_THAT(normalised == expected);
}

void test_ukpostcode()
{
	test_ukpostcode_validate("a12BN", true, "A1 2BN");
	test_ukpostcode_validate("a1 2BN", true, "A1 2BN");
	test_ukpostcode_validate("   a12 BN   ", true, "A1 2BN");
	test_ukpostcode_validate("ab2BN", false, "");
	test_ukpostcode_validate("a132BN", true, "A13 2BN");
	test_ukpostcode_validate("a132B3", false, "");
	test_ukpostcode_validate("a1323A", false, "");
	test_ukpostcode_validate("a13ABN", false, "");
	test_ukpostcode_validate("aW32BN", true, "AW3 2BN");
	test_ukpostcode_validate("a3q2BN", true, "A3Q 2BN");
	test_ukpostcode_validate("aw872BN", true, "AW87 2BN");
	test_ukpostcode_validate("aw8t 2BN", true, "AW8T 2BN");
	test_ukpostcode_validate("awqt 2BN", false, "");
	test_ukpostcode_validate("aw8722BN", false, "");
}

void test_phonenumber_validate(const char *in, bool willValidate, const char *expected)
{
	std::string canonical;
	bool result = WAFUtility::ValidatePhoneNumber(std::string(in), canonical);
	TEST_THAT(result == willValidate);
	TEST_THAT(canonical == expected);
}

void test_phonenumber()
{
	test_phonenumber_validate("+44 12746382", true, "+44 12746382");
	test_phonenumber_validate("44  12746382", true, "44 12746382");
	test_phonenumber_validate("+44 12746382    ", true, "+44 12746382");
	test_phonenumber_validate("02012746382", true, "02012746382");
	test_phonenumber_validate("    02012746382", true, "02012746382");
	test_phonenumber_validate("+44 127A46382", false, "");
	test_phonenumber_validate("++44 12746382    ", false, "");
	test_phonenumber_validate("44 12,746.382    ", true, "44 12,746.382");
}

void test_fixedpoint_parse(const char *in, int scale, bool parse, int expected)
{
	std::string input(in);
	int result = 0;
	bool parseOK = WAFUtility::ParseFixedPointDecimal(input, result, scale);
	TEST_THAT(parseOK == parse);
	if(parseOK)
	{
		TEST_THAT(result == expected);
	}
//	TRACE4("%s,%d,%d,%d\n", in, scale, parse, result);
}

void test_fixedpoint_gen(int in, int scale, int places, const char *expected)
{
	std::string out("rubbish");
	WAFUtility::FixedPointDecimalToString(in, out, scale, places);
	TEST_THAT(out == expected);
//	TRACE4("%d,%d,%s,%s\n", in, scale, out.c_str(), expected);
}

void test_fixedpoint()
{
	// Test parsing strings
	test_fixedpoint_parse("1.0103", 4, true, 10103);
	test_fixedpoint_parse("1.010", 4, true, 10100);
	test_fixedpoint_parse("1.00001", 4, true, 10000);
	test_fixedpoint_parse("1.00006", 4, true, 10001);
	test_fixedpoint_parse("pants", 4, false, 0);
	test_fixedpoint_parse("1,0", 4, false, 0);
	test_fixedpoint_parse("1,0apples", 4, false, 0);
	test_fixedpoint_parse("1.00006 pants", 4, false, 0);
	test_fixedpoint_parse("  1 . 010   ", 4, true, 10100);
	test_fixedpoint_parse("  1 . 01 0   ", 4, false, 10100);
	// negative numbers
	test_fixedpoint_parse("-1.00001", 4, true, -10000);
	test_fixedpoint_parse("-100.00", 4, true, -1000000);

	// Check representation of negative numbers
	{
		int32_t n1,n2;
		TEST_THAT(WAFUtility::ParseFixedPointDecimal(std::string("-23.56"), n1, 4));
		TEST_THAT(WAFUtility::ParseFixedPointDecimal(std::string("-12.87"), n2, 4));
		std::string out;
		WAFUtility::FixedPointDecimalToString(n1+n2, out, 4, 4);
		TEST_THAT(out == "-36.4300");
	}

	// Test generating strings
	test_fixedpoint_gen(10003, 4, 2, "1.0003");
	test_fixedpoint_gen(10020, 4, 2, "1.002");
	test_fixedpoint_gen(10100, 4, 2, "1.01");
	test_fixedpoint_gen(19000, 4, 2, "1.90");
	test_fixedpoint_gen(-19000, 4, 2, "-1.90");
	test_fixedpoint_gen(10000, 4, 2, "1.00");
	test_fixedpoint_gen(10000, 4, 0, "1");
	test_fixedpoint_gen(2360000, 4, 0, "236");
	test_fixedpoint_gen(2360020, 4, 0, "236.002");
	test_fixedpoint_gen(0, 4, 0, "0");
	test_fixedpoint_gen(0, 4, 2, "0.00");

	// Test all possible lengths to make sure the embedded table is correct
	{
		std::string expected("1");
		int v = 1;
		for(int l = 0; l <= WAFUTILITY_FIXEDPOINT_MAX_SCALE_DIGITS; ++l)
		{
			test_fixedpoint_gen(v, l, l, expected.c_str());
			test_fixedpoint_parse(expected.c_str(), l, true, v);
			// Next!
			if(l == 0) expected += '.';
			v *= 10;
			v += (l+1);
			expected += (l+1)+'0';
		}
	}
}

int test(int argc, const char *argv[])
{
	if(argc >= 2 && ::strcmp(argv[1], "server") == 0)
	{
		// Run a server
		TestWebAppServer server;
		return server.Main("doesnotexist", argc - 1, argv + 1);
	}
	
	// Test some utility functions
	test_fixedpoint();
	test_phonenumber();
	test_ukpostcode();
	
	// Create the database
	::unlink("testfiles/database.sqlite");
	{
		DatabaseConnection db;
		db.Connect(std::string("sqlite"), std::string("testfiles/database.sqlite"), 1000);
		TestWebApp_Create(db);
	}
	
	// Start the server
	int pid = LaunchServer("./test server testfiles/testwebapp.conf", "testfiles/testwebapp.pid");
	TEST_THAT(pid != -1 && pid != 0);
	if(pid > 0)
	{
		// Run the request script
//		TEST_THAT(::system("perl testfiles/testrequests.pl") == 0);
	
		// Kill it
		TEST_THAT(KillServer(pid));
		TestRemoteProcessMemLeaks("testwebapp.memleaks");
	}
	
	printf("\nTo launch the server, type:\n    ./test server testfiles/testwebapp.conf\n\nTo try the server with a web browser, go to\n    http://localhost:1080/test/en/lgin/username/0\n    http://localhost:1080/test/CAPS/lgin/username/0\n(different translations)\n\nTo stop the server, type\n    xargs kill < testfiles/testwebapp.pid\n\n");

	return 0;
}

