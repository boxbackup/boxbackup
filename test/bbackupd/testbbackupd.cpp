// --------------------------------------------------------------------------
//
// File
//		Name:    testbbackupd.cpp
//		Purpose: test backup daemon (and associated client bits)
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------

#include "Box.h"

// do not include MinGW's dirent.h on Win32,
// as we override some of it in lib/win32.

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifndef WIN32
	#include <dirent.h>
#endif

#ifdef WIN32
	#include <process.h>
#endif

#ifdef HAVE_PWD_H
	#include <pwd.h>
#endif

#ifdef HAVE_SIGNAL_H
	#include <signal.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_SYSCALL
	#include <sys/syscall.h>
#endif

#ifdef HAVE_SYS_WAIT_H
	#include <sys/wait.h>
#endif

#ifdef HAVE_SYS_XATTR_H
	#include <cerrno>
	#include <sys/xattr.h>
#endif

#include <algorithm>
#include <map>

#include <boost/scope_exit.hpp>

#include "BackupClientCryptoKeys.h"
#include "BackupClientContext.h"
#include "BackupClientFileAttributes.h"

#define BACKIPCLIENTINODETOIDMAP_IMPLEMENTATION
#include <depot.h>
#include "BackupClientInodeToIDMap.h"
#undef BACKIPCLIENTINODETOIDMAP_IMPLEMENTATION

#include "BackupClientRestore.h"
#include "BackupDaemon.h"
#include "BackupDaemonConfigVerify.h"
#include "BackupProtocol.h"
#include "BackupQueries.h"
#include "BackupStoreAccounts.h"
#include "BackupStoreConstants.h"
#include "BackupStoreContext.h"
#include "BackupStoreDaemon.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BackupStoreConfigVerify.h"
#include "BackupStoreFileEncodeStream.h"
#include "BoxPortsAndFiles.h"
#include "BoxTime.h"
#include "BoxTimeToUnix.h"
#include "ClientTestUtils.h"
#include "CollectInBufferStream.h"
#include "CommonException.h"
#include "Configuration.h"
#include "FileModificationTime.h"
#include "FileStream.h"
#include "IOStreamGetLine.h"
#include "LocalProcessStream.h"
#include "MemBlockStream.h"
#include "RaidFileController.h"
#include "S3Simulator.h"
#include "SSLLib.h"
#include "ServerControl.h"
#include "Socket.h"
#include "SocketStreamTLS.h"
#include "StoreTestUtils.h"
#include "TLSContext.h"
#include "Test.h"
#include "Timer.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

// ENOATTR may be defined in a separate header file which we may not have
#ifndef ENOATTR
#define ENOATTR ENODATA
#endif

// two cycles and a bit
#define TIME_TO_WAIT_FOR_BACKUP_OPERATION 12
#define SHORT_TIMEOUT 5000
#define BACKUP_ERROR_DELAY_SHORTENED 10

void wait_for_backup_operation(const char* message)
{
	wait_for_operation(TIME_TO_WAIT_FOR_BACKUP_OPERATION, message);
}

#ifdef HAVE_SYS_XATTR_H
bool readxattr_into_map(const char *filename, std::map<std::string,std::string> &rOutput)
{
	rOutput.clear();

	ssize_t xattrNamesBufferSize = llistxattr(filename, NULL, 0);
	if(xattrNamesBufferSize < 0)
	{
#if HAVE_DECL_ENOTSUP
		if(errno == ENOTSUP)
		{
			// Pretend that it worked, leaving an empty map, so
			// that the rest of the attribute comparison will
			// proceed as normal.
			return true;
		}
#endif // HAVE_DECL_ENOTSUP

		return false;
	}
	else if(xattrNamesBufferSize > 0)
	{
		// There is some data there to look at
		char *xattrNamesBuffer = (char*)malloc(xattrNamesBufferSize + 4);
		if(xattrNamesBuffer == NULL) return false;
		char *xattrDataBuffer = 0;
		int xattrDataBufferSize = 0;
		// note: will leak these buffers if a read error occurs. (test code, so doesn't matter)

		ssize_t ns = llistxattr(filename, xattrNamesBuffer, xattrNamesBufferSize);
		if(ns < 0)
		{
			return false;
		}
		else if(ns > 0)
		{
			// Read all the attribute values
			const char *xattrName = xattrNamesBuffer;
			while(xattrName < (xattrNamesBuffer + ns))
			{
				// Store size of name
				int xattrNameSize = strlen(xattrName);

				bool ok = true;

				ssize_t dataSize = lgetxattr(filename, xattrName, NULL, 0);
				if(dataSize < 0)
				{
					if(errno == ENOATTR)
					{
						// Deleted from under us
						ok = false;
					}
					else
					{
						return false;
					}
				}
				else if(dataSize == 0)
				{
					// something must have removed all the data from under us
					ok = false;
				}
				else
				{
					// Make sure there's enough space in the buffer to get the attribute
					if(xattrDataBuffer == 0)
					{
						xattrDataBuffer = (char*)malloc(dataSize + 4);
						xattrDataBufferSize = dataSize + 4;
					}
					else if(xattrDataBufferSize < (dataSize + 4))
					{
						char *resized = (char*)realloc(xattrDataBuffer, dataSize + 4);
						if(resized == NULL) return false;
						xattrDataBuffer = resized;
						xattrDataBufferSize = dataSize + 4;
					}
				}

				// Read the data!
				dataSize = 0;
				if(ok)
				{
					dataSize = lgetxattr(filename, xattrName, xattrDataBuffer,
						xattrDataBufferSize - 1 /*for terminator*/);
					if(dataSize < 0)
					{
						if(errno == ENOATTR)
						{
							// Deleted from under us
							ok = false;
						}
						else
						{
							return false;
						}
					}
					else if(dataSize == 0)
					{
						// something must have deleted this from under us
						ok = false;
					}
					else
					{
						// Terminate the data
						xattrDataBuffer[dataSize] = '\0';
					}
					// Got the data in the buffer
				}

				// Store in map
				if(ok)
				{
					rOutput[std::string(xattrName)] = std::string(xattrDataBuffer, dataSize);
				}

				// Next attribute
				xattrName += xattrNameSize + 1;
			}
		}

		if(xattrNamesBuffer != 0) ::free(xattrNamesBuffer);
		if(xattrDataBuffer != 0) ::free(xattrDataBuffer);
	}

	return true;
}

static FILE *xattrTestDataHandle = 0;
bool write_xattr_test(const char *filename, const char *attrName, unsigned int length, bool *pNotSupported = 0)
{
	if(xattrTestDataHandle == 0)
	{
		xattrTestDataHandle = ::fopen("testfiles/test3.tgz", "rb");	// largest test file
	}
	if(xattrTestDataHandle == 0)
	{
		return false;
	}
	else
	{
		char data[1024];
		if(length > sizeof(data)) length = sizeof(data);

		if(::fread(data, length, 1, xattrTestDataHandle) != 1)
		{
			return false;
		}

		if(::lsetxattr(filename, attrName, data, length, 0) != 0)
		{
			if(pNotSupported != 0)
			{
				*pNotSupported = (errno == ENOTSUP);
			}
			return false;
		}
	}

	return true;
}
void finish_with_write_xattr_test()
{
	if(xattrTestDataHandle != 0)
	{
		::fclose(xattrTestDataHandle);
	}
}
#endif // HAVE_SYS_XATTR_H

bool attrmatch(const char *f1, const char *f2)
{
	EMU_STRUCT_STAT s1, s2;
	TEST_THAT(EMU_LSTAT(f1, &s1) == 0);
	TEST_THAT(EMU_LSTAT(f2, &s2) == 0);

#ifdef HAVE_SYS_XATTR_H
	{
		std::map<std::string,std::string> xattr1, xattr2;
		if(!readxattr_into_map(f1, xattr1)
			|| !readxattr_into_map(f2, xattr2))
		{
			return false;
		}
		if(!(xattr1 == xattr2))
		{
			return false;
		}
	}
#endif // HAVE_SYS_XATTR_H

	// if link, just make sure other file is a link too, and that the link to names match
	if((s1.st_mode & S_IFMT) == S_IFLNK)
	{
#ifdef WIN32
		TEST_FAIL_WITH_MESSAGE("No symlinks on win32!")
#else
		if((s2.st_mode & S_IFMT) != S_IFLNK) return false;

		char p1[PATH_MAX], p2[PATH_MAX];
		int p1l = ::readlink(f1, p1, PATH_MAX);
		int p2l = ::readlink(f2, p2, PATH_MAX);
		TEST_THAT(p1l != -1 && p2l != -1);
		// terminate strings properly
		p1[p1l] = '\0';
		p2[p2l] = '\0';
		return strcmp(p1, p2) == 0;
#endif
	}

	// modification times
	if(FileModificationTime(s1) != FileModificationTime(s2))
	{
		return false;
	}

	// compare the rest
	return (s1.st_mode == s2.st_mode && s1.st_uid == s2.st_uid && s1.st_gid == s2.st_gid);
}

bool unpack_files(const std::string& archive_file,
	const std::string& destination_dir = "testfiles",
	const std::string& tar_options = "")
{
	BOX_INFO("Unpacking test fixture archive into " << destination_dir
		<< ": " << archive_file);

#ifdef _MSC_VER // No tar, use 7zip.
	// 7za only extracts the tgz file to a tar file, which we have to extract in a
	// separate step.
	std::string cmd = std::string("7za x testfiles/") + archive_file + ".tgz -aos "
		"-otestfiles >nul:";
	TEST_LINE_OR(::system(cmd.c_str()) == 0, cmd, return false);

	cmd = std::string("7za x testfiles/") + archive_file + ".tar -aos "
		"-o" + destination_dir + " -x!.\\TestDir1\\symlink? -x!.\\test2 >nul:";
#elif defined WIN32 // Cygwin + MinGW, we can use real tar.
	std::string cmd("tar xz");
	cmd += tar_options + " -f testfiles/" + archive_file + ".tgz " +
		"-C " + destination_dir;
#else // Unixish, but Solaris tar doesn't like decompressing gzip files.
	std::string cmd("gzip -d < testfiles/");
	cmd += archive_file + ".tgz | ( cd " + destination_dir + " && tar xf" +
		tar_options + " -)";
#endif

	TEST_LINE_OR(::system(cmd.c_str()) == 0, cmd, return false);
	return true;
}

bool configure_bbackupd(BackupDaemon& bbackupd, const std::string& config_file)
{
	// Stop bbackupd initialisation from changing the console logging level
	// and the program name tag.
	Logger& console(Logging::GetConsole());
	Logger::LevelGuard undo_log_level_change(console, console.GetLevel());
	Logging::Tagger undo_program_name_change;

	std::vector<std::string> args;
	size_t last_arg_start = 0;
	for (size_t pos = 0; pos <= bbackupd_args.size(); pos++)
	{
		char c;

		if (pos == bbackupd_args.size())
		{
			c = ' '; // finish last argument
		}
		else
		{
			c = bbackupd_args[pos];
		}

		if (c == ' ')
		{
			if (last_arg_start < pos)
			{
				std::string last_arg =
					bbackupd_args.substr(last_arg_start,
						pos - last_arg_start);
				args.push_back(last_arg);
			}

			last_arg_start = pos + 1;
		}
	}

	MemoryBlockGuard<const char **> argv_buffer(sizeof(const char*) * (args.size() + 1));
	const char **argv = argv_buffer;
	argv_buffer[0] = "bbackupd";
	for (size_t i = 0; i < args.size(); i++)
	{
		argv_buffer[i + 1] = args[i].c_str();
	}

	TEST_EQUAL_LINE(0, bbackupd.ProcessOptions(args.size() + 1, argv),
		"processing command-line options");

	bbackupd.Configure(config_file);
	bbackupd.InitCrypto();

	return true;
}

bool prepare_test_with_client_daemon(BackupDaemon& bbackupd, bool do_unpack_files = true,
	bool do_start_bbstored = true,
	const std::string& bbackupd_conf_file = "testfiles/bbackupd.conf")
{
	Timers::Cleanup(false); // don't throw exception if not initialised
	Timers::Init();

	if (do_start_bbstored)
	{
		TEST_THAT_OR(StartServer(), FAIL);
	}

	if (do_unpack_files)
	{
		TEST_THAT_OR(unpack_files("test_base"), FAIL);
		// Older versions of GNU tar fail to set the timestamps on
		// symlinks, which makes them appear too recent to be backed
		// up immediately, causing test_bbackupd_uploads_files() for
		// example to fail. So restore the timestamps manually.
		// http://lists.gnu.org/archive/html/bug-tar/2009-08/msg00007.html
		// http://git.savannah.gnu.org/cgit/tar.git/plain/NEWS?id=release_1_24
		#ifdef HAVE_UTIMENSAT
		const struct timespec times[2] = {
			{1065707200, 0},
			{1065707200, 0},
		};
		const char * filenames[] = {
			"testfiles/TestDir1/symlink1",
			"testfiles/TestDir1/symlink2",
			"testfiles/TestDir1/symlink3",
			NULL,
		};
		for (int i = 0; filenames[i] != NULL; i++)
		{
			TEST_THAT_OR(utimensat(AT_FDCWD, filenames[i],
				times, AT_SYMLINK_NOFOLLOW) == 0,
				BOX_LOG_SYS_ERROR("Failed to change "
					"timestamp on symlink: " <<
					filenames[i]));
		}
		#endif
	}

	TEST_THAT_OR(configure_bbackupd(bbackupd, bbackupd_conf_file), FAIL);
	return true;
}

//! Simplifies calling setUp() with the current function name in each test.
#define SETUP_TEST_BBACKUPD() \
	SETUP(); \
	TEST_THAT(bbackupd_pid == 0 || StopClient()); \
	TEST_THAT(bbstored_pid == 0 || StopServer()); \
	TEST_THAT(create_account(10000, 20000));

#define SETUP_WITHOUT_FILES() \
	SETUP_TEST_BBACKUPD(); \
	BackupDaemon bbackupd; \
	TEST_THAT_OR(prepare_test_with_client_daemon(bbackupd, false), FAIL); \
	TEST_THAT_OR(::mkdir("testfiles/TestDir1", 0755) == 0, FAIL);

#define SETUP_WITH_BBSTORED() \
	SETUP_TEST_BBACKUPD(); \
	BackupDaemon bbackupd; \
	TEST_THAT_OR(prepare_test_with_client_daemon(bbackupd), FAIL);

#define TEARDOWN_TEST_BBACKUPD() \
	TEST_THAT(bbackupd_pid == 0 || StopClient()); \
	TEST_THAT(bbstored_pid == 0 || StopServer()); \
	TEARDOWN();

bool setup_test_specialised_bbstored(RaidAndS3TestSpecs::Specialisation& spec)
{
	if(spec.name() == "store")
	{
		TEST_THAT_OR(StartServer(), FAIL);
	}

	TEST_THAT_OR(::mkdir("testfiles/TestDir1", 0755) == 0, FAIL);

	return true;
}

#define SETUP_TEST_SPECIALISED_BBSTORED(spec) \
	SETUP_TEST_SPECIALISED(spec); \
	TEST_THAT(setup_test_specialised_bbstored(spec)); \
	spec.control().GetFileSystem().ReleaseLock(); \
	std::string bbackupd_conf_file = (spec.name() == "s3") ? "testfiles/bbackupd.s3.conf" : \
			"testfiles/bbackupd.conf";

bool setup_test_specialised_bbackupd(RaidAndS3TestSpecs::Specialisation& spec,
	BackupDaemon& bbackupd, const std::string& bbackupd_conf_file)
{
	TEST_THAT_OR(prepare_test_with_client_daemon(bbackupd, true, // do_unpack_files
		false, // !do_start_bbstored
		bbackupd_conf_file),
		FAIL);

	return true;
}

#define SETUP_TEST_SPECIALISED_BBACKUPD(spec) \
	SETUP_TEST_SPECIALISED_BBSTORED(spec); \
	BackupDaemon bbackupd; \
	TEST_THAT(setup_test_specialised_bbackupd(spec, bbackupd, bbackupd_conf_file));

#define TEST_COMPARE_SPECIALISED(spec, expected_status) \
	BOX_INFO("Running external compare, expecting " #expected_status); \
	{ \
		std::string bbackupd_conf_file = (spec.name() == "s3") \
			? "testfiles/bbackupd.s3.conf" \
			: "testfiles/bbackupd.conf"; \
		TEST_THAT(compare_external(BackupQueries::ReturnCode::expected_status, \
			"", "-acQ", bbackupd_conf_file)); \
	}

bool test_file_attribute_storage()
{
	SETUP();
	TEST_THAT_OR(unpack_files("test_base"), FAIL);

	// Read attributes from files
	BackupClientFileAttributes t1;
	t1.ReadAttributes("testfiles/test1");
	TEST_THAT(!t1.IsSymLink());

#ifndef WIN32
	BackupClientFileAttributes t2;
	t2.ReadAttributes("testfiles/test2");
	TEST_THAT(t2.IsSymLink());
	// Check that it's actually been encrypted (search for symlink name encoded in it)
	void *te = ::memchr(t2.GetBuffer(), 't', t2.GetSize() - 3);
	TEST_THAT(te == 0 || ::memcmp(te, "test", 4) != 0);
#endif

	BackupClientFileAttributes t3;
	{
		Logger::LevelGuard(Logging::GetConsole(), Log::ERROR);
		TEST_CHECK_THROWS(t3.ReadAttributes("doesn't exist"),
			CommonException, OSFileError);
	}

	// Create some more files
	FILE *f = fopen("testfiles/test1_n", "w");
	fclose(f);
	f = fopen("testfiles/test2_n", "w");
	fclose(f);

	// Apply attributes to these new files
	t1.WriteAttributes("testfiles/test1_n");
#ifdef WIN32
	// We can't apply symlink attributes on Win32, so use a normal file's
	// attributes instead.
	t1.WriteAttributes("testfiles/test2_n");
#else
	t2.WriteAttributes("testfiles/test2_n");
#endif

#ifndef WIN32
	{
		Logger::LevelGuard(Logging::GetConsole(), Log::ERROR);
		TEST_CHECK_THROWS(t1.WriteAttributes("testfiles/test1_nXX"),
			CommonException, OSFileError);
		TEST_CHECK_THROWS(t3.WriteAttributes("doesn't exist"),
			BackupStoreException, AttributesNotLoaded);
	}

	// Test that attributes are vaguely similar
	TEST_THAT(attrmatch("testfiles/test1", "testfiles/test1_n"));
	TEST_THAT(attrmatch("testfiles/test2", "testfiles/test2_n"));
#endif

	// Check encryption, and recovery from encryption
	// First, check that two attributes taken from the same thing have different encrypted values (think IV)
	BackupClientFileAttributes t1b;
	t1b.ReadAttributes("testfiles/test1");
	TEST_THAT(::memcmp(t1.GetBuffer(), t1b.GetBuffer(), t1.GetSize()) != 0);
	// But that comparing them works OK.
	TEST_THAT(t1 == t1b);
	// Then store them both to a stream
	CollectInBufferStream stream;
	t1.WriteToStream(stream);
	t1b.WriteToStream(stream);
	// Read them back again
	stream.SetForReading();
	BackupClientFileAttributes t1_r, t1b_r;
	t1_r.ReadFromStream(stream, 1000);
	t1b_r.ReadFromStream(stream, 1000);
	TEST_THAT(::memcmp(t1_r.GetBuffer(), t1b_r.GetBuffer(), t1_r.GetSize()) != 0);
	TEST_THAT(t1_r == t1b_r);
	TEST_THAT(t1 == t1_r);
	TEST_THAT(t1b == t1b_r);
	TEST_THAT(t1_r == t1b);
	TEST_THAT(t1b_r == t1);

#ifdef HAVE_SYS_XATTR_H
	// Write some attributes to the file, checking for ENOTSUP
	bool xattrNotSupported = false;
	if(!write_xattr_test("testfiles/test1", "user.attr_1", 1000, &xattrNotSupported) && xattrNotSupported)
	{
		::printf("***********\nYour platform supports xattr, but your filesystem does not.\nSkipping tests.\n***********\n");
	}
	else
	{
		BackupClientFileAttributes x1, x2, x3, x4;

		// Write more attributes
		TEST_THAT(write_xattr_test("testfiles/test1", "user.attr_2", 947));
		TEST_THAT(write_xattr_test("testfiles/test1", "user.sadfohij39998.3hj", 123));

		// Read file attributes
		x1.ReadAttributes("testfiles/test1");

		// Write file attributes
		FILE *f = fopen("testfiles/test1_nx", "w");
		fclose(f);
		x1.WriteAttributes("testfiles/test1_nx");

		// Compare to see if xattr copied
		TEST_THAT(attrmatch("testfiles/test1", "testfiles/test1_nx"));

		// Add more attributes to a file
		x2.ReadAttributes("testfiles/test1");
		TEST_THAT(write_xattr_test("testfiles/test1", "user.328989sj..sdf", 23));

		// Read them again, and check that the Compare() function detects that they're different
		x3.ReadAttributes("testfiles/test1");
		TEST_THAT(x1.Compare(x2, true, true));
		TEST_THAT(!x1.Compare(x3, true, true));

		// Change the value of one of them, leaving the size the same.
		TEST_THAT(write_xattr_test("testfiles/test1", "user.328989sj..sdf", 23));
		x4.ReadAttributes("testfiles/test1");
		TEST_THAT(!x1.Compare(x4, true, true));
	}
	finish_with_write_xattr_test();
#endif // HAVE_SYS_XATTR_H

	TEARDOWN();
}

int64_t get_object_id(BackupProtocolCallable &protocol, const char *name, int64_t in_dir_object_id,
	int required_flags = 0)
{
	protocol.QueryListDirectory(
		in_dir_object_id,
		required_flags,
		BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
		false /* !want_attributes */);

	// Retrieve the directory from the stream following
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(protocol.ReceiveStream());
	dir.ReadFromStream(*dirstream, protocol.GetTimeout());

	BackupStoreFilenameClear dirname(name);
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = i.FindMatchingClearName(dirname);

	if(en != 0)
	{
		return en->GetObjectID();
	}
	else
	{
		return 0;
	}
}

void terminate_on_alarm(int sigraised)
{
	abort();
}

void do_interrupted_restore(const TLSContext &context, int64_t restoredirid,
	const std::string& bbackupd_conf_file)
{
#ifdef WIN32
	// For once, Windows makes things easier. We can start a process without blocking (waiting
	// for it to finish) using CreateProcess, wrapped by LaunchServer:

	std::ostringstream cmd;
	cmd << BBACKUPQUERY << " " <<
		"-c " << bbackupd_conf_file << " \"restore "
		"-i " << restoredirid << " " /* remote dir ID */
		"testfiles/restore-interrupt\" " /* local */
		"quit";

	int pid = LaunchServer(cmd.str(), NULL /* pid_file */);
#else // !WIN32
	int pid = 0;
	switch((pid = fork()))
	{
	case 0:
		// child process
		try
		{
			Logging::SetProgramName("bbackupquery");
			Logging::ShowTagOnConsole show_tag_on_console;

			// connect and log in
			std::string errs;
			std::auto_ptr<Configuration> apConfig(
				Configuration::LoadAndVerify
					(bbackupd_conf_file, &BackupDaemonConfigVerify, errs));
			std::auto_ptr<ConfiguredBackupClient> apClient =
				GetConfiguredBackupClient(*apConfig);
			apClient->Login(true); // read_only

			// Test the restoration
			TEST_THAT(BackupClientRestore(*apClient,
					restoredirid, // ID of remote directory to restore
					"Test1", // remote (source) name, for display (logging) only
					"testfiles/restore-interrupt", // local (destination) name
					true, // print progress dots
					false, // restore deleted
					false, // undelete after
					false, // resume
					false // keep going
				) == Restore_Complete);

			// Log out
			apClient->QueryFinished();
			exit(0);
		}
		catch(std::exception &e)
		{
			printf("FAILED: Forked daemon caught exception: %s\n", e.what());
			exit(1);
		}
		catch(...)
		{
			printf("FAILED: Forked daemon caught unknown exception\n");
			exit(1);
		}
		break;

	case -1:
		{
			printf("Fork failed\n");
			exit(1);
		}

	default:
		break; // fall through
	}
#endif // !WIN32

	// Wait until a resume file is written, then terminate the child
	box_time_t deadline = GetCurrentBoxTime() + SecondsToBoxTime(10);

	while(true)
	{
		TEST_THAT(GetCurrentBoxTime() <= deadline); // restore took too long

		// Test for existence of the result file
		int64_t resumesize = 0;
		if((FileExists("testfiles/restore-interrupt.boxbackupresume", &resumesize) &&
		    resumesize > 16) || GetCurrentBoxTime() > deadline)
		{
			// It's done something. Terminate it. If it forked, then we need to wait()
			// for it as well, otherwise the zombie process will never die.
			// If the restore process returned 0, then it actually finished, so we
			// haven't tested anything. So check that it doesn't (assume 1 instead):
#ifdef WIN32
			TEST_THAT(KillServer(pid,
				true, // wait_for_process
				0, // expected_exit_status
				0 // expected_signal: no signals on Windows, and not used
			));
#else
			TEST_THAT(KillServer(pid,
				true, // wait_for_process
				0, // expected_exit_status
				SIGTERM // expected_signal: no SIGTERM handler installed
			));
#endif
			break;
		}

		// Process finished unexpectedly? We were too slow?

#ifdef HAVE_WAITPID
		WaitForProcessExit(pid, SIGTERM, 1); // expected_exit_status
		// If successful, then ServerIsAlive will return false below.
#endif

		if(!ServerIsAlive(pid))
		{
			// Even though we forked a child process, if it was a zombie waiting for us
			// to reap it then it would still be alive, so we know that it's not, so we
			// don't have to wait for it.
			break;
		}

		// Give up timeslot so as not to hog the processor
		::sleep(0);
	}
}

#ifdef WIN32
bool set_file_time(const char* filename, FILETIME creationTime,
	FILETIME lastModTime, FILETIME lastAccessTime)
{
	HANDLE handle = openfile(filename, O_RDWR, 0);
	TEST_THAT(handle != INVALID_HANDLE_VALUE);
	if (handle == INVALID_HANDLE_VALUE) return false;

	BOOL success = SetFileTime(handle, &creationTime, &lastAccessTime,
		&lastModTime);
	TEST_THAT(success);

	TEST_THAT(CloseHandle(handle));
	return success;
}
#endif

int64_t SearchDir(BackupStoreDirectory& rDir,
	const std::string& rChildName)
{
	BackupStoreDirectory::Iterator i(rDir);
	BackupStoreFilenameClear child(rChildName);
	BackupStoreDirectory::Entry *en = i.FindMatchingClearName(child);
	if (en == 0) return 0;
	int64_t id = en->GetObjectID();
	TEST_THAT(id > 0);
	TEST_THAT(id != BackupProtocolListDirectory::RootDirectory);
	return id;
}

std::auto_ptr<BackupStoreDirectory> ReadDirectory
(
	BackupProtocolCallable& rClient,
	int64_t id = BackupProtocolListDirectory::RootDirectory
)
{
	std::auto_ptr<BackupProtocolSuccess> dirreply(
		rClient.QueryListDirectory(id, false, 0, false));
	std::auto_ptr<BackupStoreDirectory> apDir(
		new BackupStoreDirectory(rClient.ReceiveStream(), SHORT_TIMEOUT));
	return apDir;
}

bool test_entry_deleted(BackupStoreDirectory& rDir,
	const std::string& rName)
{
	BackupStoreDirectory::Iterator i(rDir);

	BackupStoreDirectory::Entry *en = i.FindMatchingClearName(
		BackupStoreFilenameClear(rName));
	TEST_THAT_OR(en != 0, return false);

	int16_t flags = en->GetFlags();
	TEST_LINE(flags & BackupStoreDirectory::Entry::Flags_Deleted,
		rName + " should have been deleted");
	return flags & BackupStoreDirectory::Entry::Flags_Deleted;
}

bool compare_external(BackupQueries::ReturnCode::Type expected_status,
	const std::string& bbackupquery_options = "",
	const std::string& compare_options = "-acQ",
	const std::string& bbackupd_conf_file = "testfiles/bbackupd.conf")
{
	std::string cmd = BBACKUPQUERY;
	cmd += " ";
	cmd += (expected_status == BackupQueries::ReturnCode::Compare_Same)
		? "-Wwarning" : "-Werror";
	cmd += " -c " + bbackupd_conf_file;
	cmd += " " + bbackupquery_options;
	cmd += " \"compare " + compare_options + "\" quit";

	int returnValue = ::system(cmd.c_str());
	int expected_system_result = (int) expected_status;

#ifndef WIN32
	expected_system_result <<= 8;
#endif

	TEST_EQUAL_LINE(expected_system_result, returnValue, "compare return value");
	TestRemoteProcessMemLeaks("bbackupquery.memleaks");
	return (returnValue == expected_system_result);
}

bool compare_in_process(BackupQueries::ReturnCode::Type expected_status,
	BackupProtocolCallable& client, const std::string& compare_options = "acQ")
{
	std::auto_ptr<Configuration> config =
		load_config_file(DEFAULT_BBACKUPD_CONFIG_FILE, BackupDaemonConfigVerify);
	TEST_THAT_OR(config.get(), return false);
	BackupQueries bbackupquery(client, *config, false);

	std::vector<std::string> args;
	bool opts[256] = {};
	for (std::string::const_iterator i = compare_options.begin();
		i != compare_options.end(); i++)
	{
		opts[(unsigned char)*i] = true;
	}
	bbackupquery.CommandCompare(args, opts);
	TEST_EQUAL_OR(expected_status, bbackupquery.GetReturnCode(),
		return false);
	return true;
}

bool bbackupquery(const std::string& arguments,
	const std::string& memleaks_file = "bbackupquery.memleaks")
{
	std::string cmd = BBACKUPQUERY;
	cmd += " -c testfiles/bbackupd.conf " + arguments + " quit";

	int returnValue = ::system(cmd.c_str());

#ifndef WIN32
	returnValue >>= 8;
#endif

	TestRemoteProcessMemLeaks(memleaks_file.c_str());
	TEST_EQUAL(returnValue, BackupQueries::ReturnCode::Command_OK);
	return (returnValue == BackupQueries::ReturnCode::Command_OK);
}

bool restore(const std::string& location, const std::string& dest_dir)
{
	std::string cmd = "\"restore " + location + " " + dest_dir + "\"";
	TEST_THAT_OR(bbackupquery(cmd), FAIL);
	return true;
}

bool touch_and_wait(const std::string& filename)
{
	int fd = open(filename.c_str(), O_CREAT | O_WRONLY, 0755);
	TEST_THAT(fd > 0);
	if (fd <= 0) return false;

	// write again, to update the file's timestamp
	int write_result = write(fd, "z", 1);
	TEST_EQUAL_LINE(1, write_result, "Buffer write");
	if (write_result != 1) return false;

	TEST_THAT(close(fd) == 0);

	// wait long enough to put file into sync window
	wait_for_operation(5, "locally modified file to "
		"mature for sync");
	return true;
}

TLSContext sTlsContext;

#define TEST_COMPARE(expected_status) \
	BOX_INFO("Running external compare, expecting " #expected_status); \
	TEST_THAT(compare_external(BackupQueries::ReturnCode::expected_status));
#define TEST_COMPARE_EXTRA(expected_status, ...) \
	BOX_INFO("Running external compare, expecting " #expected_status); \
	TEST_THAT(compare_external(BackupQueries::ReturnCode::expected_status, __VA_ARGS__));

#define TEST_COMPARE_LOCAL(expected_status, client) \
	BOX_INFO("Running compare in-process, expecting " #expected_status); \
	TEST_THAT(compare_in_process(BackupQueries::ReturnCode::expected_status, client));
#define TEST_COMPARE_LOCAL_EXTRA(expected_status, client, compare_options) \
	BOX_INFO("Running compare in-process, expecting " #expected_status); \
	TEST_THAT(compare_in_process(BackupQueries::ReturnCode::expected_status, client, \
		compare_options));

bool search_for_file(const std::string& filename)
{
	std::auto_ptr<BackupProtocolCallable> client =
		connect_and_login(sTlsContext, BackupProtocolLogin::Flags_ReadOnly);

	std::auto_ptr<BackupStoreDirectory> dir = ReadDirectory(*client);
	int64_t testDirId = SearchDir(*dir, filename);
	client->QueryFinished();

	return (testDirId != 0);
}

class MockClientContext : public BackupClientContext
{
public:
	BackupProtocolCallable& mrClient;
	int mNumKeepAlivesPolled;
	int mKeepAliveTime;

	MockClientContext
	(
		const Configuration& rConfig,
		LocationResolver &rResolver,
		ProgressNotifier &rProgressNotifier,
		BackupProtocolCallable& rClient
	)
	: BackupClientContext(rConfig, rResolver, rProgressNotifier),
	  mrClient(rClient),
	  mNumKeepAlivesPolled(0),
	  mKeepAliveTime(-1)
	{ }

	// Because we don't set mapConnection, we need to override SetNiceMode too:
	virtual void SetNiceMode(bool enabled)
	{ }

	BackupProtocolCallable &GetConnection()
	{
		return mrClient;
	}

	virtual BackupProtocolCallable* GetOpenConnection() const
	{
		return &mrClient;
	}

	void SetKeepAliveTime(int iSeconds)
	{
		mKeepAliveTime = iSeconds;
		BackupClientContext::SetKeepAliveTime(iSeconds);
	}

	virtual void DoKeepAlive()
	{
		mNumKeepAlivesPolled++;
		BackupClientContext::DoKeepAlive();
	}
};

class MockBackupDaemon : public BackupDaemon
{
	BackupProtocolCallable& mrClient;

public:
	MockBackupDaemon(BackupProtocolCallable &rClient)
	: mrClient(rClient)
	{ }

	std::auto_ptr<BackupClientContext> GetNewContext
	(
		LocationResolver &rResolver,
		ProgressNotifier &rProgressNotifier
	)
	{
		std::auto_ptr<BackupClientContext> context(
			new MockClientContext(GetConfiguration(), rResolver, rProgressNotifier,
				mrClient));
		return context;
	}
};

bool test_bbackupquery_parser_escape_slashes()
{
	SETUP_WITH_BBSTORED();

	BackupProtocolLocal2 connection(0x01234567, "test",
		"backup/01234567/", 0, false);

	BackupClientFileAttributes attr;
	attr.ReadAttributes("testfiles/TestDir1",
		false /* put mod times in the attributes, please */);
	std::auto_ptr<IOStream> attrStream(new MemBlockStream(attr));
	BackupStoreFilenameClear dirname("foo");
	int64_t foo_id = connection.QueryCreateDirectory(
		BACKUPSTORE_ROOT_DIRECTORY_ID, // containing directory
		0, // attrModTime,
		dirname, // dirname,
		attrStream)->GetObjectID();

	attrStream.reset(new MemBlockStream(attr));
	dirname = BackupStoreFilenameClear("/bar");
	int64_t bar_id = connection.QueryCreateDirectory(
		BACKUPSTORE_ROOT_DIRECTORY_ID, // containing directory
		0, // attrModTime,
		dirname, // dirname,
		attrStream)->GetObjectID();

	std::auto_ptr<Configuration> config =
		load_config_file(DEFAULT_BBACKUPD_CONFIG_FILE, BackupDaemonConfigVerify);
	TEST_THAT_OR(config.get(), return false);
	BackupQueries query(connection, *config, false); // read-only

	TEST_EQUAL(foo_id, query.FindDirectoryObjectID("foo"));
	TEST_EQUAL(foo_id, query.FindDirectoryObjectID("/foo"));
	TEST_EQUAL(0, query.FindDirectoryObjectID("\\/foo"));
	TEST_EQUAL(0, query.FindDirectoryObjectID("/bar"));
	TEST_EQUAL(bar_id, query.FindDirectoryObjectID("\\/bar"));
	connection.QueryFinished();

	TEARDOWN_TEST_BBACKUPD();
}

bool test_bbackupquery_getobject_on_nonexistent_file(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED_BBSTORED(spec);

	{
		BackupFileSystem& fs(spec.control().GetFileSystem());
		CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, rwContext, protocol, false); // !ReadOnly
		BackupQueries query(protocol, spec.config(), false); // read-only

		std::vector<std::string> args;
		args.push_back("2"); // object ID
		args.push_back("testfiles/2.obj"); // output file
		bool opts[256];

		Capture capture;
		Logging::TempLoggerGuard guard(&capture);
		query.CommandGetObject(args, opts);
		std::vector<Capture::Message> messages = capture.GetMessages();

		bool found = false;
		for(std::vector<Capture::Message>::iterator
			i = messages.begin(); i != messages.end(); i++)
		{
			if(i->message == "Object ID 0x2 does not exist on store.")
			{
				found = true;
				break;
			}
		}
		TEST_LINE(found, "Last log message was: " << messages.back().message);
		TEST_THAT(!TestFileExists("testfiles/2.obj"));
	}

	TEARDOWN_TEST_SPECIALISED(spec);
}

std::auto_ptr<BackupClientContext> run_bbackupd_sync_with_logging(BackupDaemon& bbackupd)
{
	Logging::Tagger bbackupd_tagger("bbackupd", true); // replace
	Logging::ShowTagOnConsole temp_enable_tags;
	return bbackupd.RunSyncNow();
}

// ASSERT((mpBlockIndex == 0) || (NumBlocksInIndex != 0)) in
// BackupStoreFileEncodeStream::Recipe::Recipe once failed, apparently because
// a zero byte file had a block index but no entries in it. But this test
// doesn't reproduce the error, so it's not enabled for now.

bool test_replace_zero_byte_file_with_nonzero_byte_file()
{
	SETUP_TEST_BBACKUPD();

	TEST_THAT_OR(mkdir("testfiles/TestDir1", 0755) == 0, FAIL);
	FileStream emptyFile("testfiles/TestDir1/f2",
		O_WRONLY | O_CREAT | O_EXCL, 0755);
	wait_for_operation(5, "f2 to be old enough");

	BackupProtocolLocal2 client(0x01234567, "test",
		"backup/01234567/", 0, false);
	MockBackupDaemon bbackupd(client);
	TEST_THAT(configure_bbackupd(bbackupd, "testfiles/bbackupd.conf"));
	run_bbackupd_sync_with_logging(bbackupd);
	TEST_COMPARE_LOCAL(Compare_Same, client);

	MemBlockStream stream("Hello world");
	stream.CopyStreamTo(emptyFile);
	emptyFile.Close();
	wait_for_operation(5, "f2 to be old enough");

	run_bbackupd_sync_with_logging(bbackupd);
	TEST_COMPARE_LOCAL(Compare_Same, client);

	TEARDOWN_TEST_BBACKUPD();
}

// This caused the issue reported by Brendon Baumgartner and described in my
// email to the Box Backup list on Mon, 21 Apr 2014 at 18:44:38. If the
// directory disappears then we used to try to send an empty attributes block
// to the server, which is illegal.
bool test_backup_disappearing_directory(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED_BBACKUPD(spec);

	class BackupClientDirectoryRecordHooked : public BackupClientDirectoryRecord
	{
	public:
		BackupClientDirectoryRecordHooked(int64_t ObjectID,
			const std::string &rSubDirName)
		: BackupClientDirectoryRecord(ObjectID, rSubDirName),
		  mDeletedOnce(false)
		{ }
		bool mDeletedOnce;
		bool UpdateItems(SyncParams &rParams, const std::string &rLocalPath,
			const std::string &rRemotePath,
			const Location& rBackupLocation,
			BackupStoreDirectory *pDirOnStore,
			std::vector<BackupStoreDirectory::Entry *> &rEntriesLeftOver,
			std::vector<std::string> &rFiles,
			const std::vector<std::string> &rDirs)
		{
			if(!mDeletedOnce)
			{
				TEST_THAT(::rmdir("testfiles/TestDir1/dir23") == 0);
				mDeletedOnce = true;
			}

			return BackupClientDirectoryRecord::UpdateItems(rParams,
				rLocalPath, rRemotePath, rBackupLocation,
				pDirOnStore, rEntriesLeftOver, rFiles, rDirs);
		}
	};

	BackupClientContext clientContext
	(
		bbackupd.GetConfiguration(),
		bbackupd, // rLocationResolver
		bbackupd // rProgressNotifier
	);

	BackupClientInodeToIDMap oldMap, newMap;
	oldMap.OpenEmpty();
	newMap.Open("testfiles/test_map.db", false, true);
	clientContext.SetIDMaps(&oldMap, &newMap);

	BackupClientDirectoryRecord::SyncParams params(
		bbackupd, // rRunStatusProvider,
		bbackupd, // rSysadminNotifier,
		bbackupd, // rProgressNotifier,
		clientContext,
		&bbackupd);
	params.mSyncPeriodEnd = GetCurrentBoxTime();

	BackupClientFileAttributes attr;
	attr.ReadAttributes("testfiles/TestDir1",
		false /* put mod times in the attributes, please */);
	std::auto_ptr<IOStream> attrStream(new MemBlockStream(attr));
	BackupStoreFilenameClear dirname("Test1");
	std::auto_ptr<BackupProtocolSuccess>
		dirCreate(clientContext.GetConnection().QueryCreateDirectory(
			BACKUPSTORE_ROOT_DIRECTORY_ID, // containing directory
			0, // attrModTime,
			dirname, // dirname,
			attrStream));
	clientContext.CloseAnyOpenConnection();

	// Object ID for later creation
	int64_t oid = dirCreate->GetObjectID();
	BackupClientDirectoryRecordHooked record(oid, "Test1");

	TEST_COMPARE_SPECIALISED(spec, Compare_Different);

	Location fakeLocation;
	record.SyncDirectory(params,
		BACKUPSTORE_ROOT_DIRECTORY_ID,
		"testfiles/TestDir1", // locationPath,
		"/whee", // remotePath
		fakeLocation);
	clientContext.CloseAnyOpenConnection();
	TEST_COMPARE_SPECIALISED(spec, Compare_Same);

	// Run another backup, check that we haven't got an inconsistent
	// state that causes a crash.
	record.SyncDirectory(params,
		BACKUPSTORE_ROOT_DIRECTORY_ID,
		"testfiles/TestDir1", // locationPath,
		"/whee", // remotePath
		fakeLocation);
	clientContext.CloseAnyOpenConnection();
	TEST_COMPARE_SPECIALISED(spec, Compare_Same);

	// Now recreate it and run another backup, check that we haven't got
	// an inconsistent state that causes a crash or prevents us from
	// creating the directory if it appears later.
	TEST_THAT(::mkdir("testfiles/TestDir1/dir23", 0755) == 0);
	TEST_COMPARE_SPECIALISED(spec, Compare_Different);

	record.SyncDirectory(params,
		BACKUPSTORE_ROOT_DIRECTORY_ID,
		"testfiles/TestDir1", // locationPath,
		"/whee", // remotePath
		fakeLocation);
	clientContext.CloseAnyOpenConnection();
	TEST_COMPARE_SPECIALISED(spec, Compare_Same);

	TEARDOWN_TEST_SPECIALISED_NO_CHECK(spec);
}

class KeepAliveBackupProtocolLocal : public BackupProtocolLocal2
{
public:
	int mNumKeepAlivesSent;
	int mNumKeepAlivesReceived;

public:
	KeepAliveBackupProtocolLocal(int32_t AccountNumber,
		const std::string& ConnectionDetails,
		const std::string& AccountRootDir, int DiscSetNumber,
		bool ReadOnly)
	: BackupProtocolLocal2(AccountNumber, ConnectionDetails, AccountRootDir,
		DiscSetNumber, ReadOnly),
	  mNumKeepAlivesSent(0),
	  mNumKeepAlivesReceived(0)
	{ }

	std::auto_ptr<BackupProtocolIsAlive> Query(const BackupProtocolGetIsAlive &rQuery)
	{
		mNumKeepAlivesSent++;
		std::auto_ptr<BackupProtocolIsAlive> response =
			BackupProtocolLocal::Query(rQuery);
		mNumKeepAlivesReceived++;
		return response;
	}
	using BackupProtocolLocal2::Query;
};

bool test_ssl_keepalives()
{
	SETUP_TEST_BBACKUPD();

	KeepAliveBackupProtocolLocal connection(0x01234567, "test", "backup/01234567/",
		0, false);
	MockBackupDaemon bbackupd(connection);
	TEST_THAT_OR(prepare_test_with_client_daemon(bbackupd), FAIL);

	// Test that sending a keepalive actually works, when the timeout has expired,
	// but doesn't send anything at the beginning:
	{
		MockClientContext context(
			bbackupd.GetConfiguration(),
			bbackupd, // rLocationResolver
			bbackupd, // rProgressNotifier
			connection); // rClient

		// Set the timeout to 1 second
		context.SetKeepAliveTime(1);

		// Check that DoKeepAlive() does nothing right now
		context.DoKeepAlive();
		TEST_EQUAL(0, connection.mNumKeepAlivesSent);

		// Sleep until just before the timer expires, check that DoKeepAlive()
		// still does nothing.
		ShortSleep(MilliSecondsToBoxTime(900), true);
		context.DoKeepAlive();
		TEST_EQUAL(0, connection.mNumKeepAlivesSent);

		// Sleep until just after the timer expires, check that DoKeepAlive()
		// sends a GetIsAlive message now.
		ShortSleep(MilliSecondsToBoxTime(200), true);
		context.DoKeepAlive();
		TEST_EQUAL(1, connection.mNumKeepAlivesSent);
		TEST_EQUAL(1, connection.mNumKeepAlivesReceived);
		TEST_EQUAL(3, context.mNumKeepAlivesPolled);
	}

	// Do the initial backup. There are no existing files to diff, so the only
	// keepalives polled should be the ones for each directory entry while reading
	// directories, and the one in UpdateItems(), which is also once per item (file
	// or directory). test_base.tgz has 16 directory entries, so we expect 2 * 16 = 32
	// keepalives in total. Except on Windows where there are no symlinks, and when
	// compiled with MSVC we exclude them from the tar extract operation as 7za
	// complains about them, so there should be 3 files less, and thus only 26
	// keepalives.
#ifdef _MSC_VER
	#define NUM_KEEPALIVES_BASE 26
#else
	#define NUM_KEEPALIVES_BASE 32
#endif

	std::auto_ptr<BackupClientContext> apContext = bbackupd.RunSyncNow();
	MockClientContext* pContext = (MockClientContext *)(apContext.get());
	TEST_EQUAL(NUM_KEEPALIVES_BASE, pContext->mNumKeepAlivesPolled);
	TEST_EQUAL(1, pContext->mKeepAliveTime);

	// Calculate the number of blocks that will be in ./TestDir1/x1/dsfdsfs98.fd,
	// which is 4269 bytes long.
	int64_t NumBlocks;
	int32_t BlockSize, LastBlockSize;
	BackupStoreFileEncodeStream::CalculateBlockSizes(4269, NumBlocks, BlockSize, LastBlockSize);
	TEST_EQUAL(4096, BlockSize);
	TEST_EQUAL(173, LastBlockSize);
	TEST_EQUAL(2, NumBlocks);

	// Now modify the file and run another backup. It's the only file that should be
	// diffed, and DoKeepAlive() should be called for each block size in the original
	// file, times the number of times that block size fits into the new file,
	// i.e. 1 + (4269 / 256) = 18 times (plus the same 32 while scanning, as above).

	{
		int fd = open("testfiles/TestDir1/x1/dsfdsfs98.fd", O_WRONLY);
		TEST_THAT_OR(fd > 0, FAIL);

		char buffer[4000];
		memset(buffer, 0, sizeof(buffer));

		TEST_EQUAL_LINE(sizeof(buffer),
			write(fd, buffer, sizeof(buffer)),
			"Buffer write");
		TEST_THAT(close(fd) == 0);

		wait_for_operation(5, "modified file to be old enough");
	}

	apContext = bbackupd.RunSyncNow();
	pContext = (MockClientContext *)(apContext.get());
	TEST_EQUAL(NUM_KEEPALIVES_BASE + (4269/4096) + (4269/173),
		pContext->mNumKeepAlivesPolled);
	TEARDOWN_TEST_BBACKUPD();
}

bool assert_found_in_id_map(const BackupClientInodeToIDMap& id_map,
	InodeRefType hardlinked_file_inode_num, bool expected_found, int64_t expected_object_id = 0,
	int64_t expected_dir_id = 0, const std::string& expected_path = "")
{
	int num_failures_initial = num_failures;
	int64_t found_object_id, found_dir_id;
	std::string found_path;
	TEST_EQUAL_OR(expected_found, id_map.Lookup(hardlinked_file_inode_num, found_object_id,
		found_dir_id, &found_path), return false);
	if(expected_found)
	{
		TEST_EQUAL(expected_dir_id, found_dir_id);
		TEST_EQUAL(expected_object_id, found_object_id);
		TEST_EQUAL(expected_path, found_path);
	}
	return (num_failures == num_failures_initial);
}

class BackupDaemonIDMapPatched : public BackupDaemon
{
public:
	// Don't delete and rename the ID maps during RunSyncNow, so that we can access and
	// inspect them:
	void CommitIDMapsAfterSync() { }
	// Give us a way to really commit them after inspection:
	void ReallyCommitIDMapsAfterSync()
	{
		BackupDaemon::CommitIDMapsAfterSync();
	}
};

bool run_backup_compare_and_check_id_map(BackupDaemonIDMapPatched& bbackupd,
	RaidAndS3TestSpecs::Specialisation& spec, InodeRefType hardlinked_file_inode_num,
	int64_t file_id_1, int64_t dir_id_1,
	const std::string& expected_path_before = "testfiles/TestDir1/x1/dsfdsfs98.fd",
	int64_t file_id_2 = 0, int64_t dir_id_2 = 0,
	const std::string& expected_path_after = "testfiles/TestDir1/x1/dsfdsfs98.fd")
{
	int num_failures_initial = num_failures;
	std::auto_ptr<BackupClientContext> ap_context = run_bbackupd_sync_with_logging(bbackupd);
	// TEST_COMPARE_SPECIALISED(spec, Compare_Same);
	TEST_COMPARE_LOCAL(Compare_Same, ap_context->GetConnection());

	// Old map should contain what the new map did before it was committed:
	TEST_THAT(assert_found_in_id_map(ap_context->GetCurrentIDMap(), hardlinked_file_inode_num,
		true, // expected_found
		file_id_1, // expected_object_id
		dir_id_1, // expected_dir_id
		expected_path_before)); // expected_path

	// And the new map should contain the same, since the checksum didn't change, so we didn't
	// download the directory listing, and copied the existing ID map entry instead:
	TEST_THAT(assert_found_in_id_map(ap_context->GetNewIDMap(), hardlinked_file_inode_num,
		true, // expected_found
		file_id_2 ? file_id_2 : file_id_1, // expected_object_id
		dir_id_2  ? dir_id_2  : dir_id_1, // expected_dir_id,
		expected_path_after)); // expected_path

	ap_context.reset();
	bbackupd.ReallyCommitIDMapsAfterSync();
	return (num_failures == num_failures_initial);
}

bool test_backup_hardlinked_files(RaidAndS3TestSpecs::Specialisation& spec)
{
	// We could just use a standard BackupDaemon, which requires much less code, but it failed
	// randomly on some platforms, depending on whether creating hardlinks changes the attribute
	// modification time of an inode (which invalidates the cache) or not. Here we override it
	// to force the hash not to change, so the cache is not invalidated, which triggered the bug
	// reliably. It also allows us to inspect the inode map.
	//
	// SETUP_TEST_SPECIALISED_BBACKUPD(spec);
	SETUP_TEST_SPECIALISED_BBSTORED(spec);

	class BackupClientDirectoryRecordPatched : public BackupClientDirectoryRecord
	{
	public:
		BackupClientDirectoryRecordPatched(int64_t ObjectID,
			const std::string &rSubDirName)
		: BackupClientDirectoryRecord(ObjectID, rSubDirName)
		{ }
		// Overridden to create BackupClientDirectoryRecordPatched instead:
		BackupClientDirectoryRecord* CreateSubdirectoryRecord(
			int64_t remote_dir_id, const std::string &subdir_name) override
		{
			return new BackupClientDirectoryRecordPatched(remote_dir_id,
				subdir_name);
		}
		// Increase visibility:
		std::map<std::string, BackupClientDirectoryRecord *> GetSubDirectories() override
		{
			return BackupClientDirectoryRecord::GetSubDirectories();
		}
		// Always return entries in alphabetical order:
		bool ListLocalDirectory(const std::string& local_path,
			const std::string& local_path_non_vss, ProgressNotifier& notifier,
			std::vector<struct dirent>& entries_out) override
		{
			if(!BackupClientDirectoryRecord::ListLocalDirectory(local_path,
				local_path_non_vss, notifier, entries_out))
			{
				return false;
			}
			// https://en.cppreference.com/w/cpp/algorithm/sort
			struct
			{
				bool operator()(const struct dirent& a, const struct dirent& b) const
				{
					return strcmp(a.d_name, b.d_name) < 0;
				}
			}
			sort_by_entry_name;
			std::sort(entries_out.begin(), entries_out.end(), sort_by_entry_name);
			return true;
		}
	};

	class BackupDaemonPatched : public BackupDaemonIDMapPatched
	{
	public:
		virtual BackupClientDirectoryRecord* CreateLocationRootRecord(
			int64_t remote_dir_id, const std::string &location_name)
		{
			return new BackupClientDirectoryRecordPatched(remote_dir_id,
				location_name);
		}
	};

	BackupDaemonPatched bbackupd;
	TEST_THAT(setup_test_specialised_bbackupd(spec, bbackupd, bbackupd_conf_file));

	InodeRefType hardlinked_file_inode_num;
	{
		EMU_STRUCT_STAT st;
		TEST_THAT(EMU_STAT("testfiles/TestDir1/x1/dsfdsfs98.fd", &st) == 0);
		hardlinked_file_inode_num = st.st_ino;
	}

	BackupFileSystem& fs(spec.control().GetFileSystem());
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol, false); // !ReadOnly
	fs.ReleaseLock();

	std::auto_ptr<BackupClientContext> ap_context = run_bbackupd_sync_with_logging(bbackupd);
	TEST_COMPARE_LOCAL(Compare_Same, ap_context->GetConnection());

	int64_t test1_dir_id = get_object_id(protocol, "Test1", BACKUPSTORE_ROOT_DIRECTORY_ID);
	TEST_THAT_OR(test1_dir_id != 0, FAIL);
	int64_t x1_dir_id = get_object_id(protocol, "x1", test1_dir_id);
	TEST_THAT_OR(x1_dir_id != 0, FAIL);
	int64_t file_id_1 = get_object_id(protocol, "dsfdsfs98.fd", x1_dir_id);
	TEST_THAT_OR(file_id_1 != 0, FAIL);

	TEST_THAT(assert_found_in_id_map(ap_context->GetCurrentIDMap(), hardlinked_file_inode_num,
		false)); // !expected_found
	TEST_THAT(assert_found_in_id_map(ap_context->GetNewIDMap(), hardlinked_file_inode_num,
		true, // expected_found
		file_id_1, // expected_object_id
		x1_dir_id, // expected_dir_id = 0,
		"testfiles/TestDir1/x1/dsfdsfs98.fd")); // expected_path

	ap_context.reset();
	bbackupd.ReallyCommitIDMapsAfterSync();

	BOX_NOTICE("Creating a hard-linked file in the same directory (x1/hardlink1)");
	TEST_THAT(EMU_LINK("testfiles/TestDir1/x1/dsfdsfs98.fd",
		"testfiles/TestDir1/x1/hardlink1") == 0);

	TEST_THAT(run_backup_compare_and_check_id_map(bbackupd, spec, hardlinked_file_inode_num,
		file_id_1, x1_dir_id));

	protocol.GetContext().ClearDirectoryCache();
	int64_t file_id_2 = get_object_id(protocol, "hardlink1", x1_dir_id);
	TEST_THAT_OR(file_id_2 != 0, FAIL);
	fs.ReleaseLock();

	BOX_NOTICE("Creating a hard-linked file in a different directory (x2/hardlink2)");
	TEST_THAT(mkdir("testfiles/TestDir1/x2", 0755) == 0);
	TEST_THAT(EMU_LINK("testfiles/TestDir1/x1/dsfdsfs98.fd",
		"testfiles/TestDir1/x2/hardlink2") == 0);

	// Should still point to the first entry, because subsequent ones should not overwrite the
	// first one added to the new map. Because we enforce directory ordering in this test, we
	// know that that's x1/dsfdsfs98.fd:
	TEST_THAT(run_backup_compare_and_check_id_map(bbackupd, spec, hardlinked_file_inode_num,
		file_id_1, x1_dir_id));

	TEST_EQUAL(0, check_account_and_fix_errors(fs));
	protocol.GetContext().ClearDirectoryCache();
	int64_t x2_dir_id = get_object_id(protocol, "x2", test1_dir_id);
	TEST_THAT_OR(x2_dir_id != 0, FAIL);
	int64_t file_id_3 = get_object_id(protocol, "hardlink2", x2_dir_id);
	TEST_THAT_OR(file_id_3 != 0, FAIL);
	fs.ReleaseLock();

	bbackupd.Configure((spec.name() == "s3") ?
		"testfiles/bbackupd.logall.s3.conf" : bbackupd_conf_file);

	LogLevelOverrideByFileGuard log_directory_record_trace(
		"BackupClientDirectoryRecord.cpp", "", Log::TRACE);
	log_directory_record_trace.Install();

	BOX_NOTICE("Deleting one of the hard links to the same inode");
	// Sleep to ensure that if this changes the inode (nlink), it will reliably change the
	// ctime, and thus be detected. There is a small race condition where changes to files
	// between two runs in the same second might not be detected, which is not a realistic
	// scenario, so we don't need to defend against it.
	safe_sleep(1);
	TEST_THAT(EMU_UNLINK("testfiles/TestDir1/x1/dsfdsfs98.fd") == 0);

	// Now, after the backup has run, the map should point to hardlink1 instead:
	TEST_THAT(run_backup_compare_and_check_id_map(bbackupd, spec,
		hardlinked_file_inode_num, file_id_1, x1_dir_id,
		"testfiles/TestDir1/x1/dsfdsfs98.fd", file_id_2, x1_dir_id,
		"testfiles/TestDir1/x1/hardlink1"));

	fs.ReleaseLock();
	TEST_EQUAL(0, check_account_and_fix_errors(fs));
	fs.ReleaseLock();
	TEST_COMPARE_SPECIALISED(spec, Compare_Same);

	BOX_NOTICE("Deleting the other hard link to the same inode");
	// See comment on safe_sleep above, which applies here too:
	safe_sleep(1);
	TEST_THAT(EMU_UNLINK("testfiles/TestDir1/x1/hardlink1") == 0);

	// Now, after the backup has run, the map should point to hardlink2 instead:
	TEST_THAT(run_backup_compare_and_check_id_map(bbackupd, spec, hardlinked_file_inode_num,
		file_id_2, x1_dir_id, "testfiles/TestDir1/x1/hardlink1",
		file_id_3, x2_dir_id, "testfiles/TestDir1/x2/hardlink2"));

	TEARDOWN_TEST_SPECIALISED_NO_CHECK(spec);
}

bool test_backup_pauses_when_store_is_full(RaidAndS3TestSpecs::Specialisation& spec)
{
	// Temporarily enable timestamp logging, to help debug race conditions causing
	// test failures:
	Logger::LevelGuard temporary_verbosity(Logging::GetConsole(), Log::TRACE);
	Console::SettingsGuard save_old_settings;
	Console::SetShowTime(true);
	Console::SetShowTimeMicros(true);

	SETUP_TEST_SPECIALISED_BBSTORED(spec);

	BackupFileSystem& fs(spec.control().GetFileSystem());
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol, false); // !ReadOnly
	protocol.QueryFinished();

	unpack_files("spacetest1", "testfiles/TestDir1");

	// Start the bbackupd client. Also enable logging with milliseconds, for the same reason:
	std::string daemon_args(bbackupd_args_overridden ? bbackupd_args :
		"-kU -Wnotice -tbbackupd");
	TEST_THAT_OR(StartClient(bbackupd_conf_file, daemon_args), FAIL);

	int block_multiplier = (spec.name() == "s3" ? 1 : 2);

	// TODO FIXME dedent
	{
		// wait for files to be uploaded
		BOX_TRACE("Waiting for all outstanding files to be uploaded...")
		wait_for_sync_end();
		BOX_TRACE("Done. Comparing to check that it worked...")
		TEST_COMPARE_SPECIALISED(spec, Compare_Same);

		// BLOCK
		{
			protocol.Reopen();
			TEST_THAT(check_num_files(fs, 5, 0, 0, 9));
			TEST_THAT(check_num_blocks(protocol, 5 * block_multiplier, 0, 0,
				9 * block_multiplier, 14 * block_multiplier));
			protocol.QueryFinished();
		}

		// Set limit to something very small. 14/28 blocks are used at this point.
		// Set soft limit to 0 to ensure that all deleted files are deleted immediately
		// by housekeeping when it runs.
		TEST_EQUAL(0, spec.control().SetLimit(0, 10 * block_multiplier));
		fs.ReleaseLock();

		// Unpack some more files
		unpack_files("spacetest2", "testfiles/TestDir1");

		// Delete a file and a directory
		TEST_THAT(EMU_UNLINK("testfiles/TestDir1/spacetest/f1") == 0);
#ifdef WIN32
		TEST_THAT(::system("rd /s/q testfiles\\TestDir1\\spacetest\\d7") == 0);
#else
		TEST_THAT(::system("rm -rf testfiles/TestDir1/spacetest/d7") == 0);
#endif

		// The following files should be in the backup directory:
		// 00000001 -d---- 00002 (root)
		// 00000002 -d---- 00002 Test1
		// 00000003 -d---- 00002 Test1/spacetest
		// 00000004 f-X--- 00002 Test1/spacetest/f1
		// 00000005 f----- 00002 Test1/spacetest/f2
		// 00000006 -d---- 00002 Test1/spacetest/d1
		// 00000007 f----- 00002 Test1/spacetest/d1/f3
		// 00000008 f----- 00002 Test1/spacetest/d1/f4
		// 00000009 -d---- 00002 Test1/spacetest/d2
		// 00000009 -d---- 00002 Test1/spacetest/d6
		// 0000000a -d---- 00002 Test1/spacetest/d3
		// 0000000b -d---- 00002 Test1/spacetest/d3/d4
		// 0000000c f----- 00002 Test1/spacetest/d3/d4/f5
		// 0000000d -d---- 00002 Test1/spacetest/d6
		// 0000000d -d---- 00002 Test1/spacetest/d6/d8
		// 0000000d -d---- 00002 Test1/spacetest/d6/d8/f7
		// 0000000e -dX--- 00002 Test1/spacetest/d7
		//
		// root + location + spacetest1 + spacetest2 = 17 files
		// = 34 blocks with raidfile. Of which 2 in deleted files
		// and 18 in directories. Note that f1 and d7 may or may
		// not be deleted yet.
		//
		// The files and dirs from spacetest1 are already on the server
		// (28 blocks). If we set the limit to 20 then the client will
		// notice as soon as it tries to create the new files and dirs
		// from spacetest2. It should still delete f1 and d7, but that
		// won't bring it back under the hard limit, so no files from
		// spacetest2 should be uploaded.

		BOX_TRACE("Waiting for sync for bbackupd to notice that the store is full");
		wait_for_sync_end();
		BOX_TRACE("Sync finished.");

		BOX_TRACE("Compare to check that there are differences");
		TEST_COMPARE_SPECIALISED(spec, Compare_Different);

		// Check that the notify script was run
		TEST_THAT(TestFileExists("testfiles/notifyran.store-full.1"));
		// But only once!
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.2"));

		// We can't guarantee to get in before housekeeping runs, so it's safer
		// (more reliable) to wait for it to finish. But we also need to stop the
		// client first, otherwise it might be connected when housekeeping tries
		// to run, and that would prevent it from running, causing test to fail.
		TEST_THAT_OR(StopClient(), FAIL);

		if(spec.name() == "s3")
		{
			// Housekeeping only runs explicitly, so force it to run now:
			std::string cmd = BBSTOREACCOUNTS " -3 -c " + bbackupd_conf_file +
				" housekeep test";
			TEST_RETURN(system(cmd.c_str()), 0)
		}
		else
		{
			// bbstored will run housekeeping if we wait long enough:
			wait_for_operation(5, "housekeeping to run");
		}

		// BLOCK
		{
			protocol.Reopen();
			std::auto_ptr<BackupStoreDirectory> root_dir =
				ReadDirectory(protocol, BACKUPSTORE_ROOT_DIRECTORY_ID);

			int64_t test_dir_id = SearchDir(*root_dir, "Test1");
			TEST_THAT_OR(test_dir_id, FAIL);
			std::auto_ptr<BackupStoreDirectory> test_dir =
				ReadDirectory(protocol, test_dir_id);

			int64_t spacetest_dir_id = SearchDir(*test_dir, "spacetest");
			TEST_THAT_OR(spacetest_dir_id, FAIL);
			std::auto_ptr<BackupStoreDirectory> spacetest_dir =
				ReadDirectory(protocol, spacetest_dir_id);

			int64_t d2_id = SearchDir(*spacetest_dir, "d2");
			int64_t d6_id = SearchDir(*spacetest_dir, "d6");
			TEST_THAT_OR(d2_id != 0, FAIL);
			TEST_THAT_OR(d6_id != 0, FAIL);
			std::auto_ptr<BackupStoreDirectory> d2_dir =
				ReadDirectory(protocol, d2_id);
			std::auto_ptr<BackupStoreDirectory> d6_dir =
				ReadDirectory(protocol, d6_id);

			// None of the new files should have been uploaded
			TEST_EQUAL(SearchDir(*d2_dir, "f6"), 0);
			TEST_EQUAL(SearchDir(*d6_dir, "d8"), 0);

			// But f1 and d7 should have been deleted.
			TEST_EQUAL(SearchDir(*spacetest_dir, "f1"), 0);
			TEST_EQUAL(SearchDir(*spacetest_dir, "d7"), 0);

			TEST_THAT(check_num_files(fs, 4, 0, 0, 8));
			TEST_THAT(check_num_blocks(protocol, 4 * block_multiplier, 0, 0,
				8 * block_multiplier, 12 * block_multiplier));
			protocol.QueryFinished();
		}
	}

	// Increase the limit again, check that all files are backed up on the
	// next run.
	TEST_EQUAL(0, spec.control().SetLimit(0, 17 * block_multiplier));
	fs.ReleaseLock();

	TEST_THAT_OR(StartClient(bbackupd_conf_file), FAIL);
	wait_for_sync_end();
	TEST_COMPARE_SPECIALISED(spec, Compare_Same);

	TEARDOWN_TEST_SPECIALISED_NO_CHECK(spec);
}

bool test_bbackupd_exclusions(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED_BBSTORED(spec);

	BackupDaemon bbackupd;
	TEST_THAT_OR(prepare_test_with_client_daemon(bbackupd, false, // !do_unpack_files
		false, // !do_start_bbstored
		bbackupd_conf_file),
		FAIL);

	BackupFileSystem& fs(spec.control().GetFileSystem());
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol, true); // ReadOnly
	protocol.QueryFinished();

	TEST_THAT(unpack_files("spacetest1", "testfiles/TestDir1"));
	// Delete a file and a directory
	TEST_THAT(EMU_UNLINK("testfiles/TestDir1/spacetest/f1") == 0);

#ifdef WIN32
	TEST_THAT(::system("rd /s/q testfiles\\TestDir1\\spacetest\\d7") == 0);
#else
	TEST_THAT(::system("rm -rf testfiles/TestDir1/spacetest/d7") == 0);
#endif

	// We need just enough space to upload the initial files (>24 blocks), but not enough to
	// create another directory. That must be the thing that pushes us over the limit:
	int block_multiplier = (spec.name() == "s3" ? 1 : 2);
	TEST_EQUAL(0, spec.control().SetLimit(0, 12 * block_multiplier));
	fs.ReleaseLock();

	// Backup the initial files:
	{
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_THAT(!bbackupd.StorageLimitExceeded());

		// BLOCK
		{
			protocol.Reopen();
			TEST_THAT(check_num_files(fs, 4, 0, 0, 8));
			TEST_THAT(check_num_blocks(protocol, 4 * block_multiplier, 0, 0,
				8 * block_multiplier, 12 * block_multiplier));
			protocol.QueryFinished();
		}
	}

	// Create a directory and then try to run a backup. This should try
	// to create the directory on the server, fail, and catch the error.
	// The directory that we create, spacetest/d6/d8, is included in
	// spacetest2.tgz, so we can ignore this for counting files after we
	// unpack spacetest2.tgz.
	TEST_THAT(::mkdir("testfiles/TestDir1/spacetest/d6/d8", 0755) == 0);
	run_bbackupd_sync_with_logging(bbackupd);
	TEST_THAT(bbackupd.StorageLimitExceeded());

	// BLOCK
	{
		TEST_THAT(unpack_files("spacetest2", "testfiles/TestDir1"));
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_THAT(bbackupd.StorageLimitExceeded());

		// BLOCK
		{
			protocol.Reopen();
			TEST_THAT(check_num_files(fs, 4, 0, 0, 8));
			TEST_THAT(check_num_blocks(protocol, 4 * block_multiplier, 0, 0,
				8 * block_multiplier, 12 * block_multiplier));
			protocol.QueryFinished();
		}
	}

	std::string bbackupd_conf_exclude = (spec.name() == "s3")
		? "testfiles/bbackupd-exclude.s3.conf"
		: "testfiles/bbackupd-exclude.conf";

	// TODO FIXME dedent
	{
		// Start again with a new config that excludes d3 and f2,
		// and hence also d3/d4 and d3/d4/f5. bbackupd should mark
		// them as deleted and housekeeping should later clean up,
		// making space to upload the new files.
		// total required: (13-2-4+3)*2 = 20 blocks

		TEST_THAT(configure_bbackupd(bbackupd, bbackupd_conf_exclude));
		// Should be marked as deleted by this run. Hold onto the
		// BackupClientContext to stop housekeeping from running.
		std::auto_ptr<BackupClientContext> apClientContext =
			bbackupd.RunSyncNow();
		// Housekeeping has not yet deleted the files, so there's not
		// enough space to upload the new ones.
		TEST_THAT(bbackupd.StorageLimitExceeded());

		// Check that the notify script was run
		// TEST_THAT(TestFileExists("testfiles/notifyran.store-full.2"));
		// But only twice!
		// TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.3"));

		// All these should be marked as deleted but not removed by
		// housekeeping yet:
		// f2		excluded
		// d3		excluded
		// d3/d4	excluded
		// d3/d4/f5	excluded
		// Careful with timing here, these files will be removed by
		// housekeeping the next time it runs. We hold onto the client
		// context (and hence an open connection) to stop it from
		// running for now.

		// But we can't do that on Windows, because bbstored only
		// support one simultaneous connection. So we have to hope that
		// housekeeping has run recently enough that it doesn't need to
		// run again when we disconnect.

		if(spec.name() == "s3")
		{
			// We can't access the store simultaneously, even using a read-only context,
			// because we can't lock the cache directory. But also we don't need to,
			// because there's no housekeeping, so just release the context now.
			apClientContext.reset();
		}
		else
		{
#ifdef WIN32
			apClientContext.reset();
#endif
		}

		BOX_INFO("Finding out whether bbackupd marked files as deleted");

		// TODO FIXME dedent
		{
			protocol.Reopen();
			std::auto_ptr<BackupStoreDirectory> rootDir =
				ReadDirectory(protocol);

			int64_t testDirId = SearchDir(*rootDir, "Test1");
			TEST_THAT(testDirId != 0);

			std::auto_ptr<BackupStoreDirectory> Test1_dir =
				ReadDirectory(protocol, testDirId);

			int64_t spacetestDirId = SearchDir(*Test1_dir,
				"spacetest");
			TEST_THAT(spacetestDirId != 0);

			std::auto_ptr<BackupStoreDirectory> spacetest_dir =
				ReadDirectory(protocol, spacetestDirId);

			// these files were deleted before, they should be
			// long gone by now

			TEST_THAT(SearchDir(*spacetest_dir, "f1") == 0);
			TEST_THAT(SearchDir(*spacetest_dir, "d7") == 0);

			// these files have just been deleted, because
			// they are excluded by the new configuration.
			// but housekeeping should not have run yet

			TEST_THAT(test_entry_deleted(*spacetest_dir, "f2"));
			TEST_THAT(test_entry_deleted(*spacetest_dir, "d3"));

			int64_t d3_id = SearchDir(*spacetest_dir, "d3");
			TEST_THAT_OR(d3_id != 0, FAIL);

			std::auto_ptr<BackupStoreDirectory> d3_dir =
				ReadDirectory(protocol, d3_id);
			TEST_THAT(test_entry_deleted(*d3_dir, "d4"));

			int64_t d4_id = SearchDir(*d3_dir, "d4");
			TEST_THAT_OR(d4_id != 0, FAIL);

			std::auto_ptr<BackupStoreDirectory> d4_dir =
				ReadDirectory(protocol, d4_id);
			TEST_THAT(test_entry_deleted(*d4_dir, "f5"));

			// d1/f3 and d1/f4 are the only two files on the
			// server which are not deleted, they use 2 blocks
			// each, the rest is directories and 2 deleted files
			// (f2 and d3/d4/f5)
			TEST_THAT(check_num_files(fs, 2, 0, 2, 8));
			TEST_THAT(check_num_blocks(protocol, 2 * block_multiplier, 0,
				2 * block_multiplier, 8 * block_multiplier, 12 * block_multiplier));

			// Log out.
			protocol.QueryFinished();
		}

		// Release our BackupClientContext and open connection, and
		// force housekeeping to run now.
		apClientContext.reset();
		TEST_THAT(run_housekeeping_and_check_account(fs));

		BOX_INFO("Checking that the files were removed");
		{
			protocol.Reopen();
			std::auto_ptr<BackupStoreDirectory> rootDir = ReadDirectory(protocol);

			int64_t testDirId = SearchDir(*rootDir, "Test1");
			TEST_THAT(testDirId != 0);

			std::auto_ptr<BackupStoreDirectory> Test1_dir =
				ReadDirectory(protocol, testDirId);

			int64_t spacetestDirId = SearchDir(*Test1_dir, "spacetest");
			TEST_THAT(spacetestDirId != 0);

			std::auto_ptr<BackupStoreDirectory> spacetest_dir =
				ReadDirectory(protocol, spacetestDirId);

			TEST_THAT(SearchDir(*spacetest_dir, "f1") == 0);
			TEST_THAT(SearchDir(*spacetest_dir, "f2") == 0);
			TEST_THAT(SearchDir(*spacetest_dir, "d3") == 0);
			TEST_THAT(SearchDir(*spacetest_dir, "d7") == 0);

			// f2, d3, d3/d4 and d3/d4/f5 have been removed.
			// The files were counted as deleted files before, the
			// deleted directories just as directories.
			TEST_THAT(check_num_files(fs, 2, 0, 0, 6));
			TEST_THAT(check_num_blocks(protocol, 2 * block_multiplier, 0, 0,
				6 * block_multiplier, 8 * block_multiplier));

			// Log out.
			protocol.QueryFinished();
		}

		// Need 22 blocks free to upload everything
		TEST_EQUAL(0, spec.control().SetLimit(0, 11 * block_multiplier));
		fs.ReleaseLock();

		// Run another backup, now there should be enough space
		// for everything we want to upload.
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_THAT(!bbackupd.StorageLimitExceeded());

		// Check that the contents of the store are the same
		// as the contents of the disc
		TEST_COMPARE_EXTRA(Compare_Same, ("-c " + bbackupd_conf_exclude).c_str());
		BOX_TRACE("done.");

		// BLOCK
		{
			protocol.Reopen();
			TEST_THAT(check_num_files(fs, 4, 0, 0, 7));
			TEST_THAT(check_num_blocks(protocol, 4 * block_multiplier, 0, 0,
				7 * block_multiplier, 11 * block_multiplier));

			// d2/f6, d6/d8 and d6/d8/f7 are new
			// i.e. 2 new files, 1 new directory

			protocol.QueryFinished();
		}
	}

	TEARDOWN_TEST_SPECIALISED_NO_CHECK(spec);
}

bool test_bbackupd_uploads_files()
{
	SETUP_WITH_BBSTORED();

	// TODO FIXME dedent
	{
		// The files were all unpacked with timestamps in the past,
		// so no delay should be needed to make them eligible to be
		// backed up.
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);
	}

	// Check that no read error has been reported yet
	TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

	TEARDOWN_TEST_BBACKUPD();
}

// Start a bbstored with a test hook that makes it terminate on the first StoreFile command,
// breaking the connection to bbackupd.
class TerminateEarlyHook : public BackupStoreContext::TestHook
{
public:
	int trigger_count;
	TerminateEarlyHook() : trigger_count(0) { }

	virtual std::auto_ptr<BackupProtocolMessage> StartCommand(
		const BackupProtocolMessage& rCommand)
	{
		if (rCommand.GetType() ==
			BackupProtocolStoreFile::TypeID)
		{
			// terminate badly
			trigger_count++;
			THROW_EXCEPTION(ConnectionException,
				TLSReadFailed);
		}
		return std::auto_ptr<BackupProtocolMessage>();
	}
};

// The in-process version of this test is easier to debug, but less realistic, since no sockets
// are used or closed, and so there is no attempt to use a closed connection:
bool test_bbackupd_responds_to_connection_failure_in_process()
{
	SETUP_TEST_BBACKUPD();
	TEST_THAT_OR(unpack_files("test_base"), FAIL);

	std::auto_ptr<BackupProtocolCallable> apClient;

	class MockBackupProtocolLocal : public BackupProtocolLocal2
	{
	public:
		TerminateEarlyHook hook;
		MockBackupProtocolLocal(int32_t AccountNumber,
			const std::string& ConnectionDetails,
			const std::string& AccountRootDir, int DiscSetNumber,
			bool ReadOnly)
		: BackupProtocolLocal2(AccountNumber, ConnectionDetails,
			AccountRootDir, DiscSetNumber, ReadOnly)
		{
			GetContext().SetTestHook(hook);
		}
		virtual ~MockBackupProtocolLocal() { }
	};

	apClient.reset(new MockBackupProtocolLocal(0x01234567, "test", "backup/01234567/",
		0, false));
	MockBackupProtocolLocal& r_mock_client(dynamic_cast<MockBackupProtocolLocal &>(*apClient));

	MockBackupDaemon bbackupd(*apClient);
	TEST_THAT_OR(prepare_test_with_client_daemon(bbackupd, false, false), FAIL);

	TEST_THAT(::system("rm -f testfiles/notifyran.store-full.*") == 0);

	{
		Console& console(Logging::GetConsole());
		Logger::LevelGuard guard(console);

		if (console.GetLevel() < Log::TRACE)
		{
			console.Filter(Log::NOTHING);
		}

		bbackupd.RunSyncNowWithExceptionHandling();
	}

	// Should only have been triggered once
	TEST_EQUAL(1, r_mock_client.hook.trigger_count);

	TEST_THAT(TestFileExists("testfiles/notifyran.backup-error.1"));
	TEST_THAT(!TestFileExists("testfiles/notifyran.backup-error.2"));
	TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.1"));

	TEARDOWN_TEST_BBACKUPD();
}

class MockS3Client : public S3Client
{
public:
	int hook_trigger_count;

	// Constructor with a simulator:
	MockS3Client(HTTPServer* pSimulator, const std::string& rHostName,
		const std::string& rAccessKey, const std::string& rSecretKey)
	: S3Client(pSimulator, rHostName, rAccessKey, rSecretKey),
	  hook_trigger_count(0)
	{ }

	virtual ~MockS3Client() { }

	virtual HTTPResponse PutObject(const std::string& rObjectURI,
		IOStream& rStreamToSend, const char* pContentType = NULL)
	{
		hook_trigger_count++;
		THROW_EXCEPTION_MESSAGE(ConnectionException, SocketWriteError,
			"Simulated server failure (unexpected disconnection)");
	}
};

// The S3 in-process version of this test is quite different to the backupstore version,
// because it's not the BackupStoreContext that we simulate failure of, but the S3Simulator,
// in this case by using an in-process simulator instead of the server running on port 22080.
bool test_bbackupd_responds_to_connection_failure_in_process_s3(RaidAndS3TestSpecs::Specialisation& spec)
{
	ASSERT(spec.name() == "s3");

	SETUP_TEST_BBACKUPD();
	TEST_THAT_OR(unpack_files("test_base"), FAIL);
	TEST_THAT_OR(create_test_account_specialised(spec.name(), spec.control()), FAIL);
	spec.control().GetFileSystem().ReleaseLock();

	S3Simulator simulator;
	simulator.Configure("testfiles/s3simulator.conf");

	Configuration s3config(spec.config().GetSubConfiguration("S3Store"));
	MockS3Client s3client(&simulator,
		s3config.GetKeyValue("HostName"),
		s3config.GetKeyValue("AccessKey"),
		s3config.GetKeyValue("SecretKey"));

	S3BackupFileSystem fs(s3config, s3client);
	BackupStoreContext context(fs, NULL, "S3BackupClient");
	context.SetClientHasAccount();
	BackupProtocolLocal2 protocol(context, S3_FAKE_ACCOUNT_ID, false); // !ReadOnly

	MockBackupDaemon bbackupd(protocol);
	TEST_THAT_OR(prepare_test_with_client_daemon(bbackupd, false, false), FAIL);

	TEST_THAT(::system("rm -f testfiles/notifyran.store-full.*") == 0);

	{
		Console& console(Logging::GetConsole());
		Logger::LevelGuard guard(console);

		if (console.GetLevel() < Log::TRACE)
		{
			console.Filter(Log::NOTHING);
		}

		bbackupd.RunSyncNowWithExceptionHandling();
	}

	// Should only have been triggered once
	TEST_EQUAL(1, s3client.hook_trigger_count);

	TEST_THAT(TestFileExists("testfiles/notifyran.backup-error.1"));
	TEST_THAT(!TestFileExists("testfiles/notifyran.backup-error.2"));
	TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.1"));

	TEARDOWN_TEST_BBACKUPD();
}

class MockS3Simulator : public S3Simulator
{
public:
	// Constructor with a simulator:
	MockS3Simulator() { }
	virtual ~MockS3Simulator() { }
	virtual void HandlePut(HTTPRequest &rRequest, HTTPResponse &rResponse)
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, TerminateWorkerNow,
			"Force the server worker process to terminate, to test client response");
	}
};

// The out-of-process version of this test is more realistic, because it uses and closes real
// sockets, but that also makes it much harder to debug.
//
// This is unusual: we need to pass the specs auto_ptr to this test case, to allow the forked child
// to release it before exiting, to avoid triggering the memory leak detection!
bool test_bbackupd_responds_to_connection_failure_out_of_process(
	RaidAndS3TestSpecs::Specialisation& spec,
	std::auto_ptr<RaidAndS3TestSpecs>& rap_specs
)
{
	// SETUP_TEST_SPECIALISED_BBSTORED(spec);
	SETUP_TEST_SPECIALISED(spec);
	spec.control().GetFileSystem().ReleaseLock();
	// TEST_THAT(setup_test_specialised_bbstored(spec));
	TEST_THAT_OR(::mkdir("testfiles/TestDir1", 0755) == 0, FAIL);
	std::string bbackupd_conf_file = (spec.name() == "s3") ? "testfiles/bbackupd.s3.conf" :
			"testfiles/bbackupd.conf";

#if defined WIN32
	BOX_NOTICE("skipping test on this platform"); // requires fork
#else // !WIN32
	TEST_THAT_OR(unpack_files("test_base"), FAIL);

	std::string spec_name = spec.name();
	if(spec_name == "s3")
	{
		StopSimulator();
		ASSERT(s3simulator_pid == 0);
	}

	BOOST_SCOPE_EXIT(spec_name) {
		if(spec_name == "s3")
		{
			StartSimulator();
		}
	} BOOST_SCOPE_EXIT_END

	int server_pid;

	// TODO FIXME dedent
	{
		TerminateEarlyHook hook;

		// bbstored_args always starts with a space
		if(bbstored_args.length() > 0 &&
			bbstored_args.substr(1).find(' ') != std::string::npos)
		{
			BOX_WARNING("bbstored_args contains multiple arguments, but there is no parser "
				"built in, they will probably not be handled correctly");
		}

		server_pid = fork();
		TEST_THAT_OR(server_pid >= 0, FAIL); // fork() failed

		if (server_pid == 0)
		{
			// in fork child
			TEST_THAT(setsid() != -1);

			// The Daemon must be destroyed before exit(),
			// to avoid memory leaks being reported.
			const char *server_argv[] = {
				"daemon",
				bbstored_args.c_str() + 1,
			};

			if(spec_name == "s3")
			{
				MockS3Simulator simulator;
				simulator.SetDaemonize(false);
				simulator.SetForkPerClient(false);
				simulator.Main("testfiles/s3simulator.conf",
					// Pass argc==1 if bbstored_args_overridden is false, to
					// ignore the unwanted second item in server_argv above:
					bbstored_args_overridden ? 2 : 1, server_argv);
			}
			else
			{
				BackupStoreDaemon bbstored;
				bbstored.SetTestHook(hook);
				bbstored.SetDaemonize(false);
				bbstored.SetForkPerClient(false);
				bbstored.Main("testfiles/bbstored.conf",
					// Pass argc==1 if bbstored_args_overridden is false, to
					// ignore the unwanted second item in server_argv above:
					bbstored_args_overridden ? 2 : 1, server_argv);
			}

			Timers::Cleanup(); // avoid memory leaks
			rap_specs.reset();
			exit(0);
		}

		// in fork parent
		if(spec_name == "s3")
		{
			ASSERT(s3simulator_pid == 0);
			s3simulator_pid = server_pid;
			TEST_EQUAL(s3simulator_pid, WaitForServerStartup("testfiles/s3simulator.pid",
				s3simulator_pid));
		}
		else
		{
			bbstored_pid = server_pid;
			TEST_EQUAL(bbstored_pid, WaitForServerStartup("testfiles/bbstored.pid",
				bbstored_pid));
		}

		TEST_THAT(::system("rm -f testfiles/notifyran.store-full.*") == 0);

		// Ignore SIGPIPE so that when the connection is broken,
		// the daemon doesn't terminate.
		::signal(SIGPIPE, SIG_IGN);

		{
			Console& console(Logging::GetConsole());
			Logger::LevelGuard guard(console);

			if (console.GetLevel() < Log::TRACE)
			{
				console.Filter(Log::NOTHING);
			}

			BackupDaemon bbackupd;
			bbackupd.Configure(bbackupd_conf_file);
			bbackupd.InitCrypto();
			bbackupd.RunSyncNowWithExceptionHandling();
		}

		::signal(SIGPIPE, SIG_DFL);

		TEST_THAT(TestFileExists("testfiles/notifyran.backup-error.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-error.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.1"));
	}

	// It's very important to wait() for the server when using fork, otherwise the zombie never
	// dies and the test fails:
	if(spec.name() == "s3")
	{
		StopSimulator(true); // wait_for_process
	}
	else
	{
		StopServer(true); // wait_for_process
	}
#endif // !WIN32

	TEARDOWN_TEST_SPECIALISED_NO_CHECK(spec);
}

bool test_absolute_symlinks_not_followed_during_restore()
{
	SETUP_WITH_BBSTORED();

#ifdef WIN32
	BOX_NOTICE("skipping test on this platform"); // requires symlinks
#else
	// TODO FIXME dedent
	{
		#define SYM_DIR "testfiles" DIRECTORY_SEPARATOR "TestDir1" \
			DIRECTORY_SEPARATOR "symlink_test"

		TEST_THAT(::mkdir(SYM_DIR, 0777) == 0);
		TEST_THAT(::mkdir(SYM_DIR DIRECTORY_SEPARATOR "a", 0777) == 0);
		TEST_THAT(::mkdir(SYM_DIR DIRECTORY_SEPARATOR "a"
			DIRECTORY_SEPARATOR "subdir", 0777) == 0);
		TEST_THAT(::mkdir(SYM_DIR DIRECTORY_SEPARATOR "b", 0777) == 0);

		FILE* fp = fopen(SYM_DIR DIRECTORY_SEPARATOR "a"
			DIRECTORY_SEPARATOR "subdir"
			DIRECTORY_SEPARATOR "content", "w");
		TEST_THAT(fp != NULL);
		fputs("before\n", fp);
		fclose(fp);

		char buf[PATH_MAX];
		TEST_THAT(getcwd(buf, sizeof(buf)) == buf);
		std::string path = buf;
		// testfiles/TestDir1/symlink_test/a/subdir ->
		// testfiles/TestDir1/symlink_test/b/link
		path += DIRECTORY_SEPARATOR SYM_DIR
			DIRECTORY_SEPARATOR "a"
			DIRECTORY_SEPARATOR "subdir";
		TEST_THAT(symlink(path.c_str(), SYM_DIR
			DIRECTORY_SEPARATOR "b"
			DIRECTORY_SEPARATOR "link") == 0);

		// also test symlink-to-self loop does not break restore
		TEST_THAT(symlink("self", SYM_DIR "/self") == 0);

		wait_for_operation(5, "symlinks to be old enough");
		run_bbackupd_sync_with_logging(bbackupd);

		// Check that the backup was successful, i.e. no differences
		TEST_COMPARE(Compare_Same);

		fp = fopen(SYM_DIR DIRECTORY_SEPARATOR "a"
			DIRECTORY_SEPARATOR "subdir"
			DIRECTORY_SEPARATOR "content", "w");
		TEST_THAT(fp != NULL);
		fputs("after\n", fp);
		fclose(fp);

		TEST_THAT(chmod(SYM_DIR, 0) == 0);

		// check that we can restore it
		TEST_THAT(bbackupquery("-Wwarning \"restore Test1 testfiles/restore-symlink\""));

		// make it accessible again
		TEST_THAT(chmod(SYM_DIR, 0755) == 0);

		// check that the original file was not overwritten
		FileStream fs(SYM_DIR "/a/subdir/content");
		IOStreamGetLine gl(fs);
		std::string line = gl.GetLine(false);
		TEST_THAT(line != "before");
		TEST_EQUAL("after", line);

		#undef SYM_DIR
	}
#endif

	TEARDOWN_TEST_BBACKUPD();
}

// Testing that nonexistent locations are backed up if they are created later
bool test_initially_missing_locations_are_not_forgotten()
{
	SETUP_WITH_BBSTORED();

	// ensure that the directory does not exist at the start
	TEST_THAT(!FileExists("testfiles/TestDir2"));
	TEST_THAT(configure_bbackupd(bbackupd, "testfiles/bbackupd-temploc.conf"));

	// BLOCK
	{
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-start.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-start.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-ok.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-finish.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-finish.2"));

		bbackupd.RunSyncNowWithExceptionHandling();
		TEST_COMPARE(Compare_Same);

		TEST_THAT( TestFileExists("testfiles/notifyran.backup-start.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-start.2"));
		TEST_THAT( TestFileExists("testfiles/notifyran.read-error.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-ok.1"));
		TEST_THAT( TestFileExists("testfiles/notifyran.backup-finish.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-finish.2"));

		// Did it actually get created? Should not have been!
		TEST_THAT_OR(!search_for_file("Test2"), FAIL);
	}

	// create the location directory and unpack some files into it
	TEST_THAT(::mkdir("testfiles/TestDir2", 0777) == 0);
	TEST_THAT(unpack_files("spacetest1", "testfiles/TestDir2"));

	// check that the files are backed up now
	bbackupd.RunSyncNowWithExceptionHandling();
	TEST_COMPARE(Compare_Same);

	TEST_THAT( TestFileExists("testfiles/notifyran.backup-start.2"));
	TEST_THAT(!TestFileExists("testfiles/notifyran.backup-start.3"));
	TEST_THAT( TestFileExists("testfiles/notifyran.read-error.1"));
	TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.2"));
	TEST_THAT( TestFileExists("testfiles/notifyran.backup-ok.1"));
	TEST_THAT(!TestFileExists("testfiles/notifyran.backup-ok.2"));
	TEST_THAT( TestFileExists("testfiles/notifyran.backup-finish.2"));
	TEST_THAT(!TestFileExists("testfiles/notifyran.backup-finish.3"));

	// BLOCK
	TEST_THAT_OR(search_for_file("Test2"), FAIL);
	TEARDOWN_TEST_BBACKUPD();
}

bool test_redundant_locations_deleted_on_time()
{
	SETUP_WITH_BBSTORED();

	// create the location directory and unpack some files into it
	TEST_THAT(::mkdir("testfiles/TestDir2", 0777) == 0);
	TEST_THAT(unpack_files("spacetest1", "testfiles/TestDir2"));

	// Use a daemon with the TestDir2 location configured to back it up
	// to the server.
	{
		TEST_THAT(configure_bbackupd(bbackupd, "testfiles/bbackupd-temploc.conf"));
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);
	}

	// Now use a daemon with no temporary location, which should delete
	// it after 10 seconds
	{
		TEST_THAT(configure_bbackupd(bbackupd, "testfiles/bbackupd.conf"));

		// Initial run to start the countdown to destruction
		run_bbackupd_sync_with_logging(bbackupd);

		// Not deleted yet!
		TEST_THAT(search_for_file("Test2"));

		wait_for_operation(9, "just before Test2 should be deleted");
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_THAT(search_for_file("Test2"));

		// Now wait until after it should be deleted
		wait_for_operation(2, "just after Test2 should be deleted");
		run_bbackupd_sync_with_logging(bbackupd);

		TEST_THAT(search_for_file("Test2"));
		std::auto_ptr<BackupProtocolCallable> client = connect_and_login(
			sTlsContext, 0 /* read-write */);
		std::auto_ptr<BackupStoreDirectory> root_dir =
			ReadDirectory(*client, BACKUPSTORE_ROOT_DIRECTORY_ID);
		TEST_THAT(test_entry_deleted(*root_dir, "Test2"));
	}

	TEARDOWN_TEST_BBACKUPD();
}

// Check that read-only directories and their contents can be restored.
bool test_read_only_dirs_can_be_restored()
{
	SETUP_WITH_BBSTORED();

	// TODO FIXME dedent
	{
		{
			#ifdef WIN32
				TEST_THAT(::system("attrib +r testfiles\\TestDir1\\x1")
					== 0);
			#else
				TEST_THAT(chmod("testfiles/TestDir1/x1",
					0555) == 0);
			#endif

			run_bbackupd_sync_with_logging(bbackupd);
			TEST_COMPARE_EXTRA(Compare_Same, "", "-cEQ Test1 testfiles/TestDir1");

			// check that we can restore it
			TEST_THAT(restore("Test1", "testfiles/restore1"));
			TEST_COMPARE_EXTRA(Compare_Same, "", "-cEQ Test1 testfiles/restore1");

			// Try a restore with just the remote directory name,
			// check that it uses the same name in the local
			// directory.
			TEST_THAT(::mkdir("testfiles/restore-test", 0700) == 0);
			TEST_THAT(bbackupquery("\"lcd testfiles/restore-test\" "
				"\"restore Test1\""));
			TEST_COMPARE_EXTRA(Compare_Same, "", "-cEQ Test1 "
				"testfiles/restore-test/Test1");

			// put the permissions back to sensible values
			#ifdef WIN32
				TEST_THAT(::system("attrib -r testfiles\\TestDir1\\x1")
					== 0);
				TEST_THAT(::system("attrib -r testfiles\\restore1\\x1")
					== 0);
				TEST_THAT(::system("attrib -r testfiles\\restore-test\\"
					"Test1\\x1") == 0);
			#else
				TEST_THAT(chmod("testfiles/TestDir1/x1",
					0755) == 0);
				TEST_THAT(chmod("testfiles/restore1/x1",
					0755) == 0);
				TEST_THAT(chmod("testfiles/restore-test/Test1/x1",
					0755) == 0);
			#endif
		}
	}

	TEARDOWN_TEST_BBACKUPD();
}

#ifdef WIN32
// Check that filenames in UTF-8 can be backed up
bool test_unicode_filenames_can_be_backed_up()
{
	SETUP_WITH_BBSTORED();

	// TODO FIXME dedent
	{
		// We have no guarantee that a random Unicode string can be
		// represented in the user's character set, so we go the
		// other way, taking three random characters from the
		// character set and converting them to Unicode. Unless the
		// console codepage is CP_UTF8, in which case our random
		// characters are not valid, so we use the UTF8 version
		// of them instead.
		//
		// We hope that these characters are valid in most
		// character sets, but they probably are not in multibyte
		// character sets such as Shift-JIS, GB2312, etc. This test
		// will probably fail if your system locale is set to
		// Chinese, Japanese, etc. where one of these character
		// sets is used by default. You can check the character
		// set for your system in Control Panel -> Regional
		// Options -> General -> Language Settings -> Set Default
		// (System Locale). Because bbackupquery converts from
		// system locale to UTF-8 via the console code page
		// (which you can check from the Command Prompt with "chcp")
		// they must also be valid in your code page (850 for
		// Western Europe).
		//
		// In ISO-8859-1 (Danish locale) they are three Danish
		// accented characters, which are supported in code page
		// 850. Depending on your locale, YYMV (your yak may vomit).

		std::string foreignCharsNative = (GetConsoleCP() == CP_UTF8)
			? "\xc3\xa6\xc3\xb8\xc3\xa5" : "\x91\x9b\x86";
		std::string foreignCharsUnicode;
		TEST_THAT(ConvertConsoleToUtf8(foreignCharsNative.c_str(),
			foreignCharsUnicode));

		std::string basedir("testfiles/TestDir1");
		std::string dirname("test" + foreignCharsUnicode + "testdir");
		std::string dirpath(basedir + "/" + dirname);
		TEST_THAT(mkdir(dirpath.c_str(), 0) == 0);

		std::string filename("test" + foreignCharsUnicode + "testfile");
		std::string filepath(dirpath + "/" + filename);

		char cwdbuf[1024];
		TEST_THAT(getcwd(cwdbuf, sizeof(cwdbuf)) == cwdbuf);
		std::string cwd = cwdbuf;

		// Test that our emulated chdir() works properly
		// with relative and absolute paths
		TEST_THAT(::chdir(dirpath.c_str()) == 0);
		TEST_THAT(::chdir("../../..") == 0);
		TEST_THAT(::chdir(cwd.c_str()) == 0);

		// Check that it can be converted to the system encoding
		// (which is what is needed on the command line)
		std::string systemDirName;
		TEST_THAT(ConvertEncoding(dirname.c_str(), CP_UTF8,
			systemDirName, CP_ACP));

		std::string systemFileName;
		TEST_THAT(ConvertEncoding(filename.c_str(), CP_UTF8,
			systemFileName, CP_ACP));

		// Check that it can be converted to the console encoding
		// (which is what we will see in the output)
		std::string consoleDirName;
		TEST_THAT(ConvertUtf8ToConsole(dirname.c_str(),
			consoleDirName));

		std::string consoleFileName;
		TEST_THAT(ConvertUtf8ToConsole(filename.c_str(),
			consoleFileName));

		// test that bbackupd will let us lcd into the local
		// directory using a relative path, and back out
		TEST_THAT(bbackupquery("\"lcd testfiles/TestDir1/" +
			systemDirName + "\" \"lcd ..\""));

		// and using an absolute path
		TEST_THAT(bbackupquery("\"lcd " + cwd + "/testfiles/" +
			"TestDir1/" + systemDirName + "\"  \"lcd ..\""));

		{
			FileStream fs(filepath.c_str(), O_CREAT | O_RDWR);

			std::string data("hello world\n");
			fs.Write(data.c_str(), data.size());
			TEST_EQUAL_LINE(12, fs.GetPosition(),
				"FileStream position");
			fs.Close();

			// Set modtime back in time to allow immediate backup
			struct timeval times[2] = {};
			times[1].tv_sec = 1000000000;
			TEST_THAT(emu_utimes(filepath.c_str(), times) == 0);
		}

		run_bbackupd_sync_with_logging(bbackupd);

		// Compare to check that the file was uploaded
		TEST_COMPARE(Compare_Same);

		// Check that we can find it in directory listing
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(sTlsContext, 0);

			std::auto_ptr<BackupStoreDirectory> dir = ReadDirectory(
				*client);

			int64_t baseDirId = SearchDir(*dir, "Test1");
			TEST_THAT(baseDirId != 0);
			dir = ReadDirectory(*client, baseDirId);

			int64_t testDirId = SearchDir(*dir, dirname.c_str());
			TEST_THAT(testDirId != 0);
			dir = ReadDirectory(*client, testDirId);

			TEST_THAT(SearchDir(*dir, filename.c_str()) != 0);
			// Log out
			client->QueryFinished();
		}

		// Check that bbackupquery shows the dir in console encoding
		std::string command = BBACKUPQUERY " -Wwarning "
			"-c testfiles/bbackupd.conf "
			"-q \"list Test1\" quit";
		pid_t bbackupquery_pid;
		std::auto_ptr<IOStream> queryout;
		queryout = LocalProcessStream(command.c_str(),
			bbackupquery_pid);
		TEST_THAT(queryout.get() != NULL);
		TEST_THAT(bbackupquery_pid != -1);

		IOStreamGetLine reader(*queryout);
		bool found = false;
		while (!reader.IsEOF())
		{
			std::string line = reader.GetLine(false); // !preprocess
			if (line.find(consoleDirName) != std::string::npos)
			{
				found = true;
			}
		}
		TEST_THAT(!(queryout->StreamDataLeft()));
		TEST_THAT(reader.IsEOF());
		TEST_THAT(found);
		queryout->Close();

		// Check that bbackupquery can list the dir when given
		// on the command line in system encoding, and shows
		// the file in console encoding
		command = BBACKUPQUERY " -c testfiles/bbackupd.conf "
			"-Wwarning \"list Test1/" + systemDirName + "\" quit";
		queryout = LocalProcessStream(command.c_str(),
			bbackupquery_pid);
		TEST_THAT(queryout.get() != NULL);
		TEST_THAT(bbackupquery_pid != -1);

		IOStreamGetLine reader2(*queryout);
		found = false;
		while (!reader2.IsEOF())
		{
			std::string line = reader2.GetLine(false); // !preprocess
			if (line.find(consoleFileName) != std::string::npos)
			{
				found = true;
			}
		}
		TEST_THAT(!(queryout->StreamDataLeft()));
		TEST_THAT(reader2.IsEOF());
		TEST_THAT(found);
		queryout->Close();

		// Check that bbackupquery can compare the dir when given
		// on the command line in system encoding.
		TEST_COMPARE_EXTRA(Compare_Same, "", "-cEQ Test1/" + systemDirName +
			" testfiles/TestDir1/" + systemDirName);

		// Check that bbackupquery can restore the dir when given
		// on the command line in system encoding.
		TEST_THAT(restore("Test1/" + systemDirName,
			"testfiles/restore-" + systemDirName));

		// Compare to make sure it was restored properly.
		TEST_COMPARE_EXTRA(Compare_Same, "", "-cEQ Test1/" + systemDirName +
			" testfiles/restore-" + systemDirName);

		std::string fileToUnlink = "testfiles/restore-" +
			dirname + "/" + filename;
		TEST_THAT(EMU_UNLINK(fileToUnlink.c_str()) == 0);

		// Check that bbackupquery can get the file when given
		// on the command line in system encoding.
		TEST_THAT(bbackupquery("\"get Test1/" + systemDirName + "/" +
			systemFileName + " " + "testfiles/restore-" +
			systemDirName + "/" + systemFileName + "\""));

		// And after changing directory to a relative path
		TEST_THAT(bbackupquery(
			"\"lcd testfiles\" "
			"\"cd Test1/" + systemDirName + "\" " +
			"\"get " + systemFileName + "\""));

		// cannot overwrite a file that exists, so delete it
		std::string tmp = "testfiles/" + filename;
		TEST_THAT(EMU_UNLINK(tmp.c_str()) == 0);

		// And after changing directory to an absolute path
		TEST_THAT(bbackupquery(
			"\"lcd " + cwd + "/testfiles\" "
			"\"cd Test1/" + systemDirName + "\" " +
			"\"get " + systemFileName + "\""));

		// Compare to make sure it was restored properly. The Get
		// command does restore attributes, so we don't need to
		// specify the -A option for this to succeed.
		TEST_COMPARE_EXTRA(Compare_Same, "", "-cEQ Test1/" + systemDirName +
			" testfiles/restore-" + systemDirName);

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));
	}

	TEARDOWN_TEST_BBACKUPD();
}
#endif // WIN32

bool test_sync_allow_script_can_pause_backup()
{
	SETUP_WITH_BBSTORED();
	TEST_THAT(StartClient());

	// TODO FIXME dedent
	{
		{
			wait_for_sync_end();
			// we now have 3 seconds before bbackupd
			// runs the SyncAllowScript again.

			const char* sync_control_file = "testfiles"
				DIRECTORY_SEPARATOR "syncallowscript.control";
			int fd = open(sync_control_file,
				O_CREAT | O_EXCL | O_WRONLY, 0700);
			if (fd <= 0)
			{
				perror(sync_control_file);
			}
			TEST_THAT(fd > 0);

			const char* control_string = "10\n";
			TEST_THAT(write(fd, control_string,
				strlen(control_string)) ==
				(int)strlen(control_string));
			close(fd);

			// this will pause backups, bbackupd will check
			// every 10 seconds to see if they are allowed again.

			const char* new_test_file = "testfiles"
				DIRECTORY_SEPARATOR "TestDir1"
				DIRECTORY_SEPARATOR "Added_During_Pause";
			fd = open(new_test_file,
				O_CREAT | O_EXCL | O_WRONLY, 0700);
			if (fd <= 0)
			{
				perror(new_test_file);
			}
			TEST_THAT(fd > 0);
			close(fd);

			struct stat st;

			// next poll should happen within the next
			// 5 seconds (normally about 3 seconds)

			wait_for_operation(1, "2 seconds before next run");
			TEST_THAT(stat("testfiles" DIRECTORY_SEPARATOR
				"syncallowscript.notifyran.1", &st) != 0);
			wait_for_operation(4, "2 seconds after run");
			TEST_THAT(stat("testfiles" DIRECTORY_SEPARATOR
				"syncallowscript.notifyran.1", &st) == 0);
			TEST_THAT(stat("testfiles" DIRECTORY_SEPARATOR
				"syncallowscript.notifyran.2", &st) != 0);

			// next poll should happen within the next
			// 10 seconds (normally about 8 seconds)

			wait_for_operation(6, "2 seconds before next run");
			TEST_THAT(stat("testfiles" DIRECTORY_SEPARATOR
				"syncallowscript.notifyran.2", &st) != 0);
			wait_for_operation(4, "2 seconds after run");
			TEST_THAT(stat("testfiles" DIRECTORY_SEPARATOR
				"syncallowscript.notifyran.2", &st) == 0);

			// bbackupquery compare might take a while
			// on slow machines, so start the timer now
			long start_time = time(NULL);

			// check that no backup has run (compare fails)
			TEST_COMPARE(Compare_Different);

			TEST_THAT(EMU_UNLINK(sync_control_file) == 0);
			wait_for_sync_start();
			long end_time = time(NULL);
			long wait_time = end_time - start_time + 2;

			// should be about 10 seconds
			if (wait_time < 8 || wait_time > 12)
			{
				printf("Waited for %ld seconds, should have "
					"been %s", wait_time, control_string);
			}

			TEST_THAT(wait_time >= 8);
			TEST_THAT(wait_time <= 12);

			wait_for_sync_end();

			// check that backup has run (compare succeeds)
			TEST_COMPARE(Compare_Same);
		}

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));
	}

	TEARDOWN_TEST_BBACKUPD();
}

// Delete file and update another, create symlink.
bool test_delete_update_and_symlink_files()
{
	SETUP_WITH_BBSTORED();

	run_bbackupd_sync_with_logging(bbackupd);

	// TODO FIXME dedent
	{
		// Delete a file
		TEST_THAT(EMU_UNLINK("testfiles/TestDir1/x1/dsfdsfs98.fd") == 0);

		#ifndef WIN32
			// New symlink
			TEST_THAT(::symlink("does-not-exist",
				"testfiles/TestDir1/symlink-to-dir") == 0);
		#endif

		// Update a file (will be uploaded as a diff)
		{
			// Check that the file is over the diffing
			// threshold in the bbackupd.conf file
			TEST_THAT(TestGetFileSize("testfiles/TestDir1/f45.df")
				> 1024);

			// Add a bit to the end
			FILE *f = ::fopen("testfiles/TestDir1/f45.df", "a");
			TEST_THAT(f != 0);
			::fprintf(f, "EXTRA STUFF");
			::fclose(f);
			TEST_THAT(TestGetFileSize("testfiles/TestDir1/f45.df")
				> 1024);
		}

		// wait long enough for new files to be old enough to backup
		wait_for_operation(5, "new files to be old enough");

		run_bbackupd_sync_with_logging(bbackupd);

		// compare to make sure that it worked
		TEST_COMPARE(Compare_Same);

		// Try a quick compare, just for fun
		TEST_COMPARE_EXTRA(Compare_Same, "", "-acqQ");
	}

	TEARDOWN_TEST_BBACKUPD();
}

bool move_store_info_file_away(RaidAndS3TestSpecs::Specialisation& spec)
{
	BackupFileSystem& fs(spec.control().GetFileSystem());
	fs.GetLock();
	bool success = true;

	if(spec.name() == "s3")
	{
		TEST_THAT_OR(::rename("testfiles/store/subdir/boxbackup.info",
			"testfiles/store/subdir/boxbackup.info.bak") == 0, success = false);
	}
	else
	{
		TEST_THAT_OR(::rename("testfiles/0_0/backup/01234567/info.rf",
			"testfiles/0_0/backup/01234567/info.rf.bak") == 0, success = false);
		TEST_THAT_OR(::rename("testfiles/0_1/backup/01234567/info.rf",
			"testfiles/0_1/backup/01234567/info.rf.bak") == 0, success = false);
		TEST_THAT_OR(::rename("testfiles/0_2/backup/01234567/info.rf",
			"testfiles/0_2/backup/01234567/info.rf.bak") == 0, success = false);
	}

	fs.ReleaseLock();
	return success;
}

bool move_store_info_file_back(RaidAndS3TestSpecs::Specialisation& spec)
{
	BackupFileSystem& fs(spec.control().GetFileSystem());
	fs.GetLock();
	bool success = true;

	if(spec.name() == "s3")
	{
		TEST_THAT_OR(::rename("testfiles/store/subdir/boxbackup.info.bak",
			"testfiles/store/subdir/boxbackup.info") == 0, success = false);
	}
	else
	{
		TEST_THAT_OR(::rename("testfiles/0_0/backup/01234567/info.rf.bak",
			"testfiles/0_0/backup/01234567/info.rf") == 0, success = false);
		TEST_THAT_OR(::rename("testfiles/0_1/backup/01234567/info.rf.bak",
			"testfiles/0_1/backup/01234567/info.rf") == 0, success = false);
		TEST_THAT_OR(::rename("testfiles/0_2/backup/01234567/info.rf.bak",
			"testfiles/0_2/backup/01234567/info.rf") == 0, success = false);
	}

	fs.ReleaseLock();
	return success;
}

// Check that store errors are reported neatly.
bool test_store_error_reporting(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED_BBSTORED(spec);

	// Temporarily enable timestamp logging, to help debug race conditions causing
	// test failures:
	Logger::LevelGuard temporary_verbosity(Logging::GetConsole(), Log::TRACE);
	Console::SettingsGuard save_old_settings;
	Console::SetShowTime(true);
	Console::SetShowTimeMicros(true);

	// Start the bbackupd client. Also enable logging with milliseconds, for the same reason:
	std::string daemon_args(bbackupd_args_overridden ? bbackupd_args :
		"-kU -Wnotice -tbbackupd");
	TEST_THAT_OR(StartClient(bbackupd_conf_file, daemon_args), FAIL);

	wait_for_sync_end();

	// TODO FIXME dedent
	{
		// Break the store. We need a write lock on the account
		// while we do this, otherwise housekeeping might be running
		// and might rewrite the info files when it finishes,
		// undoing our breakage.
		TEST_THAT(move_store_info_file_away(spec));

		// Create a file to trigger an upload
		{
			int fd1 = open("testfiles/TestDir1/force-upload",
				O_CREAT | O_EXCL | O_WRONLY, 0700);
			TEST_THAT(fd1 > 0);
			TEST_THAT(write(fd1, "just do it", 10) == 10);
			TEST_THAT(close(fd1) == 0);
		}

		// 4 seconds should really be enough, but AppVeyor is sometimes slow.
		wait_for_operation(8, "bbackupd to try to access the store");

		// Check that an error was reported just once
		TEST_THAT(TestFileExists("testfiles/notifyran.backup-error.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-error.2"));
		// Now kill bbackupd and start one that's running in
		// snapshot mode, check that it automatically syncs after
		// an error, without waiting for another sync command.
		TEST_THAT(StopClient());
		TEST_THAT(StartClient((spec.name() == "s3") ?
			"testfiles/bbackupd-snapshot.s3.conf" : "testfiles/bbackupd-snapshot.conf",
			daemon_args));

		sync_and_wait();
		box_time_t last_run_finish_time = GetCurrentBoxTime();

		// Check that the error was reported once more
		TEST_THAT(TestFileExists("testfiles/notifyran.backup-error.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-error.3"));

		// Fix the store (so that bbackupquery compare works)
		TEST_THAT(move_store_info_file_back(spec));

		// Check that we DO get errors on compare (cannot do this
		// until after we fix the store, which creates a race)
		TEST_COMPARE_SPECIALISED(spec, Compare_Different);

		// Test initial state
		TEST_THAT(!TestFileExists("testfiles/"
			"notifyran.backup-start.wait-snapshot.1"));

		// Set a tag for the notify script to distinguish from
		// previous runs.
		{
			int fd1 = open("testfiles/notifyscript.tag",
				O_CREAT | O_EXCL | O_WRONLY, 0700);
			TEST_THAT(fd1 > 0);
			TEST_THAT(write(fd1, "wait-snapshot", 13) == 13);
			TEST_THAT(close(fd1) == 0);
		}

		// bbackupd should pause for BACKUP_ERROR_RETRY_SECONDS (plus
		// a random delay of up to mUpdateStoreInterval/64 or 0.05
		// extra seconds) from last_run_finish_time, so check that it
		// hasn't run just before this time
		wait_for_operation(BACKUP_ERROR_DELAY_SHORTENED - 1 -
			BoxTimeToMilliSeconds(GetCurrentBoxTime() - last_run_finish_time) / 1000.0,
			"just before bbackupd recovers");
		TEST_THAT(!TestFileExists("testfiles/"
			"notifyran.backup-start.wait-snapshot.1"));

		// Should not have backed up, should still get errors
		TEST_COMPARE_SPECIALISED(spec, Compare_Different);

		// Wait another 3 seconds, bbackup should have run (allowing 4 seconds for it to
		// complete):
		wait_for_operation(7, "bbackupd to recover");
		TEST_THAT(TestFileExists("testfiles/"
			"notifyran.backup-start.wait-snapshot.1"));

		// Check that it did get uploaded, and we have no more errors
		TEST_COMPARE_SPECIALISED(spec, Compare_Same);

		TEST_THAT(EMU_UNLINK("testfiles/notifyscript.tag") == 0);

		// Stop the snapshot bbackupd
		TEST_THAT(StopClient());

		// Break the store again
		TEST_THAT(move_store_info_file_away(spec));

		// Modify a file to trigger an upload
		{
			int fd1 = open("testfiles/TestDir1/force-upload",
				O_WRONLY, 0700);
			TEST_THAT(fd1 > 0);
			TEST_THAT(write(fd1, "and again", 9) == 9);
			TEST_THAT(close(fd1) == 0);
		}

		// Restart bbackupd in automatic mode
		TEST_THAT_OR(StartClient(bbackupd_conf_file, daemon_args), FAIL);
		sync_and_wait();
		last_run_finish_time = GetCurrentBoxTime();

		// Fix the store again
		TEST_THAT(move_store_info_file_back(spec));

		// Check that we DO get errors on compare (cannot do this
		// until after we fix the store, which creates a race)
		TEST_COMPARE_SPECIALISED(spec, Compare_Different);

		// Test initial state
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-start.wait-automatic.1"));

		// Set a tag for the notify script to distinguish from
		// previous runs.
		{
			int fd1 = open("testfiles/notifyscript.tag",
				O_CREAT | O_EXCL | O_WRONLY, 0700);
			TEST_THAT(fd1 > 0);
			TEST_THAT(write(fd1, "wait-automatic", 14) == 14);
			TEST_THAT(close(fd1) == 0);
		}

		// bbackupd should pause for BACKUP_ERROR_RETRY_SECONDS (plus
		// a random delay of up to mUpdateStoreInterval/64 or 0.05
		// extra seconds) from last_run_finish_time, so check that it
		// hasn't run just before this time
		wait_for_operation(BACKUP_ERROR_DELAY_SHORTENED - 1 -
			BoxTimeToMilliSeconds(GetCurrentBoxTime() - last_run_finish_time) / 1000.0,
			"just before bbackupd recovers");
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-start.wait-automatic.1"));

		// Should not have backed up, should still get errors
		TEST_COMPARE_SPECIALISED(spec, Compare_Different);

		// wait another 3 seconds, bbackup should have run
		wait_for_operation(3, "bbackupd to recover");
		TEST_THAT(TestFileExists("testfiles/notifyran.backup-start.wait-automatic.1"));

		// Check that it did get uploaded, and we have no more errors
		TEST_COMPARE_SPECIALISED(spec, Compare_Same);

		TEST_THAT(EMU_UNLINK("testfiles/notifyscript.tag") == 0);
	}

	TEARDOWN_TEST_SPECIALISED_NO_CHECK(spec);
}

bool test_change_file_to_symlink_and_back()
{
	SETUP_WITH_BBSTORED();

#ifndef WIN32
		// New symlink
		TEST_THAT(::symlink("does-not-exist",
			"testfiles/TestDir1/symlink-to-dir") == 0);
#endif

	run_bbackupd_sync_with_logging(bbackupd);

	// TODO FIXME dedent
	{
		// Bad case: delete a file/symlink, replace it with a directory.
		// Replace symlink with directory, add new directory.

#ifndef WIN32
		TEST_THAT(EMU_UNLINK("testfiles/TestDir1/symlink-to-dir") == 0);
#endif

		TEST_THAT(::mkdir("testfiles/TestDir1/symlink-to-dir", 0755) == 0);
		TEST_THAT(::mkdir("testfiles/TestDir1/x1/dir-to-file", 0755) == 0);

		// NOTE: create a file within the directory to
		// avoid deletion by the housekeeping process later

#ifndef WIN32
		TEST_THAT(::symlink("does-not-exist",
			"testfiles/TestDir1/x1/dir-to-file/contents") == 0);
#endif

		wait_for_operation(5, "files to be old enough");
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);

		// And the inverse, replace a directory with a file/symlink

#ifndef WIN32
		TEST_THAT(EMU_UNLINK("testfiles/TestDir1/x1/dir-to-file/contents") == 0);
#endif

		TEST_THAT(::rmdir("testfiles/TestDir1/x1/dir-to-file") == 0);

#ifndef WIN32
		TEST_THAT(::symlink("does-not-exist",
			"testfiles/TestDir1/x1/dir-to-file") == 0);
#endif

		wait_for_operation(5, "files to be old enough");
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);

		// And then, put it back to how it was before.
		BOX_INFO("Replace symlink with directory (which was a symlink)");

#ifndef WIN32
		TEST_THAT(EMU_UNLINK("testfiles/TestDir1/x1/dir-to-file") == 0);
#endif

		TEST_THAT(::mkdir("testfiles/TestDir1/x1/dir-to-file",
			0755) == 0);

#ifndef WIN32
		TEST_THAT(::symlink("does-not-exist",
			"testfiles/TestDir1/x1/dir-to-file/contents2") == 0);
#endif

		wait_for_operation(5, "files to be old enough");
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);

		// And finally, put it back to how it was before
		// it was put back to how it was before
		// This gets lots of nasty things in the store with
		// directories over other old directories.

#ifndef WIN32
		TEST_THAT(EMU_UNLINK("testfiles/TestDir1/x1/dir-to-file/contents2") == 0);
#endif

		TEST_THAT(::rmdir("testfiles/TestDir1/x1/dir-to-file") == 0);

#ifndef WIN32
		TEST_THAT(::symlink("does-not-exist", "testfiles/TestDir1/x1/dir-to-file") == 0);
#endif

		wait_for_operation(5, "files to be old enough");
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN_TEST_BBACKUPD();
}

bool test_file_rename_tracking()
{
	SETUP_WITH_BBSTORED();
	run_bbackupd_sync_with_logging(bbackupd);

	// TODO FIXME dedent
	{
		// rename an untracked file over an existing untracked file
		BOX_INFO("Rename over existing untracked file");
		int fd1 = open("testfiles/TestDir1/untracked-1", O_CREAT | O_EXCL | O_WRONLY, 0700);
		int fd2 = open("testfiles/TestDir1/untracked-2", O_CREAT | O_EXCL | O_WRONLY, 0700);
		TEST_THAT(fd1 > 0);
		TEST_THAT(fd2 > 0);
		TEST_THAT(write(fd1, "hello", 5) == 5);
		TEST_THAT(close(fd1) == 0);
		safe_sleep(1);
		TEST_THAT(write(fd2, "world", 5) == 5);
		TEST_THAT(close(fd2) == 0);
		TEST_THAT(TestFileExists("testfiles/TestDir1/untracked-1"));
		TEST_THAT(TestFileExists("testfiles/TestDir1/untracked-2"));

		// back up both files
		wait_for_operation(5, "untracked files to be old enough");
		run_bbackupd_sync_with_logging(bbackupd);

		BackupProtocolLocal2 protocol(0x01234567, "test",
			"backup/01234567/", 0, false);
		TEST_COMPARE_LOCAL(Compare_Same, protocol);
		protocol.QueryFinished();

#ifdef WIN32
		TEST_THAT(EMU_UNLINK("testfiles/TestDir1/untracked-2") == 0);
#endif

		TEST_THAT(::rename("testfiles/TestDir1/untracked-1",
			"testfiles/TestDir1/untracked-2") == 0);
		TEST_THAT(!TestFileExists("testfiles/TestDir1/untracked-1"));
		TEST_THAT( TestFileExists("testfiles/TestDir1/untracked-2"));

		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);

		// case which went wrong: rename a tracked file over an
		// existing tracked file
		BOX_INFO("Rename over existing tracked file");
		fd1 = open("testfiles/TestDir1/tracked-1", O_CREAT | O_EXCL | O_WRONLY, 0700);
		fd2 = open("testfiles/TestDir1/tracked-2", O_CREAT | O_EXCL | O_WRONLY, 0700);
		TEST_THAT(fd1 > 0);
		TEST_THAT(fd2 > 0);
		char buffer[1024];
		TEST_THAT(write(fd1, "hello", 5) == 5);
		TEST_THAT(write(fd1, buffer, sizeof(buffer)) == sizeof(buffer));
		TEST_THAT(close(fd1) == 0);
		safe_sleep(1);
		TEST_THAT(write(fd2, "world", 5) == 5);
		TEST_THAT(write(fd2, buffer, sizeof(buffer)) == sizeof(buffer));
		TEST_THAT(close(fd2) == 0);
		TEST_THAT(TestFileExists("testfiles/TestDir1/tracked-1"));
		TEST_THAT(TestFileExists("testfiles/TestDir1/tracked-2"));

		// wait for them to be old enough to back up
		wait_for_operation(5, "tracked files to be old enough");
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);

#ifdef WIN32
		TEST_THAT(EMU_UNLINK("testfiles/TestDir1/tracked-2") == 0);
#endif

		TEST_THAT(::rename("testfiles/TestDir1/tracked-1",
			"testfiles/TestDir1/tracked-2") == 0);
		TEST_THAT(!TestFileExists("testfiles/TestDir1/tracked-1"));
		TEST_THAT( TestFileExists("testfiles/TestDir1/tracked-2"));

		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);

		// case which went wrong: rename a tracked file
		// over a deleted file
		BOX_INFO("Rename an existing file over a deleted file");
		TEST_THAT(EMU_UNLINK("testfiles/TestDir1/x1/dsfdsfs98.fd") == 0);
		TEST_THAT(::rename("testfiles/TestDir1/df9834.dsf",
			"testfiles/TestDir1/x1/dsfdsfs98.fd") == 0);

		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));
	}

	TEARDOWN_TEST_BBACKUPD();
}

// Files that suddenly appear, with timestamps before the last sync window,
// and files whose size or timestamp change, should still be uploaded, even
// though they look old.
bool test_upload_very_old_files()
{
	SETUP_WITH_BBSTORED();
	run_bbackupd_sync_with_logging(bbackupd);

	// TODO FIXME dedent
	{
		// Add some more files
		// Because the 'm' option is not used, these files will
		// look very old to the daemon.
		// Lucky it'll upload them then!
		TEST_THAT(unpack_files("test2"));

#ifndef WIN32
		::chmod("testfiles/TestDir1/sub23/dhsfdss/blf.h", 0415);
#endif

		// Wait and test
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

		// Check that modifying files with old timestamps
		// still get added
		BOX_INFO("Modify existing file, but change timestamp to rather old");

		// Then modify an existing file
		{
			// in the archive, it's read only
#ifdef WIN32
			TEST_THAT(::system("attrib -r testfiles\\TestDir\\sub23\\rand.h") == 0);
#else
			TEST_THAT(chmod("testfiles/TestDir1/sub23/rand.h", 0777) == 0);
#endif

			FILE *f = fopen("testfiles/TestDir1/sub23/rand.h", "w+");

			if (f == 0)
			{
				perror("Failed to open");
			}

			TEST_THAT(f != 0);

			if (f != 0)
			{
				fprintf(f, "MODIFIED!\n");
				fclose(f);
			}

			// and then move the time backwards!
			struct timeval times[2];
			BoxTimeToTimeval(SecondsToBoxTime(
				(time_t)(365*24*60*60)), times[1]);
			times[0] = times[1];
			TEST_THAT(::utimes("testfiles/TestDir1/sub23/rand.h",
				times) == 0);
		}

		// Wait and test
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same); // files too new?

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));
	}

	TEARDOWN_TEST_BBACKUPD();
}

bool test_excluded_files_are_not_backed_up(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED_BBSTORED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	BackupDaemon bbackupd;
	TEST_THAT_OR(prepare_test_with_client_daemon(bbackupd,
		true, // do_unpack_files
		false, // !do_start_bbstored - already started by SETUP_TEST_SPECIALISED_BBSTORED
		bbackupd_conf_file), FAIL);

	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol, true); // ReadOnly
	fs.ReleaseLock();

	// TODO FIXME dedent
	{
		// Add some files and directories which are marked as excluded
		TEST_THAT(unpack_files("testexclude"));
		run_bbackupd_sync_with_logging(bbackupd);

		// compare with exclusions, should not find differences
		TEST_COMPARE_SPECIALISED(spec, Compare_Same)

		// compare without exclusions, should find differences
		TEST_THAT(compare_external(BackupQueries::ReturnCode::Compare_Different,
			"", "-acEQ", bbackupd_conf_file));

		// check that the excluded files did not make it
		// into the store, and the included files did
		{
			/*
			std::auto_ptr<BackupProtocolCallable> pClient =
				connect_and_login(context,
				BackupProtocolLogin::Flags_ReadOnly);
			*/

			std::auto_ptr<BackupStoreDirectory> dir = ReadDirectory(protocol);

			int64_t testDirId = SearchDir(*dir, "Test1");
			TEST_THAT(testDirId != 0);
			dir = ReadDirectory(protocol, testDirId);

			TEST_THAT(!SearchDir(*dir, "excluded_1"));
			TEST_THAT(!SearchDir(*dir, "excluded_2"));
			TEST_THAT(!SearchDir(*dir, "exclude_dir"));
			TEST_THAT(!SearchDir(*dir, "exclude_dir_2"));
			// xx_not_this_dir_22 should not be excluded by
			// ExcludeDirsRegex, because it's a file
			TEST_THAT(SearchDir (*dir, "xx_not_this_dir_22"));
			TEST_THAT(!SearchDir(*dir, "zEXCLUDEu"));
			TEST_THAT(SearchDir (*dir, "dont.excludethis"));
			TEST_THAT(SearchDir (*dir, "xx_not_this_dir_ALWAYSINCLUDE"));

			int64_t sub23id = SearchDir(*dir, "sub23");
			TEST_THAT(sub23id != 0);
			dir = ReadDirectory(protocol, sub23id);

			TEST_THAT(!SearchDir(*dir, "xx_not_this_dir_22"));
			TEST_THAT(!SearchDir(*dir, "somefile.excludethis"));

			// client->QueryFinished();
		}
	}

	TEARDOWN_TEST_SPECIALISED_NO_CHECK(spec);
}

bool test_read_error_reporting()
{
	SETUP_WITH_BBSTORED();

#ifdef WIN32
	BOX_NOTICE("skipping test on this platform");
#else
	if(::getuid() == 0)
	{
		BOX_NOTICE("skipping test because we're running as root");
		// These tests only work as non-root users.
	}
	else
	{
		// TODO FIXME detent
		{
			// Check that the error has not been reported yet
			TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

			// Check that read errors are reported neatly
			BOX_INFO("Add unreadable files");

			{
				// Dir and file which can't be read
				TEST_THAT(::mkdir("testfiles/TestDir1/sub23",
					0755) == 0);
				TEST_THAT(::mkdir("testfiles/TestDir1/sub23"
					"/read-fail-test-dir", 0000) == 0);
				int fd = ::open("testfiles/TestDir1"
					"/read-fail-test-file",
					O_CREAT | O_WRONLY, 0000);
				TEST_THAT(fd != -1);
				::close(fd);
			}

			// Wait and test... with sysadmin notification
			bbackupd.RunSyncNowWithExceptionHandling();

			// should fail with an error due to unreadable file
			TEST_COMPARE(Compare_Error);

			// Check that it was reported correctly
			TEST_THAT(TestFileExists("testfiles/notifyran.read-error.1"));

			// Check that the error was only reported once
			TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.2"));

			// Set permissions on file and dir to stop
			// errors in the future
			TEST_THAT(::chmod("testfiles/TestDir1/sub23"
				"/read-fail-test-dir", 0770) == 0);
			TEST_THAT(::chmod("testfiles/TestDir1"
				"/read-fail-test-file", 0770) == 0);
		}
	}
#endif

	TEARDOWN_TEST_BBACKUPD();
}

bool test_continuously_updated_file()
{
	// Temporarily enable timestamp logging, to help debug race conditions causing
	// test failures:
	Console::SettingsGuard save_old_settings;
	Console::SetShowTime(true);
	Console::SetShowTimeMicros(true);

	SETUP_WITH_BBSTORED();

	// Start the bbackupd client. Enable logging to help debug race
	// conditions causing test failure:
	std::string daemon_args(bbackupd_args_overridden ? bbackupd_args :
		"-kU -Wnotice -tbbackupd");
	TEST_THAT_OR(StartClient("testfiles/bbackupd.conf", daemon_args), FAIL);

	// TODO FIXME dedent
	{
		// Make sure everything happens at the same point in the
		// sync cycle: wait until exactly the start of a sync
		wait_for_sync_end();

		{
			// The file will be uploaded after 3-4 seconds (until the next sync) +
			// 7 seconds (MaxUploadWait) + 0-2 seconds (the next sync after that,
			// at which point it's been pending for >MaxUploadWait seconds). We wait,
			// still touching the file, for 2 seconds less than that (8 seconds):
			BOX_NOTICE("Open a file, then save something to it "
				"every second for 8 seconds");

			for(int l = 0; l < 8; ++l)
			{
				FileStream fs("testfiles/TestDir1/continousupdate",
					O_CREAT | O_WRONLY);
				fs.Write("a", 1);
				fs.Close();
				safe_sleep(1);
			}

			// Check there's a difference
			BOX_NOTICE("Comparing all files to check that it was not uploaded yet");
			TEST_RETURN(::system("perl testfiles/extcheck1.pl"),
				BackupQueries::ReturnCode::Compare_Same);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			// And then another 7 seconds, until 2 seconds after the last time that a
			// sync could have run that would have uploaded it (15 seconds)
			BOX_NOTICE("Keep on continuously updating file for "
				"another 4 seconds, check it is uploaded eventually");

			for(int l = 0; l < 7; ++l)
			{
				FileStream fs("testfiles/TestDir1/continousupdate", O_WRONLY);
				fs.Write("a", 1);
				fs.Close();
				safe_sleep(1);
			}

			BOX_NOTICE("Comparing all files to check that it was uploaded by now");
			TEST_RETURN(::system("perl testfiles/extcheck2.pl"),
				BackupQueries::ReturnCode::Compare_Same);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		}
	}

	TEARDOWN_TEST_BBACKUPD();
}

bool test_delete_dir_change_attribute()
{
	SETUP_WITH_BBSTORED();
	run_bbackupd_sync_with_logging(bbackupd);

	// TODO FIXME dedent
	{
		// Delete a directory
#ifdef WIN32
		TEST_THAT(::system("rd /s/q testfiles\\TestDir1\\x1") == 0);
#else
		TEST_THAT(::system("rm -r testfiles/TestDir1/x1") == 0);
#endif
		// Change attributes on an existing file.
#ifdef WIN32
		TEST_EQUAL(0, system("attrib +r testfiles\\TestDir1\\df9834.dsf"));
#else
		TEST_THAT(::chmod("testfiles/TestDir1/df9834.dsf", 0423) == 0);
#endif

		TEST_COMPARE(Compare_Different);

		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN_TEST_BBACKUPD();
}

bool test_restore_files_and_directories()
{
	SETUP_WITH_BBSTORED();
	run_bbackupd_sync_with_logging(bbackupd);

	// TODO FIXME dedent
	{
		int64_t deldirid = 0;
		int64_t restoredirid = 0;
		{
			// connect and log in
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(sTlsContext,
					BackupProtocolLogin::Flags_ReadOnly);

			// Find the ID of the Test1 directory
			restoredirid = get_object_id(*client, "Test1",
				BackupProtocolListDirectory::RootDirectory);
			TEST_THAT_OR(restoredirid != 0, FAIL);

			// Test the restoration
			TEST_THAT(BackupClientRestore(*client, restoredirid,
				"Test1" /* remote */,
				"testfiles/restore-Test1" /* local */,
				true /* print progress dots */,
				false /* restore deleted */,
				false /* undelete after */,
				false /* resume */,
				false /* keep going */)
				== Restore_Complete);

			// On Win32 we can't open another connection
			// to the server, so we'll compare later.

			// Make sure you can't restore a restored directory
			TEST_THAT(BackupClientRestore(*client, restoredirid,
				"Test1", "testfiles/restore-Test1",
				true /* print progress dots */,
				false /* restore deleted */,
				false /* undelete after */,
				false /* resume */,
				false /* keep going */)
				== Restore_TargetExists);

			// Find ID of the deleted directory
			deldirid = get_object_id(*client, "x1", restoredirid);
			TEST_THAT(deldirid != 0);

			// Just check it doesn't bomb out -- will check this
			// properly later (when bbackupd is stopped)
			TEST_THAT(BackupClientRestore(*client, deldirid,
				"Test1", "testfiles/restore-Test1-x1",
				true /* print progress dots */,
				true /* restore deleted */,
				false /* undelete after */,
				false /* resume */,
				false /* keep going */)
				== Restore_Complete);

			// Make sure you can't restore to a nonexistant path
			BOX_INFO("Try to restore to a path that doesn't exist");

			{
				Logger::LevelGuard(Logging::GetConsole(),
					Log::FATAL);
				TEST_THAT(BackupClientRestore(*client,
					restoredirid, "Test1",
					"testfiles/no-such-path/subdir",
					true /* print progress dots */,
					true /* restore deleted */,
					false /* undelete after */,
					false /* resume */,
					false /* keep going */)
					== Restore_TargetPathNotFound);
			}

			// Log out
			client->QueryFinished();
		}

		// Compare the restored files
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN_TEST_BBACKUPD();
}

#ifdef WIN32
bool test_compare_detects_attribute_changes()
{
	SETUP_WITH_BBSTORED();

	run_bbackupd_sync_with_logging(bbackupd);
	TEST_COMPARE(Compare_Same);

	// TODO FIXME dedent
	{
		// make one of the files read-only, expect a compare failure
		int exit_status = ::system("attrib +r "
			"testfiles\\TestDir1\\f1.dat");
		TEST_RETURN(exit_status, 0);

		TEST_COMPARE(Compare_Different);

		// set it back, expect no failures
		exit_status = ::system("attrib -r "
			"testfiles\\TestDir1\\f1.dat");
		TEST_RETURN(exit_status, 0);

		TEST_COMPARE(Compare_Same);

		// change the timestamp on a file, expect a compare failure
		const char* testfile = "testfiles\\TestDir1\\f1.dat";
		HANDLE handle = openfile(testfile, O_RDWR, 0);
		TEST_THAT(handle != INVALID_HANDLE_VALUE);

		FILETIME creationTime, lastModTime, lastAccessTime;
		TEST_THAT(GetFileTime(handle, &creationTime, &lastAccessTime,
			&lastModTime) != 0);
		TEST_THAT(CloseHandle(handle));

		FILETIME dummyTime = lastModTime;
		dummyTime.dwHighDateTime -= 100;

		// creation time is backed up, so changing it should cause
		// a compare failure
		TEST_THAT(set_file_time(testfile, dummyTime, lastModTime,
			lastAccessTime));

		TEST_COMPARE(Compare_Different);

		// last access time is not backed up, so it cannot be compared
		TEST_THAT(set_file_time(testfile, creationTime, lastModTime,
			dummyTime));
		TEST_COMPARE(Compare_Same);

		// last write time is backed up, so changing it should cause
		// a compare failure
		TEST_THAT(set_file_time(testfile, creationTime, dummyTime,
			lastAccessTime));
		TEST_COMPARE(Compare_Different);

		// set back to original values, check that compare succeeds
		TEST_THAT(set_file_time(testfile, creationTime, lastModTime,
			lastAccessTime));
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN_TEST_BBACKUPD();
}
#endif // WIN32

bool test_sync_new_files()
{
	SETUP_WITH_BBSTORED();
	run_bbackupd_sync_with_logging(bbackupd);

	// TODO FIXME dedent
	{
		// Add some more files and modify others. Use the m flag this
		// time so they have a recent modification time.
		TEST_THAT(unpack_files("test3", "testfiles", "m"));

		// OpenBSD's tar interprets the "-m" option quite differently:
		// it sets the time to epoch zero (1 Jan 1970) instead of the
		// current time, which doesn't help us. So reset the timestamp
		// on a file by touching it, so it won't be backed up.
		{
#ifndef WIN32
			TEST_THAT(chmod("testfiles/TestDir1/chsh", 0755) == 0);
#endif
			FileStream fs("testfiles/TestDir1/chsh", O_WRONLY);
			fs.Write("a", 1);
		}
		
		// At least one file is too new to be backed up on the first run.
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Different);

		wait_for_operation(5, "newly added files to be old enough");
		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN_TEST_BBACKUPD();
}

bool test_rename_operations()
{
	SETUP_WITH_BBSTORED();

	TEST_THAT(unpack_files("test2"));
	TEST_THAT(unpack_files("test3"));
	run_bbackupd_sync_with_logging(bbackupd);
	TEST_COMPARE(Compare_Same);

	// TODO FIXME dedent
	{
		BOX_INFO("Rename directory");
		TEST_THAT(rename("testfiles/TestDir1/sub23/dhsfdss",
			"testfiles/TestDir1/renamed-dir") == 0);

		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);

		// and again, but with quick flag
		TEST_COMPARE_EXTRA(Compare_Same, "", "-acqQ");

		// Rename some files -- one under the threshold, others above
		TEST_THAT(rename("testfiles/TestDir1/df324",
			"testfiles/TestDir1/df324-ren") == 0);
		TEST_THAT(rename("testfiles/TestDir1/sub23/find2perl",
			"testfiles/TestDir1/find2perl-ren") == 0);

		run_bbackupd_sync_with_logging(bbackupd);
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN_TEST_BBACKUPD();
}

// Check that modifying files with madly in the future timestamps still get added
bool test_sync_files_with_timestamps_in_future()
{
	SETUP_WITH_BBSTORED();
	run_bbackupd_sync_with_logging(bbackupd);

	// TODO FIXME dedent
	{
		{
			TEST_THAT(::mkdir("testfiles/TestDir1/sub23",
				0755) == 0);
			FILE *f = fopen("testfiles/TestDir1/sub23/"
				"in-the-future", "w");
			TEST_THAT_OR(f != 0, FAIL);
			fprintf(f, "Back to the future!\n");
			fclose(f);
			// and then move the time forwards!
			struct timeval times[2];
			BoxTimeToTimeval(GetCurrentBoxTime() +
				SecondsToBoxTime((time_t)(365*24*60*60)),
				times[1]);
			times[0] = times[1];
			TEST_THAT(::utimes("testfiles/TestDir1/sub23/"
				"in-the-future", times) == 0);
		}

		// Wait and test
		run_bbackupd_sync_with_logging(bbackupd);
		wait_for_backup_operation("bbackup to sync future file");
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN_TEST_BBACKUPD();
}

// Check change of store marker pauses daemon
bool test_changing_client_store_marker_pauses_daemon(RaidAndS3TestSpecs::Specialisation& spec)
{
	// Temporarily enable timestamp logging, to help debug race conditions causing
	// test failures:
	Console::SettingsGuard save_old_settings;
	Console::SetShowTime(true);
	Console::SetShowTimeMicros(true);

	// Debugging this test requires INFO level logging
	Logger::LevelGuard increase_to_info(Logging::GetConsole(), Log::INFO);

	SETUP_TEST_SPECIALISED_BBSTORED(spec);
	BackupFileSystem& fs(spec.control().GetFileSystem());

	// Start the bbackupd client. Enable logging to help debug race
	// conditions causing test failure:
	std::string daemon_args(bbackupd_args_overridden ? bbackupd_args :
		"-kU -Wnotice -tbbackupd");
	TEST_THAT_OR(StartClient(bbackupd_conf_file, daemon_args), FAIL);

	// Wait for the client to upload all current files. We also time
	// approximately how long a sync takes.
	box_time_t sync_start_time = GetCurrentBoxTime();
	sync_and_wait();
	box_time_t sync_time = GetCurrentBoxTime() - sync_start_time;
	BOX_INFO("Sync takes " << BOX_FORMAT_MICROSECONDS(sync_time));

	// Time how long a compare takes. On NetBSD it's 3 seconds, and that
	// interferes with test timing unless we account for it.
	box_time_t compare_start_time = GetCurrentBoxTime();
	// There should be no differences right now (yet).
	TEST_COMPARE_SPECIALISED(spec, Compare_Same);
	box_time_t compare_time = GetCurrentBoxTime() - compare_start_time;
	BOX_INFO("Compare takes " << BOX_FORMAT_MICROSECONDS(compare_time));

	// Wait for the end of another sync, to give us ~3 seconds to change
	// the client store marker.
	wait_for_sync_end();

	// TODO FIXME dedent
	{
		// Then... connect to the server, and change the
		// client store marker. See what that does!
		{
			bool done = false;
			int tries = 4;
			while(!done && tries > 0)
			{
				try
				{
					CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, context, protocol,
						false); // !ReadOnly

					// Make sure the marker isn't zero,
					// because that's the default, and
					// it should have changed
					TEST_THAT(protocol.GetClientStoreMarker() != 0);

					// Change it to something else
					BOX_INFO("Changing client store marker from " <<
						protocol.GetClientStoreMarker() << " to 12");
					protocol.QuerySetClientStoreMarker(12);

					// Success!
					done = true;

					// Log out
					protocol.QueryFinished();
				}
				catch(BoxException &e)
				{
					BOX_INFO("Failed to connect to bbstored, "
						<< tries << " retries remaining: "
						<< e.what());
					tries--;
				}
				catch(...)
				{
					tries--;
				}
			}
			TEST_THAT(done);
		}

		// Make a change to a file, to detect whether or not
		// it's hanging around waiting to retry.
		{
			FILE *f = ::fopen("testfiles/TestDir1/fileaftermarker", "w");
			TEST_THAT(f != 0);
			::fprintf(f, "Lovely file you got there.");
			::fclose(f);
		}

		// Wait for bbackupd to detect the problem.
		wait_for_sync_end();

		// Test that there *are* differences still, i.e. that bbackupd
		// didn't successfully run a backup during that time.
		BOX_INFO("Compare starting, expecting differences");
		TEST_COMPARE_SPECIALISED(spec, Compare_Different);
		BOX_TRACE("Compare finished, expected differences");

		// Wait out the expected delay in bbackupd. This is quite
		// time-sensitive, so we use sub-second precision.
		box_time_t wait =
			SecondsToBoxTime(BACKUP_ERROR_DELAY_SHORTENED - 1) -
			compare_time * 2;
		BOX_INFO("Waiting for " << BOX_FORMAT_MICROSECONDS(wait) << " "
			"until just before bbackupd recovers");
		ShortSleep(wait, true);

		// bbackupd should not have recovered yet, so there should
		// still be differences.
		BOX_INFO("Compare starting, expecting differences");
		TEST_COMPARE_SPECIALISED(spec, Compare_Different);
		BOX_TRACE("Compare finished, expected differences");

		// Now wait for it to recover and finish a sync, and check that
		// the differences are gone (successful backup). Wait until ~2
		// seconds after we expect the sync to have finished, to reduce
		// the risk of random failure on AppVeyor when heavily loaded.
		wait = sync_time + SecondsToBoxTime(6);
		BOX_INFO("Waiting for " << BOX_FORMAT_MICROSECONDS(wait) <<
			" until just after bbackupd recovers and finishes a sync");
		ShortSleep(wait, true);

		BOX_INFO("Compare starting, expecting no differences");
		TEST_COMPARE_SPECIALISED(spec, Compare_Same);
		BOX_TRACE("Compare finished, expected no differences");
	}

	TEARDOWN_TEST_SPECIALISED_NO_CHECK(spec);
}

bool test_interrupted_restore_can_be_recovered(RaidAndS3TestSpecs::Specialisation& spec)
{
	SETUP_TEST_SPECIALISED_BBACKUPD(spec);

	run_bbackupd_sync_with_logging(bbackupd);

	BackupFileSystem& fs(spec.control().GetFileSystem());
	CREATE_LOCAL_CONTEXT_AND_PROTOCOL(fs, rwContext, protocol, true); // ReadOnly

	// Find the ID of the Test1 directory
	int64_t restoredirid = get_object_id(protocol, "Test1",
		BackupProtocolListDirectory::RootDirectory);
	TEST_THAT_OR(restoredirid != 0, FAIL);
	fs.ReleaseLock();

	do_interrupted_restore(sTlsContext, restoredirid, bbackupd_conf_file);
	int64_t resumesize = 0;
	TEST_THAT(FileExists("testfiles/" "restore-interrupt.boxbackupresume", &resumesize));
	// make sure it has recorded something to resume
	TEST_THAT(resumesize > 16);

	// Check that the restore fn returns resume possible,
	// rather than doing anything
	TEST_THAT(BackupClientRestore(protocol, restoredirid,
		"Test1", "testfiles/restore-interrupt",
		true /* print progress dots */,
		false /* restore deleted */,
		false /* undelete after */,
		false /* resume */,
		false /* keep going */)
		== Restore_ResumePossible);

	// Then resume it
	TEST_THAT(BackupClientRestore(protocol, restoredirid,
		"Test1", "testfiles/restore-interrupt",
		true /* print progress dots */,
		false /* restore deleted */,
		false /* undelete after */,
		true /* resume */,
		false /* keep going */)
		== Restore_Complete);

	protocol.QueryFinished();

	// Then check it has restored the correct stuff
	TEST_COMPARE_SPECIALISED(spec, Compare_Same);

	TEARDOWN_TEST_SPECIALISED_NO_CHECK(spec);
}

bool assert_x1_deleted_or_not(bool expected_deleted)
{
	std::auto_ptr<BackupProtocolCallable> client =
		connect_and_login(sTlsContext, 0 /* read-write */);

	std::auto_ptr<BackupStoreDirectory> dir = ReadDirectory(*client);
	int64_t testDirId = SearchDir(*dir, "Test1");
	TEST_THAT_OR(testDirId != 0, return false);

	dir = ReadDirectory(*client, testDirId);
	BackupStoreDirectory::Iterator i(*dir);
	BackupStoreFilenameClear child("x1");
	BackupStoreDirectory::Entry *en = i.FindMatchingClearName(child);
	TEST_THAT_OR(en != 0, return false);
	TEST_EQUAL_OR(expected_deleted, en->IsDeleted(), return false);

	return true;
}

bool test_restore_deleted_files()
{
	SETUP_WITH_BBSTORED();

	run_bbackupd_sync_with_logging(bbackupd);
	TEST_COMPARE(Compare_Same);

	TEST_THAT(EMU_UNLINK("testfiles/TestDir1/f1.dat") == 0);
#ifdef WIN32
	TEST_THAT(::system("rd /s/q testfiles\\TestDir1\\x1") == 0);
#else
	TEST_THAT(::system("rm -r testfiles/TestDir1/x1") == 0);
#endif
	TEST_COMPARE(Compare_Different);

	run_bbackupd_sync_with_logging(bbackupd);
	TEST_COMPARE(Compare_Same);
	TEST_THAT(assert_x1_deleted_or_not(true));

	// TODO FIXME dedent
	{
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(sTlsContext, 0 /* read-write */);

			// Find the ID of the Test1 directory
			int64_t restoredirid = get_object_id(*client, "Test1",
				BackupProtocolListDirectory::RootDirectory);
			TEST_THAT_OR(restoredirid != 0, FAIL);

			// Find ID of the deleted directory
			int64_t deldirid = get_object_id(*client, "x1", restoredirid);
			TEST_THAT_OR(deldirid != 0, FAIL);

			// Do restore and undelete
			TEST_THAT(BackupClientRestore(*client, deldirid,
				"Test1", "testfiles/restore-Test1-x1-2",
				true /* print progress dots */,
				true /* deleted files */,
				true /* undelete after */,
				false /* resume */,
				false /* keep going */)
				== Restore_Complete);

			client->QueryFinished();
			client.reset();

			// Do a compare with the now undeleted files
			TEST_COMPARE_EXTRA(Compare_Same, "", "-cEQ Test1/x1 "
				"testfiles/restore-Test1-x1-2");
		}

		// Final check on notifications
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.2"));
	}

	// should have been undeleted by restore
	TEST_THAT(assert_x1_deleted_or_not(false));

	TEARDOWN_TEST_BBACKUPD();
}

bool test_locked_file_behaviour()
{
	SETUP_WITH_BBSTORED();

#ifndef WIN32
	// There are no tests for mandatory locks on non-Windows platforms yet.
	BOX_NOTICE("skipping test on this platform");
#else
	// TODO FIXME dedent
	{
		// Test that locked files cannot be backed up,
		// and the appropriate error is reported.

		HANDLE handle = openfile("testfiles/TestDir1/f1.dat",
			BOX_OPEN_LOCK, 0);
		TEST_THAT_OR(handle != INVALID_HANDLE_VALUE, FAIL);

		{
			// this sync should try to back up the file,
			// and fail, because it's locked
			bbackupd.RunSyncNowWithExceptionHandling();
			TEST_THAT(TestFileExists("testfiles/"
				"notifyran.read-error.1"));
			TEST_THAT(!TestFileExists("testfiles/"
				"notifyran.read-error.2"));
		}

		{
			// now close the file and check that it is
			// backed up on the next run.
			CloseHandle(handle);
			run_bbackupd_sync_with_logging(bbackupd);

			// still no read errors?
			TEST_THAT(!TestFileExists("testfiles/"
				"notifyran.read-error.2"));
			TEST_COMPARE(Compare_Same);
		}

		{
			// open the file again, compare and check that compare
			// reports the correct error message (and finishes)
			handle = openfile("testfiles/TestDir1/f1.dat",
				BOX_OPEN_LOCK, 0);
			TEST_THAT_OR(handle != INVALID_HANDLE_VALUE, FAIL);

			TEST_COMPARE(Compare_Error);

			// close the file again, check that compare
			// works again
			CloseHandle(handle);
			TEST_COMPARE(Compare_Same);
		}
	}
#endif // WIN32

	TEARDOWN_TEST_BBACKUPD();
}

bool test_backup_many_files()
{
	SETUP_WITH_BBSTORED();

	unpack_files("test2");
	unpack_files("test3");
	unpack_files("testexclude");
	unpack_files("spacetest1", "testfiles/TestDir1");
	unpack_files("spacetest2", "testfiles/TestDir1");

	run_bbackupd_sync_with_logging(bbackupd);
	TEST_COMPARE(Compare_Same);

	TEARDOWN_TEST_BBACKUPD();
}

bool test_parse_incomplete_command()
{
	SETUP_TEST_BBACKUPD();

	{
		// This is not a complete command, it should not parse!
		BackupQueries::ParsedCommand cmd("-od", true);
		TEST_THAT(cmd.mFailed);
		TEST_EQUAL((void *)NULL, cmd.pSpec);
		TEST_EQUAL(0, cmd.mCompleteArgCount);
	}

	TEARDOWN_TEST_BBACKUPD();
}

bool test_parse_syncallowscript_output()
{
	SETUP_TEST_BBACKUPD();

	{
		BackupDaemon daemon;

		TEST_EQUAL(1234, daemon.ParseSyncAllowScriptOutput("test", "1234"));
		TEST_EQUAL(0, daemon.GetMaxBandwidthFromSyncAllowScript());

		TEST_EQUAL(1234, daemon.ParseSyncAllowScriptOutput("test", "1234 5"));
		TEST_EQUAL(5, daemon.GetMaxBandwidthFromSyncAllowScript());

		TEST_EQUAL(-1, daemon.ParseSyncAllowScriptOutput("test", "now"));
		TEST_EQUAL(0, daemon.GetMaxBandwidthFromSyncAllowScript());
	}

	TEARDOWN_TEST_BBACKUPD();
}

bool test_bbackupd_config_script()
{
	SETUP_TEST_BBACKUPD();

#ifdef WIN32
	BOX_NOTICE("skipping test on this platform"); // TODO: write a PowerShell version
#else
	char buf[PATH_MAX];
	if (getcwd(buf, sizeof(buf)) == NULL)
	{
		BOX_LOG_SYS_ERROR("getcwd");
	}
	std::string current_dir = buf;

	TEST_THAT(mkdir("testfiles/tmp", 0777) == 0);
	TEST_THAT(mkdir("testfiles/TestDir1", 0777) == 0);

	// Generate a new configuration for our test bbackupd, from scratch:
	std::string cmd = "../../../bin/bbackupd/bbackupd-config " +
		current_dir + "/testfiles/tmp " // config-dir
		"lazy " // backup-mode
		"12345 " // account-num
		"localhost " + // server-hostname
		current_dir + "/testfiles " + // working-dir
		current_dir + "/testfiles/TestDir1"; // backup directories
	TEST_RETURN(system(cmd.c_str()), 0)

	// Open the generated config file and add a StorePort line:
	{
		FileStream conf_file("testfiles/tmp/bbackupd.conf", O_WRONLY | O_APPEND);
		conf_file.Write("StorePort = 22011\n");
		conf_file.Close();
	}

	// Generate a new configuration for our test bbstored, from scratch:
	struct passwd *result = getpwuid(getuid());
	TEST_THAT_OR(result != NULL, FAIL); // failed to get username for current user
	std::string username = result->pw_name;

	cmd = "../../../bin/bbstored/bbstored-config testfiles/tmp localhost " + username + " "
		"testfiles/raidfile.conf";
	TEST_RETURN(system(cmd.c_str()), 0)

	cmd = "sed -i.orig -e 's/\\(ListenAddresses = inet:localhost\\)/\\1:22011/' "
		"-e 's@PidFile = .*/run/bbstored.pid@PidFile = testfiles/bbstored.pid@' "
		"testfiles/tmp/bbstored.conf";
	TEST_RETURN(system(cmd.c_str()), 0)

	// Create a server certificate authority, and sign the client and server certificates:
	cmd = "../../../bin/bbstored/bbstored-certs testfiles/ca init";
	TEST_RETURN(system(cmd.c_str()), 0)

	cmd = "echo yes | ../../../bin/bbstored/bbstored-certs testfiles/ca sign "
		"testfiles/tmp/bbackupd/12345-csr.pem";
	TEST_RETURN(system(cmd.c_str()), 0)

	cmd = "echo yes | ../../../bin/bbstored/bbstored-certs testfiles/ca sign-server "
		"testfiles/tmp/bbstored/localhost-csr.pem";
	TEST_RETURN(system(cmd.c_str()), 0)

	// Copy the certificate files into the right places
	cmd = "cp testfiles/ca/clients/12345-cert.pem testfiles/tmp/bbackupd";
	TEST_RETURN(system(cmd.c_str()), 0)

	cmd = "cp testfiles/ca/roots/serverCA.pem testfiles/tmp/bbackupd";
	TEST_RETURN(system(cmd.c_str()), 0)

	cmd = "cp testfiles/ca/servers/localhost-cert.pem testfiles/tmp/bbstored";
	TEST_RETURN(system(cmd.c_str()), 0)

	cmd = "cp testfiles/ca/roots/clientCA.pem testfiles/tmp/bbstored";
	TEST_RETURN(system(cmd.c_str()), 0)

	cmd = BBSTOREACCOUNTS " -c testfiles/tmp/bbstored.conf create 12345 0 1M 2M";
	TEST_RETURN(system(cmd.c_str()), 0)

	bbstored_pid = StartDaemon(bbstored_pid, BBSTORED " " + bbstored_args +
		" testfiles/tmp/bbstored.conf", "testfiles/bbstored.pid", 22011);

	BackupDaemon bbackupd;
	TEST_THAT(
		prepare_test_with_client_daemon(
			bbackupd,
			true, // do_unpack_files
			false, // !do_start_bbstored
			"testfiles/tmp/bbackupd.conf")
		);

	run_bbackupd_sync_with_logging(bbackupd);

	TEST_THAT(compare_external(BackupQueries::ReturnCode::Compare_Same,
		"", "-acQ", "testfiles/tmp/bbackupd.conf"));

	TEST_THAT(StopServer());
#endif

	TEARDOWN_TEST_BBACKUPD();
}

int test(int argc, const char *argv[])
{
	// SSL library
	SSLLib::Initialise();

	// Keys for subsystems
	BackupClientCryptoKeys_Setup("testfiles/bbackupd.keys");

	{
		std::string errs;
		std::auto_ptr<Configuration> config(
			Configuration::LoadAndVerify
				("testfiles/bbstored.conf", &BackupConfigFileVerify, errs));
		TEST_EQUAL_LINE(0, errs.size(), "Loading configuration file "
			"reported errors: " << errs);
		TEST_THAT_OR(config.get(), return 1);
		// Initialise the raid file controller
		RaidFileController &rcontroller(RaidFileController::GetController());
		rcontroller.Initialise(config->GetKeyValue("RaidFileConf").c_str());
	}

	sTlsContext.Initialise(false /* client */,
			"testfiles/clientCerts.pem",
			"testfiles/clientPrivKey.pem",
			"testfiles/clientTrustedCAs.pem");

	std::auto_ptr<RaidAndS3TestSpecs> specs(
		new RaidAndS3TestSpecs("testfiles/bbackupd.s3.conf"));

	TEST_THAT(kill_running_daemons());
	TEST_THAT(StartSimulator());

	TEST_THAT(test_file_attribute_storage());

	// Run all tests that take a RaidAndS3TestSpecs::Specialisation argument twice, once with
	// each specialisation that we have (S3 and BackupStore).

	for(auto i = specs->specs().begin(); i != specs->specs().end(); i++)
	{
		TEST_THAT(test_bbackupquery_getobject_on_nonexistent_file(*i));
		TEST_THAT(test_backup_disappearing_directory(*i));
		TEST_THAT(test_backup_hardlinked_files(*i));
		TEST_THAT(test_backup_pauses_when_store_is_full(*i));
		TEST_THAT(test_bbackupd_exclusions(*i));
		TEST_THAT(test_bbackupd_responds_to_connection_failure_out_of_process(*i, specs));
		TEST_THAT(test_interrupted_restore_can_be_recovered(*i));
		TEST_THAT(test_store_error_reporting(*i));
		TEST_THAT(test_excluded_files_are_not_backed_up(*i));
		TEST_THAT(test_changing_client_store_marker_pauses_daemon(*i));
	}

	TEST_THAT(test_bbackupquery_parser_escape_slashes());
	// TEST_THAT(test_replace_zero_byte_file_with_nonzero_byte_file());
	TEST_THAT(test_ssl_keepalives());
	TEST_THAT(test_bbackupd_uploads_files());
	TEST_THAT(test_bbackupd_responds_to_connection_failure_in_process());
	TEST_THAT(test_bbackupd_responds_to_connection_failure_in_process_s3(specs->s3()));
	TEST_THAT(test_absolute_symlinks_not_followed_during_restore());
	TEST_THAT(test_initially_missing_locations_are_not_forgotten());
	TEST_THAT(test_redundant_locations_deleted_on_time());
	TEST_THAT(test_read_only_dirs_can_be_restored());

#ifndef WIN32
	// requires ConvertConsoleToUtf8()
#else
	TEST_THAT(test_unicode_filenames_can_be_backed_up());
#endif

	TEST_THAT(test_sync_allow_script_can_pause_backup());
	TEST_THAT(test_delete_update_and_symlink_files());
	TEST_THAT(test_change_file_to_symlink_and_back());
	TEST_THAT(test_file_rename_tracking());
	TEST_THAT(test_upload_very_old_files());
	TEST_THAT(test_read_error_reporting());
	TEST_THAT(test_continuously_updated_file());
	TEST_THAT(test_delete_dir_change_attribute());
	TEST_THAT(test_restore_files_and_directories());

#ifndef WIN32
	// requires openfile(), GetFileTime() and attrib.exe
#else
	TEST_THAT(test_compare_detects_attribute_changes());
#endif

	TEST_THAT(test_sync_new_files());
	TEST_THAT(test_rename_operations());
	TEST_THAT(test_sync_files_with_timestamps_in_future());
	TEST_THAT(test_restore_deleted_files());
	TEST_THAT(test_locked_file_behaviour());
	TEST_THAT(test_backup_many_files());
	TEST_THAT(test_parse_incomplete_command());
	TEST_THAT(test_parse_syncallowscript_output());
	TEST_THAT(test_bbackupd_config_script());

#ifndef WIN32
	if(::getuid() == 0)
	{
		BOX_WARNING("This test was run as root. Some tests have been omitted.");
	}
#endif

	TEST_THAT(StopSimulator());
	TEST_THAT(kill_running_daemons());

	return finish_test_suite();
}
