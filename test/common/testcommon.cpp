// --------------------------------------------------------------------------
//
// File
//		Name:    testcommon.cpp
//		Purpose: Tests for the code in lib/common
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include "Test.h"
#include "Configuration.h"
#include "FdGetLine.h"
#include "Guards.h"
#include "FileStream.h"
#include "IOStreamGetLine.h"
#include "NamedLock.h"
#include "ReadGatherStream.h"
#include "MemBlockStream.h"
#include "ExcludeList.h"
#include "CommonException.h"
#include "Conversion.h"
#include "autogen_ConversionException.h"

#include "MemLeakFindOn.h"

using namespace BoxConvert;

void test_conversions()
{
	TEST_THAT((Convert<int32_t, const std::string &>(std::string("32"))) == 32);
	TEST_THAT((Convert<int32_t, const char *>("42")) == 42);
	TEST_THAT((Convert<int32_t, const char *>("-42")) == -42);
	TEST_CHECK_THROWS((Convert<int8_t, const char *>("500")), ConversionException, IntOverflowInConvertFromString);
	TEST_CHECK_THROWS((Convert<int8_t, const char *>("pants")), ConversionException, BadStringRepresentationOfInt);
	TEST_CHECK_THROWS((Convert<int8_t, const char *>("")), ConversionException, CannotConvertEmptyStringToInt);
	
	std::string a(Convert<std::string, int32_t>(63));
	TEST_THAT(a == "63");
	std::string b(Convert<std::string, int32_t>(-3473463));
	TEST_THAT(b == "-3473463");
	std::string c(Convert<std::string, int16_t>(344));
	TEST_THAT(c == "344");
}

ConfigurationVerifyKey verifykeys1_1_1[] =
{
	{"bing", 0, ConfigTest_Exists, 0},
	{"carrots", 0, ConfigTest_Exists | ConfigTest_IsInt, 0},
	{"terrible", 0, ConfigTest_Exists | ConfigTest_LastEntry, 0}
};

ConfigurationVerifyKey verifykeys1_1_2[] =
{
	{"fish", 0, ConfigTest_Exists | ConfigTest_IsInt, 0},
	{"string", 0, ConfigTest_Exists | ConfigTest_LastEntry, 0}
};


ConfigurationVerify verifysub1_1[] = 
{
	{
		"*",
		0,
		verifykeys1_1_1,
		ConfigTest_Exists,
		0
	},
	{
		"otherthing",
		0,
		verifykeys1_1_2,
		ConfigTest_Exists | ConfigTest_LastEntry,
		0
	}
};

ConfigurationVerifyKey verifykeys1_1[] =
{
	{"value", 0, ConfigTest_Exists | ConfigTest_IsInt, 0},
	{"string1", 0, ConfigTest_Exists, 0},
	{"string2", 0, ConfigTest_Exists | ConfigTest_LastEntry, 0}
};

ConfigurationVerifyKey verifykeys1_2[] = 
{
	{"carrots", 0, ConfigTest_Exists | ConfigTest_IsInt, 0},
	{"string", 0, ConfigTest_Exists | ConfigTest_LastEntry, 0}
};

ConfigurationVerify verifysub1[] = 
{
	{
		"test1",
		verifysub1_1,
		verifykeys1_1,
		ConfigTest_Exists,
		0
	},
	{
		"ping",
		0,
		verifykeys1_2,
		ConfigTest_Exists | ConfigTest_LastEntry,
		0
	}
};

ConfigurationVerifyKey verifykeys1[] =
{
		{"notExpected", 0, 0, 0},
		{"HasDefaultValue", "Lovely default value", 0, 0},
		{"MultiValue", 0, ConfigTest_MultiValueAllowed, 0},
		{"BoolTrue1", 0, ConfigTest_IsBool, 0},
		{"BoolTrue2", 0, ConfigTest_IsBool, 0},
		{"BoolFalse1", 0, ConfigTest_IsBool, 0},
		{"BoolFalse2", 0, ConfigTest_IsBool, 0},
		{"TOPlevel", 0, ConfigTest_LastEntry | ConfigTest_Exists, 0}
};

ConfigurationVerify verify =
{
	"root",
	verifysub1,
	verifykeys1,
	ConfigTest_Exists | ConfigTest_LastEntry,
	0
};

int test(int argc, const char *argv[])
{
	// Test memory leak detection
#ifdef BOX_MEMORY_LEAK_TESTING
	{
		TEST_THAT(memleakfinder_numleaks() == 0);
		void *block = ::malloc(12);
		TEST_THAT(memleakfinder_numleaks() == 1);
		void *b2 = ::realloc(block, 128*1024);
		TEST_THAT(block != b2);
		TEST_THAT(memleakfinder_numleaks() == 1);
		::free(b2);
		TEST_THAT(memleakfinder_numleaks() == 0);
		char *test = new char[1024];
		TEST_THAT(memleakfinder_numleaks() == 1);
		MemBlockStream *s = new MemBlockStream(test,12);
		TEST_THAT(memleakfinder_numleaks() == 2);
		delete s;
		TEST_THAT(memleakfinder_numleaks() == 1);
		delete [] test;
		TEST_THAT(memleakfinder_numleaks() == 0);
	}
#endif // BOX_MEMORY_LEAK_TESTING
	

	static char *testfilelines[] =
	{
		"First line",
		"Second line",
		"Third",
		"",
		"",
		"",
		"sdf hjjk",
		"",
		"test",
		"test#not comment",
		"test#not comment",
		"",
		"nice line",
		"fish",
		"",
		"ping",
		"",
		"",
		"Nothing",
		"Nothing",
		0
	};

	// First, test the FdGetLine class -- rather important this works!
	{
		FileHandleGuard<O_RDONLY> file("testfiles/fdgetlinetest.txt");
		FdGetLine getline(file);
		
  		int l = 0;
  		while(testfilelines[l] != 0)
  		{
	  		TEST_THAT(!getline.IsEOF());
	  		std::string line = getline.GetLine(true);
	  		//printf("expected |%s| got |%s|\n", lines[l], line.c_str());
	  		TEST_THAT(strcmp(testfilelines[l], line.c_str()) == 0);
	  		l++;
  		}
  		TEST_THAT(getline.IsEOF());
  		TEST_CHECK_THROWS(getline.GetLine(true), CommonException, GetLineEOF);
	}
	// and again without pre-processing
	{
		FileHandleGuard<O_RDONLY> file("testfiles/fdgetlinetest.txt");
		FILE *file2 = fopen("testfiles/fdgetlinetest.txt", "r");
		TEST_THAT_ABORTONFAIL(file2 != 0);
		FdGetLine getline(file);
		char ll[512];
		
		while(!feof(file2))
		{
			fgets(ll, sizeof(ll), file2);
			int e = strlen(ll);
			while(e > 0 && (ll[e-1] == '\n' || ll[e-1] == '\r'))
			{
				e--;
			}
			ll[e] = '\0';
		
	  		TEST_THAT(!getline.IsEOF());
	  		std::string line = getline.GetLine(false);
	  		//printf("expected |%s| got |%s|\n", ll, line.c_str());
	  		TEST_THAT(strcmp(ll, line.c_str()) == 0);
		}
  		TEST_THAT(getline.IsEOF());
  		TEST_CHECK_THROWS(getline.GetLine(true), CommonException, GetLineEOF);
		
		fclose(file2);
	}
	
	// Then the IOStream version of get line, seeing as we're here...
	{
		FileStream file("testfiles/fdgetlinetest.txt", O_RDONLY);
		IOStreamGetLine getline(file);
		
  		int l = 0;
  		while(testfilelines[l] != 0)
  		{
	  		TEST_THAT(!getline.IsEOF());
	  		std::string line;
	  		while(!getline.GetLine(line, true))
	  			;
	  		//printf("expected |%s| got |%s|\n", lines[l], line.c_str());
	  		TEST_THAT(strcmp(testfilelines[l], line.c_str()) == 0);
	  		l++;
  		}
  		TEST_THAT(getline.IsEOF());
  		std::string dummy;
  		TEST_CHECK_THROWS(getline.GetLine(dummy, true), CommonException, GetLineEOF);
	}
	// and again without pre-processing
	{
		FileStream file("testfiles/fdgetlinetest.txt", O_RDONLY);
		IOStreamGetLine getline(file);

		FILE *file2 = fopen("testfiles/fdgetlinetest.txt", "r");
		TEST_THAT_ABORTONFAIL(file2 != 0);
		char ll[512];
		
		while(!feof(file2))
		{
			fgets(ll, sizeof(ll), file2);
			int e = strlen(ll);
			while(e > 0 && (ll[e-1] == '\n' || ll[e-1] == '\r'))
			{
				e--;
			}
			ll[e] = '\0';
		
	  		TEST_THAT(!getline.IsEOF());
	  		std::string line;
	  		while(!getline.GetLine(line, false))
	  			;
	  		//printf("expected |%s| got |%s|\n", ll, line.c_str());
	  		TEST_THAT(strcmp(ll, line.c_str()) == 0);
		}
  		TEST_THAT(getline.IsEOF());
  		std::string dummy;
  		TEST_CHECK_THROWS(getline.GetLine(dummy, true), CommonException, GetLineEOF);
		
		fclose(file2);
	}
	
	// Doesn't exist
	{
		std::string errMsg;
		TEST_CHECK_THROWS(std::auto_ptr<Configuration> pconfig(Configuration::LoadAndVerify("testfiles/DOESNTEXIST", &verify, errMsg)), CommonException, OSFileOpenError);
	}

	// Basic configuration test
	{
		std::string errMsg;
		std::auto_ptr<Configuration> pconfig(Configuration::LoadAndVerify("testfiles/config1.txt", &verify, errMsg));
		if(!errMsg.empty())
		{
			printf("UNEXPECTED error msg is:\n------\n%s------\n", errMsg.c_str());
		}
		TEST_THAT_ABORTONFAIL(pconfig.get() != 0);
		TEST_THAT(errMsg.empty());
		TEST_THAT(pconfig->KeyExists("TOPlevel"));
		TEST_THAT(pconfig->GetKeyValue("TOPlevel") == "value");
		TEST_THAT(pconfig->KeyExists("MultiValue"));
		TEST_THAT(pconfig->GetKeyValue("MultiValue") == "single");
		TEST_THAT(!pconfig->KeyExists("not exist"));
		TEST_THAT(pconfig->KeyExists("HasDefaultValue"));
		TEST_THAT(pconfig->GetKeyValue("HasDefaultValue") == "Lovely default value");
		TEST_CHECK_THROWS(pconfig->GetKeyValue("not exist"), CommonException, ConfigNoKey);
		// list of keys
		std::vector<std::string> keylist(pconfig->GetKeyNames());
		TEST_THAT(keylist.size() == 3);
		// will be sorted alphanumerically
		TEST_THAT(keylist[2] == "TOPlevel" && keylist[1] == "MultiValue" && keylist[0] == "HasDefaultValue");
		// list of sub configurations
		std::vector<std::string> sublist(pconfig->GetSubConfigurationNames());
		TEST_THAT(sublist.size() == 2);
		TEST_THAT(sublist[0] == "test1");
		TEST_THAT(sublist[1] == "ping");
		TEST_THAT(pconfig->SubConfigurationExists("test1"));
		TEST_THAT(pconfig->SubConfigurationExists("ping"));
		TEST_CHECK_THROWS(pconfig->GetSubConfiguration("nosubconfig"), CommonException, ConfigNoSubConfig);
		// Get a sub configuration
		const Configuration &sub1 = pconfig->GetSubConfiguration("test1");
		TEST_THAT(sub1.GetKeyValueInt("value") == 12);
		std::vector<std::string> sublist2(sub1.GetSubConfigurationNames());
		TEST_THAT(sublist2.size() == 4);
		// And the sub-sub configs
		const Configuration &sub1_1 = sub1.GetSubConfiguration("subconfig");
		TEST_THAT(sub1_1.GetKeyValueInt("carrots") == 0x2356);
		const Configuration &sub1_2 = sub1.GetSubConfiguration("subconfig2");
		TEST_THAT(sub1_2.GetKeyValueInt("carrots") == -243895);
		const Configuration &sub1_3 = sub1.GetSubConfiguration("subconfig3");
		TEST_THAT(sub1_3.GetKeyValueInt("carrots") == 050);
		TEST_THAT(sub1_3.GetKeyValue("terrible") == "absolutely");
	}	

	static const char *file[] =
	{
		"testfiles/config2.txt", // Value missing from root
		"testfiles/config3.txt", // Unexpected {
		"testfiles/config4.txt", // Missing }
		"testfiles/config5.txt", // { expected, but wasn't there
		"testfiles/config6.txt", // Duplicate key
		"testfiles/config7.txt", // Invalid key (no name)
		"testfiles/config8.txt", // Not all sub blocks terminated
		"testfiles/config9.txt", // Not valid integer
		"testfiles/config9b.txt", // Not valid integer
		"testfiles/config9c.txt", // Not valid integer
		"testfiles/config9d.txt", // Not valid integer
		"testfiles/config10.txt", // Missing key (in subblock)
		"testfiles/config11.txt", // Unknown key
		"testfiles/config12.txt", // Missing block
		"testfiles/config13.txt", // Subconfig (wildcarded) should exist, but missing (ie nothing present)
		"testfiles/config16.txt", // bad boolean value
		0
	};

	for(int l = 0; file[l] != 0; ++l)	
	{
		std::string errMsg;
		std::auto_ptr<Configuration> pconfig(Configuration::LoadAndVerify(file[l], &verify, errMsg));
		TEST_THAT(pconfig.get() == 0);
		TEST_THAT(!errMsg.empty());
		printf("(%s) Error msg is:\n------\n%s------\n", file[l], errMsg.c_str());
	}
	
	// Check that multivalues happen as expected
	// (single value in a multivalue already checked)
	{
		std::string errMsg;
		std::auto_ptr<Configuration> pconfig(Configuration::LoadAndVerify("testfiles/config14.txt", &verify, errMsg));
		TEST_THAT(pconfig.get() != 0);
		TEST_THAT(errMsg.empty());
		TEST_THAT(pconfig->KeyExists("MultiValue"));
		// values are separated by a specific character
		std::string expectedvalue("value1");
		expectedvalue += Configuration::MultiValueSeparator;
		expectedvalue += "secondvalue";
		TEST_THAT(pconfig->GetKeyValue("MultiValue") == expectedvalue);
	}

	// Check boolean values	
	{
		std::string errMsg;
		std::auto_ptr<Configuration> pconfig(Configuration::LoadAndVerify("testfiles/config15.txt", &verify, errMsg));
		TEST_THAT(pconfig.get() != 0);
		TEST_THAT(errMsg.empty());
		TEST_THAT(pconfig->GetKeyValueBool("BoolTrue1") == true);
		TEST_THAT(pconfig->GetKeyValueBool("BoolTrue2") == true);
		TEST_THAT(pconfig->GetKeyValueBool("BoolFalse1") == false);
		TEST_THAT(pconfig->GetKeyValueBool("BoolFalse2") == false);
	}
	
	// Test named locks
	{
		NamedLock lock1;
		// Try and get a lock on a name in a directory which doesn't exist
		TEST_CHECK_THROWS(lock1.TryAndGetLock("testfiles/non-exist/lock"), CommonException, OSFileError);
		// And a more resonable request
		TEST_THAT(lock1.TryAndGetLock("testfiles/lock1") == true);
		// Try to lock something using the same lock
		TEST_CHECK_THROWS(lock1.TryAndGetLock("testfiles/non-exist/lock2"), CommonException, NamedLockAlreadyLockingSomething);		
#ifndef PLATFORM_open_USE_fcntl
		// And again on that name
		NamedLock lock2;
		TEST_THAT(lock2.TryAndGetLock("testfiles/lock1") == false);
#endif
	}
	{
		// Check that it unlocked when it went out of scope
		NamedLock lock3;
		TEST_THAT(lock3.TryAndGetLock("testfiles/lock1") == true);
	}
	{
		// And unlocking works
		NamedLock lock4;
		TEST_CHECK_THROWS(lock4.ReleaseLock(), CommonException, NamedLockNotHeld);		
		TEST_THAT(lock4.TryAndGetLock("testfiles/lock4") == true);
		lock4.ReleaseLock();
		NamedLock lock5;
		TEST_THAT(lock5.TryAndGetLock("testfiles/lock4") == true);
		// And can reuse it
		TEST_THAT(lock4.TryAndGetLock("testfiles/lock5") == true);
	}

	// Test the ReadGatherStream
	{
		#define GATHER_DATA1 "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
		#define GATHER_DATA2 "ZYZWVUTSRQPOMNOLKJIHGFEDCBA9876543210zyxwvutsrqpomno"

		// Make two streams
		MemBlockStream s1(GATHER_DATA1, sizeof(GATHER_DATA1));
		MemBlockStream s2(GATHER_DATA2, sizeof(GATHER_DATA2));

		// And a gather stream
		ReadGatherStream gather(false /* no deletion */);
		
		// Add the streams
		int s1_c = gather.AddComponent(&s1);
		int s2_c = gather.AddComponent(&s2);
		TEST_THAT(s1_c == 0);
		TEST_THAT(s2_c == 1);
		
		// Set up some blocks
		gather.AddBlock(s1_c, 11);
		gather.AddBlock(s1_c, 2);
		gather.AddBlock(s1_c, 8, true, 2);
		gather.AddBlock(s2_c, 20);
		gather.AddBlock(s1_c, 20);
		gather.AddBlock(s2_c, 25);
		gather.AddBlock(s1_c, 10, true, 0);
		#define GATHER_RESULT "0123456789abc23456789ZYZWVUTSRQPOMNOLKJIHabcdefghijklmnopqrstGFEDCBA9876543210zyxwvuts0123456789"

		// Read them in...
		char buffer[1024];
		unsigned int r = 0;
		while(r < sizeof(GATHER_RESULT) - 1)
		{
			int s = gather.Read(buffer + r, 7);
			r += s;
			
			TEST_THAT(gather.GetPosition() == r);
			if(r < sizeof(GATHER_RESULT) - 1)
			{
				TEST_THAT(gather.StreamDataLeft());
				TEST_THAT(static_cast<size_t>(gather.BytesLeftToRead()) == sizeof(GATHER_RESULT) - 1 - r);
			}
			else
			{
				TEST_THAT(!gather.StreamDataLeft());
				TEST_THAT(gather.BytesLeftToRead() == 0);
			}
		}
		TEST_THAT(r == sizeof(GATHER_RESULT) - 1);
		TEST_THAT(::memcmp(buffer, GATHER_RESULT, sizeof(GATHER_RESULT) - 1) == 0);
	}
	
	// Test ExcludeList
	{
		ExcludeList elist;
		// Check assumption
		TEST_THAT(Configuration::MultiValueSeparator == '\x01');
		// Add definite entries
		elist.AddDefiniteEntries(std::string("\x01"));
		elist.AddDefiniteEntries(std::string(""));
		elist.AddDefiniteEntries(std::string("Definite1\x01/dir/DefNumberTwo\x01\x01ThingDefThree"));
		elist.AddDefiniteEntries(std::string("AnotherDef"));
		TEST_THAT(elist.SizeOfDefiniteList() == 4);

		// Add regex entries
		#ifndef PLATFORM_REGEX_NOT_SUPPORTED
			elist.AddRegexEntries(std::string("[a-d]+\\.reg$" "\x01" "EXCLUDE" "\x01" "^exclude$"));
			elist.AddRegexEntries(std::string(""));
			TEST_CHECK_THROWS(elist.AddRegexEntries(std::string("[:not_valid")), CommonException, BadRegularExpression);
			TEST_THAT(elist.SizeOfRegexList() == 3);
		#else
			TEST_CHECK_THROWS(elist.AddRegexEntries(std::string("[a-d]+\\.reg$" "\x01" "EXCLUDE" "\x01" "^exclude$")), CommonException, RegexNotSupportedOnThisPlatform);
			TEST_THAT(elist.SizeOfRegexList() == 0);
		#endif
		// Try some matches!
		TEST_THAT(elist.IsExcluded(std::string("Definite1")) == true);
		TEST_THAT(elist.IsExcluded(std::string("/dir/DefNumberTwo")) == true);
		TEST_THAT(elist.IsExcluded(std::string("ThingDefThree")) == true);
		TEST_THAT(elist.IsExcluded(std::string("AnotherDef")) == true);
		TEST_THAT(elist.IsExcluded(std::string("dir/DefNumberTwo")) == false);
		#ifndef PLATFORM_REGEX_NOT_SUPPORTED
			TEST_THAT(elist.IsExcluded(std::string("b.reg")) == true);
			TEST_THAT(elist.IsExcluded(std::string("e.reg")) == false);
			TEST_THAT(elist.IsExcluded(std::string("b.Reg")) == false);
			TEST_THAT(elist.IsExcluded(std::string("DEfinite1")) == false);
			TEST_THAT(elist.IsExcluded(std::string("DEXCLUDEfinite1")) == true);
			TEST_THAT(elist.IsExcluded(std::string("DEfinitexclude1")) == false);
			TEST_THAT(elist.IsExcluded(std::string("exclude")) == true);
		#endif
	}

	test_conversions();

	return 0;
}
