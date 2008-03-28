// --------------------------------------------------------------------------
//
// File
//		Name:    testcommon.cpp
//		Purpose: Tests for the code in lib/common
//		Created: 2003/07/23
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <errno.h>
#include <stdio.h>
#include <time.h>

#include "Test.h"
#include "Configuration.h"
#include "FdGetLine.h"
#include "Guards.h"
#include "FileStream.h"
#include "InvisibleTempFileStream.h"
#include "IOStreamGetLine.h"
#include "NamedLock.h"
#include "ReadGatherStream.h"
#include "MemBlockStream.h"
#include "ExcludeList.h"
#include "CommonException.h"
#include "Conversion.h"
#include "autogen_ConversionException.h"
#include "CollectInBufferStream.h"
#include "Archive.h"
#include "Timer.h"
#include "Logging.h"
#include "ZeroStream.h"
#include "PartialReadStream.h"

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
	ConfigurationVerifyKey("bing", ConfigTest_Exists),
	ConfigurationVerifyKey("carrots", ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("terrible", ConfigTest_Exists | ConfigTest_LastEntry)
};

ConfigurationVerifyKey verifykeys1_1_2[] =
{
	ConfigurationVerifyKey("fish", ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("string", ConfigTest_Exists | ConfigTest_LastEntry)
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
	ConfigurationVerifyKey("value", ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("string1", ConfigTest_Exists),
	ConfigurationVerifyKey("string2", ConfigTest_Exists | ConfigTest_LastEntry)
};

ConfigurationVerifyKey verifykeys1_2[] = 
{
	ConfigurationVerifyKey("carrots", ConfigTest_Exists | ConfigTest_IsInt),
	ConfigurationVerifyKey("string", ConfigTest_Exists | ConfigTest_LastEntry)
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
		ConfigurationVerifyKey("notExpected", 0),
		ConfigurationVerifyKey("HasDefaultValue", 0, "Lovely default value"),
		ConfigurationVerifyKey("MultiValue", ConfigTest_MultiValueAllowed),
		ConfigurationVerifyKey("BoolTrue1", ConfigTest_IsBool),
		ConfigurationVerifyKey("BoolTrue2", ConfigTest_IsBool),
		ConfigurationVerifyKey("BoolFalse1", ConfigTest_IsBool),
		ConfigurationVerifyKey("BoolFalse2", ConfigTest_IsBool),
		ConfigurationVerifyKey("TOPlevel", 
			ConfigTest_LastEntry | ConfigTest_Exists)
};

ConfigurationVerify verify =
{
	"root",
	verifysub1,
	verifykeys1,
	ConfigTest_Exists | ConfigTest_LastEntry,
	0
};

class TestLogger : public Logger
{
	private:
	bool mTriggered;
	Log::Level mTargetLevel;

	public:
	TestLogger(Log::Level targetLevel) 
	: mTriggered(false), mTargetLevel(targetLevel)
	{ 
		Logging::Add(this);
	}
	~TestLogger() 
	{
		Logging::Remove(this);
	}

	bool IsTriggered() { return mTriggered; }
	void Reset()       { mTriggered = false; }

	virtual bool Log(Log::Level level, const std::string& rFile,
		int line, std::string& rMessage)
	{
		if (level == mTargetLevel)
		{
			mTriggered = true;
		}
		return true;
	}

	virtual const char* GetType() { return "Test"; }
	virtual void SetProgramName(const std::string& rProgramName) { }
};

int test(int argc, const char *argv[])
{
	// Test PartialReadStream and ReadGatherStream handling of files
	// over 2GB (refs #2)
	{
		char buffer[8];

		ZeroStream zero(0x80000003);
		zero.Seek(0x7ffffffe, IOStream::SeekType_Absolute);
		TEST_THAT(zero.GetPosition() == 0x7ffffffe);
		TEST_THAT(zero.Read(buffer, 8) == 5);
		TEST_THAT(zero.GetPosition() == 0x80000003);
		TEST_THAT(zero.Read(buffer, 8) == 0);
		zero.Seek(0, IOStream::SeekType_Absolute);
		TEST_THAT(zero.GetPosition() == 0);

		char* buffer2 = new char [0x1000000];
		TEST_THAT(buffer2 != NULL);

		PartialReadStream part(zero, 0x80000002);
		for (int i = 0; i < 0x80; i++)
		{
			int read = part.Read(buffer2, 0x1000000);
			TEST_THAT(read == 0x1000000);
		}
		TEST_THAT(part.Read(buffer, 8) == 2);
		TEST_THAT(part.Read(buffer, 8) == 0);

		delete [] buffer2;

		ReadGatherStream gather(false);
		zero.Seek(0, IOStream::SeekType_Absolute);
		int component = gather.AddComponent(&zero);
		gather.AddBlock(component, 0x80000002);
		TEST_THAT(gather.Read(buffer, 8) == 8);
	}

	// Test self-deleting temporary file streams
	{
		std::string tempfile("testfiles/tempfile");
		TEST_CHECK_THROWS(InvisibleTempFileStream fs(tempfile.c_str()), 
			CommonException, OSFileOpenError);
		InvisibleTempFileStream fs(tempfile.c_str(), O_CREAT);

	#ifdef WIN32
		// file is still visible under Windows
		TEST_THAT(TestFileExists(tempfile.c_str()));

		// opening it again should work
		InvisibleTempFileStream fs2(tempfile.c_str());
		TEST_THAT(TestFileExists(tempfile.c_str()));

		// opening it to create should work
		InvisibleTempFileStream fs3(tempfile.c_str(), O_CREAT);
		TEST_THAT(TestFileExists(tempfile.c_str()));

		// opening it to create exclusively should fail
		TEST_CHECK_THROWS(InvisibleTempFileStream fs4(tempfile.c_str(), 
			O_CREAT | O_EXCL), CommonException, OSFileOpenError);

		fs2.Close();
	#else
		// file is not visible under Unix
		TEST_THAT(!TestFileExists(tempfile.c_str()));

		// opening it again should fail
		TEST_CHECK_THROWS(InvisibleTempFileStream fs2(tempfile.c_str()),
			CommonException, OSFileOpenError);

		// opening it to create should work
		InvisibleTempFileStream fs3(tempfile.c_str(), O_CREAT);
		TEST_THAT(!TestFileExists(tempfile.c_str()));

		// opening it to create exclusively should work
		InvisibleTempFileStream fs4(tempfile.c_str(), O_CREAT | O_EXCL);
		TEST_THAT(!TestFileExists(tempfile.c_str()));

		fs4.Close();
	#endif

		fs.Close();
		fs3.Close();

		// now that it's closed, it should be invisible on all platforms
		TEST_THAT(!TestFileExists(tempfile.c_str()));
	}

	// Test that memory leak detection doesn't crash
	{
		char *test = new char[1024];
		delete [] test;
		MemBlockStream *s = new MemBlockStream(test,12);
		delete s;
	}

#ifdef BOX_MEMORY_LEAK_TESTING
	{
		Timers::Cleanup();

		TEST_THAT(memleakfinder_numleaks() == 0);
		void *block = ::malloc(12);
		TEST_THAT(memleakfinder_numleaks() == 1);
		void *b2 = ::realloc(block, 128*1024);
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

		Timers::Init();
	}
#endif // BOX_MEMORY_LEAK_TESTING

	// test main() initialises timers for us, so uninitialise them
	Timers::Cleanup();
	
	// Check that using timer methods without initialisation
	// throws an assertion failure. Can only do this in debug mode
	#ifndef NDEBUG
		TEST_CHECK_THROWS(Timers::Add(*(Timer*)NULL), 
			CommonException, AssertFailed);
		TEST_CHECK_THROWS(Timers::Remove(*(Timer*)NULL), 
			CommonException, AssertFailed);
	#endif

	// TEST_CHECK_THROWS(Timers::Signal(), CommonException, AssertFailed);
	#ifndef NDEBUG
		TEST_CHECK_THROWS(Timers::Cleanup(), CommonException,
			AssertFailed);
	#endif
	
	// Check that we can initialise the timers
	Timers::Init();
	
	// Check that double initialisation throws an exception
	#ifndef NDEBUG
		TEST_CHECK_THROWS(Timers::Init(), CommonException,
			AssertFailed);
	#endif

	// Check that we can clean up the timers
	Timers::Cleanup();
	
	// Check that double cleanup throws an exception
	#ifndef NDEBUG
		TEST_CHECK_THROWS(Timers::Cleanup(), CommonException,
			AssertFailed);
	#endif

	Timers::Init();

	Timer t0(0); // should never expire
	Timer t1(1);
	Timer t2(2);
	Timer t3(3);
	
	TEST_THAT(!t0.HasExpired());
	TEST_THAT(!t1.HasExpired());
	TEST_THAT(!t2.HasExpired());
	TEST_THAT(!t3.HasExpired());
	
	safe_sleep(1);
	TEST_THAT(!t0.HasExpired());
	TEST_THAT(t1.HasExpired());
	TEST_THAT(!t2.HasExpired());
	TEST_THAT(!t3.HasExpired());
	
	safe_sleep(1);
	TEST_THAT(!t0.HasExpired());
	TEST_THAT(t1.HasExpired());
	TEST_THAT(t2.HasExpired());
	TEST_THAT(!t3.HasExpired());
	
	t1 = Timer(1);
	t2 = Timer(2);
	TEST_THAT(!t0.HasExpired());
	TEST_THAT(!t1.HasExpired());
	TEST_THAT(!t2.HasExpired());
	
	safe_sleep(1);
	TEST_THAT(!t0.HasExpired());
	TEST_THAT(t1.HasExpired());
	TEST_THAT(!t2.HasExpired());
	TEST_THAT(t3.HasExpired());

	// Leave timers initialised for rest of test.
	// Test main() will cleanup after test finishes.

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
		FileHandleGuard<O_RDONLY> file("testfiles"
			DIRECTORY_SEPARATOR "fdgetlinetest.txt");
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
		FileHandleGuard<O_RDONLY> file("testfiles"
			DIRECTORY_SEPARATOR "fdgetlinetest.txt");
		FILE *file2 = fopen("testfiles" DIRECTORY_SEPARATOR 
			"fdgetlinetest.txt", "r");
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
		FileStream file("testfiles" DIRECTORY_SEPARATOR 
			"fdgetlinetest.txt", O_RDONLY);
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
		FileStream file("testfiles" DIRECTORY_SEPARATOR 
			"fdgetlinetest.txt", O_RDONLY);
		IOStreamGetLine getline(file);

		FILE *file2 = fopen("testfiles" DIRECTORY_SEPARATOR 
			"fdgetlinetest.txt", "r");
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
		TEST_CHECK_THROWS(std::auto_ptr<Configuration> pconfig(
			Configuration::LoadAndVerify(
				"testfiles" DIRECTORY_SEPARATOR "DOESNTEXIST", 
				&verify, errMsg)), 
			CommonException, OSFileOpenError);
	}

	// Basic configuration test
	{
		std::string errMsg;
		std::auto_ptr<Configuration> pconfig(
			Configuration::LoadAndVerify(
				"testfiles" DIRECTORY_SEPARATOR "config1.txt", 
				&verify, errMsg));
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
		"testfiles" DIRECTORY_SEPARATOR "config2.txt", 
			// Value missing from root
		"testfiles" DIRECTORY_SEPARATOR "config3.txt", 
			// Unexpected {
		"testfiles" DIRECTORY_SEPARATOR "config4.txt", 
			// Missing }
		"testfiles" DIRECTORY_SEPARATOR "config5.txt", 
			// { expected, but wasn't there
		"testfiles" DIRECTORY_SEPARATOR "config6.txt", 
			// Duplicate key
		"testfiles" DIRECTORY_SEPARATOR "config7.txt", 
			// Invalid key (no name)
		"testfiles" DIRECTORY_SEPARATOR "config8.txt", 
			// Not all sub blocks terminated
		"testfiles" DIRECTORY_SEPARATOR "config9.txt", 
			// Not valid integer
		"testfiles" DIRECTORY_SEPARATOR "config9b.txt", 
			// Not valid integer
		"testfiles" DIRECTORY_SEPARATOR "config9c.txt", 
			// Not valid integer
		"testfiles" DIRECTORY_SEPARATOR "config9d.txt", 
			// Not valid integer
		"testfiles" DIRECTORY_SEPARATOR "config10.txt", 
			// Missing key (in subblock)
		"testfiles" DIRECTORY_SEPARATOR "config11.txt", 
			// Unknown key
		"testfiles" DIRECTORY_SEPARATOR "config12.txt", 
			// Missing block
		"testfiles" DIRECTORY_SEPARATOR "config13.txt", 
			// Subconfig (wildcarded) should exist, but missing (ie nothing present)
		"testfiles" DIRECTORY_SEPARATOR "config16.txt", 
			// bad boolean value
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
		std::auto_ptr<Configuration> pconfig(
			Configuration::LoadAndVerify(
				"testfiles" DIRECTORY_SEPARATOR "config14.txt",
			&verify, errMsg));
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
		std::auto_ptr<Configuration> pconfig(
			Configuration::LoadAndVerify(
				"testfiles" DIRECTORY_SEPARATOR "config15.txt",
			&verify, errMsg));
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
		TEST_CHECK_THROWS(lock1.TryAndGetLock(
				"testfiles" 
				DIRECTORY_SEPARATOR "non-exist"
				DIRECTORY_SEPARATOR "lock"), 
			CommonException, OSFileError);

		// And a more resonable request
		TEST_THAT(lock1.TryAndGetLock(
			"testfiles" DIRECTORY_SEPARATOR "lock1") == true);

		// Try to lock something using the same lock
		TEST_CHECK_THROWS(
			lock1.TryAndGetLock(
				"testfiles" 
				DIRECTORY_SEPARATOR "non-exist"
				DIRECTORY_SEPARATOR "lock2"), 
			CommonException, NamedLockAlreadyLockingSomething);		
#if defined(HAVE_FLOCK) || HAVE_DECL_O_EXLOCK
		// And again on that name
		NamedLock lock2;
		TEST_THAT(lock2.TryAndGetLock(
			"testfiles" DIRECTORY_SEPARATOR "lock1") == false);
#endif
	}
	{
		// Check that it unlocked when it went out of scope
		NamedLock lock3;
		TEST_THAT(lock3.TryAndGetLock(
			"testfiles" DIRECTORY_SEPARATOR "lock1") == true);
	}
	{
		// And unlocking works
		NamedLock lock4;
		TEST_CHECK_THROWS(lock4.ReleaseLock(), CommonException, 
			NamedLockNotHeld);
		TEST_THAT(lock4.TryAndGetLock(
			"testfiles" DIRECTORY_SEPARATOR "lock4") == true);
		lock4.ReleaseLock();
		NamedLock lock5;
		TEST_THAT(lock5.TryAndGetLock(
			"testfiles" DIRECTORY_SEPARATOR "lock4") == true);
		// And can reuse it
		TEST_THAT(lock4.TryAndGetLock(
			"testfiles" DIRECTORY_SEPARATOR "lock5") == true);
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
		#ifdef HAVE_REGEX_SUPPORT
			elist.AddRegexEntries(std::string("[a-d]+\\.reg$" "\x01" "EXCLUDE" "\x01" "^exclude$"));
			elist.AddRegexEntries(std::string(""));
			TEST_CHECK_THROWS(elist.AddRegexEntries(std::string("[:not_valid")), CommonException, BadRegularExpression);
			TEST_THAT(elist.SizeOfRegexList() == 3);
		#else
			TEST_CHECK_THROWS(elist.AddRegexEntries(std::string("[a-d]+\\.reg$" "\x01" "EXCLUDE" "\x01" "^exclude$")), CommonException, RegexNotSupportedOnThisPlatform);
			TEST_THAT(elist.SizeOfRegexList() == 0);
		#endif

		#ifdef WIN32
		#define CASE_SENSITIVE false
		#else
		#define CASE_SENSITIVE true
		#endif

		// Try some matches!
		TEST_THAT(elist.IsExcluded(std::string("Definite1")) == true);
		TEST_THAT(elist.IsExcluded(std::string("/dir/DefNumberTwo")) == true);
		TEST_THAT(elist.IsExcluded(std::string("ThingDefThree")) == true);
		TEST_THAT(elist.IsExcluded(std::string("AnotherDef")) == true);
		TEST_THAT(elist.IsExcluded(std::string("dir/DefNumberTwo")) == false);

		// Try some case insensitive matches,
		// that should pass on Win32 and fail elsewhere
		TEST_THAT(elist.IsExcluded("DEFINITe1") 
			== !CASE_SENSITIVE);
		TEST_THAT(elist.IsExcluded("/Dir/DefNumberTwo") 
			== !CASE_SENSITIVE);
		TEST_THAT(elist.IsExcluded("thingdefthree") 
			== !CASE_SENSITIVE);

		#ifdef HAVE_REGEX_SUPPORT
			TEST_THAT(elist.IsExcluded(std::string("b.reg")) == true);
			TEST_THAT(elist.IsExcluded(std::string("B.reg")) == !CASE_SENSITIVE);
			TEST_THAT(elist.IsExcluded(std::string("b.Reg")) == !CASE_SENSITIVE);
			TEST_THAT(elist.IsExcluded(std::string("e.reg")) == false);
			TEST_THAT(elist.IsExcluded(std::string("e.Reg")) == false);
			TEST_THAT(elist.IsExcluded(std::string("DEfinite1")) == !CASE_SENSITIVE);
			TEST_THAT(elist.IsExcluded(std::string("DEXCLUDEfinite1")) == true);
			TEST_THAT(elist.IsExcluded(std::string("DEfinitexclude1")) == !CASE_SENSITIVE);
			TEST_THAT(elist.IsExcluded(std::string("exclude")) == true);
			TEST_THAT(elist.IsExcluded(std::string("ExcludE")) == !CASE_SENSITIVE);
		#endif

		#undef CASE_SENSITIVE

		TestLogger logger(Log::WARNING);
		TEST_THAT(!logger.IsTriggered());
		elist.AddDefiniteEntries(std::string("/foo"));
		TEST_THAT(!logger.IsTriggered());
		elist.AddDefiniteEntries(std::string("/foo/"));
		TEST_THAT(logger.IsTriggered());
		logger.Reset();
		elist.AddDefiniteEntries(std::string("/foo" 
			DIRECTORY_SEPARATOR));
		TEST_THAT(logger.IsTriggered());
		logger.Reset();
		elist.AddDefiniteEntries(std::string("/foo" 
			DIRECTORY_SEPARATOR "bar\x01/foo"));
		TEST_THAT(!logger.IsTriggered());
		elist.AddDefiniteEntries(std::string("/foo" 
			DIRECTORY_SEPARATOR "bar\x01/foo" 
			DIRECTORY_SEPARATOR));
		TEST_THAT(logger.IsTriggered());
	}

	test_conversions();

	// test that we can use Archive and CollectInBufferStream
	// to read and write arbitrary types to a memory buffer

	{
		CollectInBufferStream buffer;
		ASSERT(buffer.GetPosition() == 0);

		{
			Archive archive(buffer, 0);
			ASSERT(buffer.GetPosition() == 0);

			archive.Write((bool) true);
			archive.Write((bool) false);
			archive.Write((int) 0x12345678);
			archive.Write((int) 0x87654321);
			archive.Write((int64_t)  0x0badfeedcafebabeLL);
			archive.Write((uint64_t) 0xfeedfacedeadf00dLL);
			archive.Write((uint8_t)  0x01);
			archive.Write((uint8_t)  0xfe);
			archive.Write(std::string("hello world!"));
			archive.Write(std::string("goodbye cruel world!"));
		}

		CollectInBufferStream buf2;
		buf2.Write(buffer.GetBuffer(), buffer.GetSize());
		TEST_THAT(buf2.GetPosition() == buffer.GetSize());

		buf2.SetForReading();
		TEST_THAT(buf2.GetPosition() == 0);

		{
			Archive archive(buf2, 0);
			TEST_THAT(buf2.GetPosition() == 0);

			bool b;
			archive.Read(b); TEST_THAT(b == true);
			archive.Read(b); TEST_THAT(b == false);

			int i;
			archive.Read(i); TEST_THAT(i == 0x12345678);
			archive.Read(i); TEST_THAT((unsigned int)i == 0x87654321);

			uint64_t i64;
			archive.Read(i64); TEST_THAT(i64 == 0x0badfeedcafebabeLL);
			archive.Read(i64); TEST_THAT(i64 == 0xfeedfacedeadf00dLL);

			uint8_t i8;
			archive.Read(i8); TEST_THAT(i8 == 0x01);
			archive.Read(i8); TEST_THAT(i8 == 0xfe);

			std::string s;
			archive.Read(s); TEST_THAT(s == "hello world!");
			archive.Read(s); TEST_THAT(s == "goodbye cruel world!");

			TEST_THAT(!buf2.StreamDataLeft());
		}
	}

	return 0;
}
