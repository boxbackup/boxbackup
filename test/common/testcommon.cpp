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

#include "Archive.h"
#include "CollectInBufferStream.h"
#include "CommonException.h"
#include "Configuration.h"
#include "Conversion.h"
#include "ExcludeList.h"
#include "FdGetLine.h"
#include "FileStream.h"
#include "Guards.h"
#include "InvisibleTempFileStream.h"
#include "IOStreamGetLine.h"
#include "Logging.h"
#include "MemBlockStream.h"
#include "NamedLock.h"
#include "PartialReadStream.h"
#include "ReadGatherStream.h"
#include "Test.h"
#include "Timer.h"
#include "ZeroStream.h"
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

	virtual bool Log(Log::Level level, const std::string& file, int line,
		const std::string& function, const Log::Category& category,
		const std::string& message)
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

// Test PartialReadStream and ReadGatherStream handling of files over 2GB (refs #2)
void test_stream_large_files()
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
void test_invisible_temp_file_stream()
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

// Test that named locks work as expected
void test_named_locks()
{
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

		// And again on that name
		NamedLock lock2;
		TEST_THAT(lock2.TryAndGetLock(
			"testfiles" DIRECTORY_SEPARATOR "lock1") == false);
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
}

void test_memory_leak_detection()
{
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

		TEST_EQUAL(0, memleakfinder_numleaks());
		void *block = ::malloc(12);
		TEST_EQUAL(1, memleakfinder_numleaks());
		void *b2 = ::realloc(block, 128*1024);
		TEST_EQUAL(1, memleakfinder_numleaks());
		::free(b2);
		TEST_EQUAL(0, memleakfinder_numleaks());
		char *test = new char[1024];
		TEST_EQUAL(1, memleakfinder_numleaks());
		MemBlockStream *s = new MemBlockStream(test,12);
		TEST_EQUAL(3, memleakfinder_numleaks());
		delete s;
		TEST_EQUAL(1, memleakfinder_numleaks());
		delete [] test;
		TEST_EQUAL(0, memleakfinder_numleaks());

		Timers::Init();
	}
#endif // BOX_MEMORY_LEAK_TESTING
}

void test_timers()
{
	// test main() initialises timers for us, so uninitialise them
	Timers::Cleanup();

	// Check that using timer methods without initialisation
	// throws an assertion failure. Can only do this in debug mode
	#ifndef BOX_RELEASE_BUILD
		TEST_CHECK_THROWS(Timers::Add(*(Timer*)NULL),
			CommonException, AssertFailed);
		TEST_CHECK_THROWS(Timers::Remove(*(Timer*)NULL),
			CommonException, AssertFailed);
	#endif

	// TEST_CHECK_THROWS(Timers::Signal(), CommonException, AssertFailed);
	#ifndef BOX_RELEASE_BUILD
		TEST_CHECK_THROWS(Timers::Cleanup(), CommonException,
			AssertFailed);
	#endif

	// Check that we can initialise the timers
	Timers::Init();

	// Check that double initialisation throws an exception
	#ifndef BOX_RELEASE_BUILD
		TEST_CHECK_THROWS(Timers::Init(), CommonException,
			AssertFailed);
	#endif

	// Check that we can clean up the timers
	Timers::Cleanup();

	// Check that double cleanup throws an exception
	#ifndef BOX_RELEASE_BUILD
		TEST_CHECK_THROWS(Timers::Cleanup(), CommonException,
			AssertFailed);
	#endif

	Timers::Init();

	// Ideally timers would be perfectly accurate and we could sleep for 1.0 seconds, but
	// on OSX in particular they could fire 50-100 ms late (I've seen 4 ms in practice)
	// and we don't want the tests to fail because of this, because we don't really need
	// that kind of precision in practice. So we reduce the timer intervals by 100ms to
	// be safe.

	{
		Logger::LevelGuard temporary_verbosity(Logging::GetConsole(), Log::TRACE);
		Console::SettingsGuard save_old_settings;
		Console::SetShowTime(true);
		Console::SetShowTimeMicros(true);

		Timer t0(0, "t0"); // should never expire
		Timer t1(900, "t1");
		Timer t2(1900, "t2");
		Timer t3(2900, "t3");

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

		// Try both ways of resetting an existing timer.
		t1 = Timer(900, "t1a");
		t2.Reset(1900);
		TEST_THAT(!t0.HasExpired());
		TEST_THAT(!t1.HasExpired());
		TEST_THAT(!t2.HasExpired());
		TEST_THAT(!t3.HasExpired());

		safe_sleep(1);
		TEST_THAT(!t0.HasExpired());
		TEST_THAT(t1.HasExpired());
		TEST_THAT(!t2.HasExpired());
		TEST_THAT(t3.HasExpired());

		safe_sleep(1);
		TEST_THAT(!t0.HasExpired());
		TEST_THAT(t1.HasExpired());
		TEST_THAT(t2.HasExpired());
		TEST_THAT(t3.HasExpired());
	}

	// Leave timers initialised for rest of test.
	// Test main() will cleanup after test finishes.
}

void test_getline()
{
	static const char *testfilelines[] =
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
			TEST_EQUAL(testfilelines[l], line);
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
		TEST_THAT(file2 != 0);
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
			TEST_EQUAL(ll, line);
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
			{
				// skip line
			}
			TEST_EQUAL(testfilelines[l], line);
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
		TEST_THAT(file2 != 0);
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
			TEST_EQUAL(ll, line);
		}
		TEST_THAT(getline.IsEOF());
		std::string dummy;
		TEST_CHECK_THROWS(getline.GetLine(dummy, true), CommonException, GetLineEOF);

		fclose(file2);
	}
}

void test_configuration()
{
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

		TEST_THAT(pconfig.get() != 0);
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

	static const char *file[][2] =
	{
		{"testfiles" DIRECTORY_SEPARATOR "config2.txt",
			"<root>.TOPlevel (key) is missing."},
			// Value missing from root
		{"testfiles" DIRECTORY_SEPARATOR "config3.txt",
			"Unexpected start block in test1"},
			// Unexpected {
		{"testfiles" DIRECTORY_SEPARATOR "config4.txt",
			"Root level has close block -- forgot to terminate subblock?"},
			// Missing }
		{"testfiles" DIRECTORY_SEPARATOR "config5.txt",
			"Block subconfig2 wasn't started correctly (no '{' on line of it's own)\n"
			"Root level has close block -- forgot to terminate subblock?"},
			// { expected, but wasn't there
		{"testfiles" DIRECTORY_SEPARATOR "config6.txt",
			"test1.subconfig2.bing (key) multi value not allowed (duplicated key?)."},
			// Duplicate key
		{"testfiles" DIRECTORY_SEPARATOR "config7.txt",
			"Invalid configuration key: = invalid thing here!"},
			// Invalid key (no name)
		{"testfiles" DIRECTORY_SEPARATOR "config8.txt",
			"File ended without terminating all subblocks"},
			// Not all sub blocks terminated
		{"testfiles" DIRECTORY_SEPARATOR "config9.txt",
			"test1.subconfig3.carrots (key) is not a valid integer."},
			// Not valid integer
		{"testfiles" DIRECTORY_SEPARATOR "config9b.txt",
			"test1.subconfig2.carrots (key) is not a valid integer."},
			// Not valid integer
		{"testfiles" DIRECTORY_SEPARATOR "config9c.txt",
			"test1.subconfig2.carrots (key) is not a valid integer."},
			// Not valid integer
		{"testfiles" DIRECTORY_SEPARATOR "config9d.txt",
			"test1.subconfig3.carrots (key) is not a valid integer."},
			// Not valid integer
		{"testfiles" DIRECTORY_SEPARATOR "config10.txt",
			"test1.subconfig.carrots (key) is missing."},
			// Missing key (in subblock)
		{"testfiles" DIRECTORY_SEPARATOR "config11.txt",
			"test1.subconfig3.NOTEXPECTED (key) is not a known key. Check spelling and placement."},
			// Unknown key
		{"testfiles" DIRECTORY_SEPARATOR "config12.txt",
			"<root>.test1.otherthing (block) is missing."},
			// Missing block
		{"testfiles" DIRECTORY_SEPARATOR "config13.txt",
			"<root>.test1.* (block) is missing (a block must be present).\n"
			"<root>.test1.otherthing (block) is missing."},
			// Subconfig (wildcarded) should exist, but missing (ie nothing present)
		{"testfiles" DIRECTORY_SEPARATOR "config16.txt",
			"<root>.BoolTrue1 (key) is not a valid boolean value."},
			// bad boolean value
		{NULL, NULL},
	};

	for(int l = 0; file[l][0] != 0; ++l)
	{
		HideCategoryGuard hide(ConfigurationVerify::VERIFY_ERROR);
		std::string errMsg;
		std::auto_ptr<Configuration> pconfig(Configuration::LoadAndVerify(file[l][0], &verify, errMsg));
		TEST_THAT(pconfig.get() == 0);
		errMsg = errMsg.substr(0, errMsg.size() > 0 ? errMsg.size() - 1 : 0);
		TEST_EQUAL_LINE(file[l][1], errMsg, file[l][0]);
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
}

// Test the ReadGatherStream
void test_read_gather_stream()
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
void test_exclude_list()
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
	{
		HideCategoryGuard hide(ConfigurationVerify::VERIFY_ERROR);
		elist.AddRegexEntries(std::string("[a-d]+\\.reg$" "\x01" "EXCLUDE" "\x01" "^exclude$"));
		elist.AddRegexEntries(std::string(""));
		TEST_CHECK_THROWS(elist.AddRegexEntries(std::string("[:not_valid")), CommonException, BadRegularExpression);
		TEST_THAT(elist.SizeOfRegexList() == 3);
	}
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

// Test that we can use Archive and CollectInBufferStream
// to read and write arbitrary types to a memory buffer
void test_archive()
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

// Test that box_strtoui64 works properly
void test_box_strtoui64()
{
	TEST_EQUAL(1234567890123456, box_strtoui64("1234567890123456", NULL, 10));
	TEST_EQUAL(0x1234567890123456, box_strtoui64("1234567890123456", NULL, 16));
	TEST_EQUAL(0xd9385a13c3842ba0, box_strtoui64("d9385a13c3842ba0", NULL, 16));
	const char *input = "12a34";
	const char *endptr;
	TEST_EQUAL(12, box_strtoui64(input, &endptr, 10));
	TEST_EQUAL(input + 2, endptr);
	TEST_EQUAL(0x12a34, box_strtoui64(input, &endptr, 16));
	TEST_EQUAL(input + 5, endptr);
}

// Test that RemovePrefix and RemoveSuffix work properly
void test_remove_prefix_suffix()
{
	TEST_EQUAL("food", RemovePrefix("", "food", false)); // !force
	TEST_EQUAL("food", RemoveSuffix("", "food", false)); // !force
	TEST_EQUAL("", RemovePrefix("d", "food", false)); // !force
	TEST_EQUAL("", RemoveSuffix("f", "food", false)); // !force
	TEST_EQUAL("", RemovePrefix("dz", "food", false)); // !force
	TEST_EQUAL("", RemoveSuffix("fz", "food", false)); // !force
	TEST_EQUAL("ood", RemovePrefix("f", "food", false)); // !force
	TEST_EQUAL("foo", RemoveSuffix("d", "food", false)); // !force
	TEST_EQUAL("od", RemovePrefix("fo", "food", false)); // !force
	TEST_EQUAL("fo", RemoveSuffix("od", "food", false)); // !force
	TEST_EQUAL("", RemovePrefix("food", "food", false)); // !force
	TEST_EQUAL("", RemoveSuffix("food", "food", false)); // !force
	TEST_EQUAL("", RemovePrefix("foodz", "food", false)); // !force
	TEST_EQUAL("", RemoveSuffix("foodz", "food", false)); // !force
	TEST_EQUAL("", RemoveSuffix("zfood", "food", false)); // !force

	TEST_EQUAL("food", RemovePrefix("", "food", true)); // force
	TEST_EQUAL("food", RemoveSuffix("", "food", true)); // force
	TEST_CHECK_THROWS(RemovePrefix("d", "food", true), CommonException, Internal); // force
	TEST_CHECK_THROWS(RemoveSuffix("f", "food", true), CommonException, Internal); // force
	TEST_CHECK_THROWS(RemovePrefix("dz", "food", true), CommonException, Internal); // force
	TEST_CHECK_THROWS(RemoveSuffix("fz", "food", true), CommonException, Internal); // force
	TEST_EQUAL("ood", RemovePrefix("f", "food", true)); // force
	TEST_EQUAL("foo", RemoveSuffix("d", "food", true)); // force
	TEST_EQUAL("od", RemovePrefix("fo", "food", true)); // force
	TEST_EQUAL("fo", RemoveSuffix("od", "food", true)); // force
	TEST_CHECK_THROWS(RemovePrefix("foodz", "food", true), CommonException, Internal); // force
	TEST_CHECK_THROWS(RemoveSuffix("foodz", "food", true), CommonException, Internal); // force
	TEST_CHECK_THROWS(RemoveSuffix("zfood", "food", true), CommonException, Internal); // force

	// Test that force defaults to true:
	TEST_CHECK_THROWS(RemovePrefix("d", "food"), CommonException, Internal);
}

int test(int argc, const char *argv[])
{
	test_stream_large_files();
	test_invisible_temp_file_stream();
	test_named_locks();
	test_memory_leak_detection();
	test_timers();
	test_getline();
	test_configuration();
	test_read_gather_stream();
	test_exclude_list();
	test_conversions();
	test_archive();
	test_box_strtoui64();
	test_remove_prefix_suffix();

	return 0;
}
