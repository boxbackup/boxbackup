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

#ifndef WIN32
	#include <dirent.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_SYS_WAIT_H
	#include <sys/wait.h>
#endif

#ifdef HAVE_SYS_XATTR_H
	#include <cerrno>
	#include <sys/xattr.h>
#endif

#ifdef HAVE_SIGNAL_H
	#include <signal.h>
#endif

#include <map>

#ifdef HAVE_SYSCALL
	#include <sys/syscall.h>
#endif

#include "BackupClientCryptoKeys.h"
#include "BackupClientContext.h"
#include "BackupClientFileAttributes.h"
#include "BackupClientInodeToIDMap.h"
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
#include "BoxPortsAndFiles.h"
#include "BoxTime.h"
#include "BoxTimeToUnix.h"
#include "CollectInBufferStream.h"
#include "CommonException.h"
#include "Configuration.h"
#include "FileModificationTime.h"
#include "FileStream.h"
#include "intercept.h"
#include "IOStreamGetLine.h"
#include "LocalProcessStream.h"
#include "MemBlockStream.h"
#include "RaidFileController.h"
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
#define TIME_TO_WAIT_FOR_BACKUP_OPERATION	12
#define SHORT_TIMEOUT 5000
#define BACKUP_ERROR_DELAY_SHORTENED 10

std::string current_test_name;
std::map<std::string, std::string> s_test_status;

#define FAIL { \
	std::ostringstream os; \
	os << "failed at " << __FUNCTION__ << ":" << __LINE__; \
	s_test_status[current_test_name] = os.str(); \
	return fail(); \
}

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

#ifdef WIN32
	std::string cmd("tar xz ");
	cmd += tar_options + " -f testfiles/" + archive_file + ".tgz " +
		"-C " + destination_dir;
#else
	std::string cmd("gzip -d < testfiles/");
	cmd += archive_file + ".tgz | ( cd " + destination_dir + " && tar xf - " +
		tar_options + ")";
#endif

	TEST_THAT_OR(::system(cmd.c_str()) == 0, return false);
	return true;
}

Daemon* spDaemon = NULL;

bool configure_bbackupd(BackupDaemon& bbackupd, const std::string& config_file)
{
	// Stop bbackupd initialisation from changing the console logging level
	// and the program name tag.
	Logger& console(Logging::GetConsole());
	Logger::LevelGuard guard(console, console.GetLevel());
	Logging::Tagger();

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

bool kill_running_daemons()
{
	TEST_THAT_OR(::system("test ! -r testfiles/bbstored.pid || "
		"kill `cat testfiles/bbstored.pid`") == 0, FAIL);
	TEST_THAT_OR(::system("test ! -r testfiles/bbackupd.pid || "
		"kill `cat testfiles/bbackupd.pid`") == 0, FAIL);
	TEST_THAT_OR(::system("rm -f testfiles/bbackupd.pid "
		"testfiles/bbstored.pid") == 0, FAIL);
	return true;
}

bool setup_test_bbackupd(BackupDaemon& bbackupd, bool do_unpack_files = true,
	bool do_start_bbstored = true)
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

	TEST_THAT_OR(configure_bbackupd(bbackupd, "testfiles/bbackupd.conf"),
		FAIL);
	spDaemon = &bbackupd;
	return true;
}

int num_tests_selected = 0;

//! Simplifies calling setUp() with the current function name in each test.
#define SETUP() \
	TEST_THAT(kill_running_daemons()); \
	if (!setUp(__FUNCTION__)) return true; \
	num_tests_selected++; \
	int old_failure_count = failures;

#define SETUP_WITHOUT_FILES() \
	SETUP() \
	BackupDaemon bbackupd; \
	TEST_THAT_OR(setup_test_bbackupd(bbackupd, false), FAIL); \
	TEST_THAT_OR(::mkdir("testfiles/TestDir1", 0755) == 0, FAIL);

#define SETUP_WITH_BBSTORED() \
	SETUP() \
	BackupDaemon bbackupd; \
	TEST_THAT_OR(setup_test_bbackupd(bbackupd), FAIL);

//! Checks account for errors and shuts down daemons at end of every test.
bool teardown_test_bbackupd(std::string test_name, int old_failure_count)
{
	if (failures == old_failure_count)
	{
		BOX_NOTICE(test_name << " passed");
		s_test_status[test_name] = "passed";
	}
	else
	{
		BOX_NOTICE(test_name << " failed"); \
		s_test_status[test_name] = "FAILED";
	}

	if(bbackupd_pid != 0)
	{
		TEST_THAT(StopClient());
	}

	return tearDown();
}

#define TEARDOWN() \
	return teardown_test_bbackupd(__FUNCTION__, old_failure_count);

bool test_basics()
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

int64_t GetDirID(BackupProtocolCallable &protocol, const char *name, int64_t InDirectory)
{
	protocol.QueryListDirectory(
			InDirectory,
			BackupProtocolListDirectory::Flags_Dir,
			BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
			true /* want attributes */);
	
	// Retrieve the directory from the stream following
	BackupStoreDirectory dir;
	std::auto_ptr<IOStream> dirstream(protocol.ReceiveStream());
	dir.ReadFromStream(*dirstream, protocol.GetTimeout());
	
	BackupStoreDirectory::Iterator i(dir);
	BackupStoreDirectory::Entry *en = 0;
	int64_t dirid = 0;
	BackupStoreFilenameClear dirname(name);
	while((en = i.Next()) != 0)
	{
		if(en->GetName() == dirname)
		{
			dirid = en->GetObjectID();
		}
	}
	return dirid;
}

void terminate_on_alarm(int sigraised)
{
	abort();
}

#ifndef WIN32
void do_interrupted_restore(const TLSContext &context, int64_t restoredirid)
{
	int pid = 0;
	switch((pid = fork()))
	{
	case 0:
		// child process
		{
			// connect and log in
			SocketStreamTLS* pConn = new SocketStreamTLS;
			std::auto_ptr<SocketStream> apConn(pConn);
			pConn->Open(context, Socket::TypeINET, "localhost", 22011);
			BackupProtocolClient protocol(apConn);

			protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION);
			std::auto_ptr<BackupProtocolLoginConfirmed>
				loginConf(protocol.QueryLogin(0x01234567,
					BackupProtocolLogin::Flags_ReadOnly));
			
			// Test the restoration
			TEST_THAT(BackupClientRestore(protocol, restoredirid,
				"testfiles/restore-interrupt", /* remote */
				"testfiles/restore-interrupt", /* local */
				true /* print progress dots */,
				false /* restore deleted */,
				false /* undelete after */,
				false /* resume */,
				false /* keep going */) == Restore_Complete);

			// Log out
			protocol.QueryFinished();
		}
		exit(0);
		break;
	
	case -1:
		{
			printf("Fork failed\n");
			exit(1);
		}
	
	default:
		{
			// Wait until a resume file is written, then terminate the child
			while(true)
			{
				// Test for existence of the result file
				int64_t resumesize = 0;
				if(FileExists("testfiles/restore-interrupt.boxbackupresume", &resumesize) && resumesize > 16)
				{
					// It's done something. Terminate it.	
					::kill(pid, SIGTERM);
					break;
				}

				// Process finished?
				int status = 0;
				if(waitpid(pid, &status, WNOHANG) != 0)
				{
					// child has finished anyway.
					return;
				}
				
				// Give up timeslot so as not to hog the processor
				::sleep(0);
			}
			
			// Just wait until the child has completed
			int status = 0;
			waitpid(pid, &status, 0);
		}
	}
}
#endif // !WIN32

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

void intercept_setup_delay(const char *filename, unsigned int delay_after,
	int delay_ms, int syscall_to_delay);
bool intercept_triggered();

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

int start_internal_daemon()
{
	// ensure that no child processes end up running tests!
	int own_pid = getpid();
	BOX_TRACE("Test PID is " << own_pid);
		
	// this is a quick hack to allow passing some options to the daemon
	const char* argv[] = {
		"dummy",
		bbackupd_args.c_str(),
	};

	BackupDaemon daemon;
	spDaemon = &daemon; // to propagate into child
	int result;

	if (bbackupd_args.size() > 0)
	{
		result = daemon.Main("testfiles/bbackupd.conf", 2, argv);
	}
	else
	{
		result = daemon.Main("testfiles/bbackupd.conf", 1, argv);
	}

	spDaemon = NULL; // to ensure not used by parent
	
	TEST_EQUAL_LINE(0, result, "Daemon exit code");
	
	// ensure that no child processes end up running tests!
	if (getpid() != own_pid)
	{
		// abort!
		BOX_INFO("Daemon child finished, exiting now.");
		_exit(0);
	}

	TEST_THAT(TestFileExists("testfiles/bbackupd.pid"));
	
	printf("Waiting for backup daemon to start: ");
	int pid = -1;
	
	for (int i = 0; i < 30; i++)
	{
		printf(".");
		fflush(stdout);
		safe_sleep(1);

		if (TestFileExists("testfiles/bbackupd.pid"))
		{
			pid = ReadPidFile("testfiles/bbackupd.pid");
		}

		if (pid > 0)
		{
			break;
		}		
	}
	
	printf(" done.\n");
	fflush(stdout);

	TEST_THAT(pid > 0);
	spDaemon = &daemon;
	return pid;
}

bool stop_internal_daemon(int pid)
{
	bool killed_server = KillServer(pid, false);
	TEST_THAT(killed_server);
	return killed_server;
}

static struct dirent readdir_test_dirent;
static int readdir_test_counter = 0;
static int readdir_stop_time = 0;
static char stat_hook_filename[512];

// First test hook, during the directory scanning stage, returns empty.
// (Where is this stage? I can't find it, so I switched from using 
// readdir_test_hook_1 to readdir_test_hook_2 in intercept tests.)
// This will not match the directory on the store, so a sync will start.
// We set up the next intercept for the same directory by passing NULL.

extern "C" struct dirent *readdir_test_hook_2(DIR *dir);

#ifdef LINUX_WEIRD_LSTAT
extern "C" int lstat_test_hook(int ver, const char *file_name, struct stat *buf);
#else
extern "C" int lstat_test_hook(const char *file_name, struct stat *buf);
#endif

extern "C" struct dirent *readdir_test_hook_1(DIR *dir)
{
#ifndef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
	intercept_setup_readdir_hook(NULL, readdir_test_hook_2);
#endif
	return NULL;
}

// Second test hook, called by BackupClientDirectoryRecord::SyncDirectory,
// keeps returning new filenames until the timer expires, then disables the
// intercept.

extern "C" struct dirent *readdir_test_hook_2(DIR *dir)
{
	if (spDaemon->IsTerminateWanted())
	{
		// force daemon to crash, right now
		return NULL;
	}

	time_t time_now = time(NULL);

	if (time_now >= readdir_stop_time)
	{
#ifndef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
		BOX_NOTICE("Cancelling readdir hook at " << time_now);
		intercept_setup_readdir_hook(NULL, NULL);
		intercept_setup_lstat_hook  (NULL, NULL);
		// we will not be called again.
#else
		BOX_NOTICE("Failed to cancel readdir hook at " << time_now);
#endif
	}
	else
	{
		BOX_TRACE("readdir hook still active at " << time_now << ", "
			"waiting for " << readdir_stop_time);
	}

	// fill in the struct dirent appropriately
	memset(&readdir_test_dirent, 0, sizeof(readdir_test_dirent));

	#ifdef HAVE_STRUCT_DIRENT_D_INO
		readdir_test_dirent.d_ino = ++readdir_test_counter;
	#endif

	snprintf(readdir_test_dirent.d_name, 
		sizeof(readdir_test_dirent.d_name),
		"test.%d", readdir_test_counter);
	BOX_TRACE("readdir hook returning " << readdir_test_dirent.d_name);

	// ensure that when bbackupd stats the file, it gets the 
	// right answer
	snprintf(stat_hook_filename, sizeof(stat_hook_filename),
		"testfiles/TestDir1/spacetest/d1/test.%d", 
		readdir_test_counter);

#ifndef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
	intercept_setup_lstat_hook(stat_hook_filename, lstat_test_hook);
#endif

	// sleep a bit to reduce the number of dirents returned
	if (time_now < readdir_stop_time)
	{
		::safe_sleep(1);
	}

	return &readdir_test_dirent;
}

#ifdef LINUX_WEIRD_LSTAT
extern "C" int lstat_test_hook(int ver, const char *file_name, struct stat *buf)
#else
extern "C" int lstat_test_hook(const char *file_name, struct stat *buf)
#endif
{
	// TRACE1("lstat hook triggered for %s", file_name);
	memset(buf, 0, sizeof(*buf));
	buf->st_mode = S_IFREG;
	return 0;
}

// Simulate a symlink that is on a different device than the file
// that it points to.
int lstat_test_post_hook(int old_ret, const char *file_name, struct stat *buf)
{
	BOX_TRACE("lstat post hook triggered for " << file_name);
	if (old_ret == 0 &&
		strcmp(file_name, "testfiles/symlink-to-TestDir1") == 0)
	{
		buf->st_dev ^= 0xFFFF;
	}
	return old_ret;
}

bool test_entry_deleted(BackupStoreDirectory& rDir,
	const std::string& rName)
{
	BackupStoreDirectory::Iterator i(rDir);

	BackupStoreDirectory::Entry *en = i.FindMatchingClearName(
		BackupStoreFilenameClear(rName));
	TEST_THAT_OR(en != 0, return false);

	int16_t flags = en->GetFlags();
	TEST_LINE(flags && BackupStoreDirectory::Entry::Flags_Deleted,
		rName + " should have been deleted");
	return flags && BackupStoreDirectory::Entry::Flags_Deleted;
}

bool compare(BackupQueries::ReturnCode::Type expected_status,
	const std::string& bbackupquery_options = "",
	const std::string& compare_options = "-acQ")
{
	std::string cmd = BBACKUPQUERY;
	cmd += " ";
	cmd += (expected_status == BackupQueries::ReturnCode::Compare_Same)
		? "-Wwarning" : "-Werror";
	cmd += " -c testfiles/bbackupd.conf ";
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

std::auto_ptr<Configuration> load_config_file(
	std::string config_file = "testfiles/bbackupd.conf")
{
	std::string errs;
	std::auto_ptr<Configuration> config(
		Configuration::LoadAndVerify
			("testfiles/bbackupd.conf", &BackupDaemonConfigVerify, errs));
	TEST_EQUAL_LINE(0, errs.size(), "Failed to load configuration file: " + errs);
	TEST_EQUAL_OR(0, errs.size(), config.reset());
	return config;
}

bool compare_local(BackupQueries::ReturnCode::Type expected_status,
	BackupProtocolCallable& client,
	const std::string& compare_options = "acQ")
{
	std::auto_ptr<Configuration> config = load_config_file();
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

TLSContext context;

#define TEST_COMPARE(...) \
	TEST_THAT(compare(BackupQueries::ReturnCode::__VA_ARGS__));
#define TEST_COMPARE_LOCAL(...) \
	TEST_THAT(compare_local(BackupQueries::ReturnCode::__VA_ARGS__));

bool search_for_file(const std::string& filename)
{
	std::auto_ptr<BackupProtocolCallable> client =
		connect_and_login(context, BackupProtocolLogin::Flags_ReadOnly);

	std::auto_ptr<BackupStoreDirectory> dir = ReadDirectory(*client);
	int64_t testDirId = SearchDir(*dir, filename);
	client->QueryFinished();

	return (testDirId != 0);
}

class MockClientContext : public BackupClientContext
{
public:
	BackupProtocolCallable& mrClient;
	MockClientContext
	(
		LocationResolver &rResolver,
		TLSContext &rTLSContext,
		const std::string &rHostname,
		int32_t Port,
		uint32_t AccountNumber,
		bool ExtendedLogging,
		bool ExtendedLogToFile,
		std::string ExtendedLogFile,
		ProgressNotifier &rProgressNotifier,
		bool TcpNiceMode,
		BackupProtocolCallable& rClient
	)
	: BackupClientContext(rResolver, rTLSContext,
		rHostname, Port, AccountNumber, ExtendedLogging,
		ExtendedLogToFile, ExtendedLogFile,
		rProgressNotifier, TcpNiceMode),
	  mrClient(rClient)
	{ }

	BackupProtocolCallable &GetConnection()
	{
		return mrClient;
	}
};

class MockBackupDaemon : public BackupDaemon {
	BackupProtocolCallable& mrClient;

public:
	MockBackupDaemon(BackupProtocolCallable &rClient)
	: mrClient(rClient)
	{ }

	std::auto_ptr<BackupClientContext> GetNewContext
	(
		LocationResolver &rResolver,
		TLSContext &rTLSContext,
		const std::string &rHostname,
		int32_t Port,
		uint32_t AccountNumber,
		bool ExtendedLogging,
		bool ExtendedLogToFile,
		std::string ExtendedLogFile,
		ProgressNotifier &rProgressNotifier,
		bool TcpNiceMode
	)
	{
		std::auto_ptr<BackupClientContext> context(
			new MockClientContext(rResolver,
				rTLSContext, rHostname, Port,
				AccountNumber, ExtendedLogging,
				ExtendedLogToFile, ExtendedLogFile,
				rProgressNotifier, TcpNiceMode, mrClient));
		return context;
	}
};

bool test_readdirectory_on_nonexistent_dir()
{
	SETUP_WITH_BBSTORED();

	{
		std::auto_ptr<BackupProtocolCallable> client = connect_and_login(
			context, 0 /* read-write */);
		
		{
			Logger::LevelGuard(Logging::GetConsole(), Log::ERROR);
			TEST_CHECK_THROWS(ReadDirectory(*client, 0x12345678),
				ConnectionException,
				Protocol_UnexpectedReply);
			TEST_PROTOCOL_ERROR_OR(*client, Err_DoesNotExist,);
		}

		client->QueryFinished();
	}

	TEARDOWN();
}

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

	std::auto_ptr<Configuration> config = load_config_file();
	TEST_THAT_OR(config.get(), return false);
	BackupQueries query(connection, *config, false); // read-only

	TEST_EQUAL(foo_id, query.FindDirectoryObjectID("foo"));
	TEST_EQUAL(foo_id, query.FindDirectoryObjectID("/foo"));
	TEST_EQUAL(0, query.FindDirectoryObjectID("\\/foo"));
	TEST_EQUAL(0, query.FindDirectoryObjectID("/bar"));
	TEST_EQUAL(bar_id, query.FindDirectoryObjectID("\\/bar"));
	connection.QueryFinished();

	TEARDOWN();
}

bool test_getobject_on_nonexistent_file()
{
	SETUP_WITH_BBSTORED();

	{
		std::auto_ptr<Configuration> config = load_config_file();
		TEST_THAT_OR(config.get(), return false);

		std::auto_ptr<BackupProtocolCallable> connection =
			connect_and_login(context, 0 /* read-write */);
		BackupQueries query(*connection, *config, false); // read-only
		std::vector<std::string> args;
		args.push_back("2"); // object ID
		args.push_back("testfiles/2.obj"); // output file
		bool opts[256];

		Capture capture;
		Logging::TempLoggerGuard guard(&capture);
		query.CommandGetObject(args, opts);
		std::vector<Capture::Message> messages = capture.GetMessages();
		TEST_THAT(!messages.empty());
		if (!messages.empty())
		{
			std::string last_message = messages.back().message;
			TEST_EQUAL("Object ID 0x2 does not exist on store.",
				last_message);
		}
	}
		
	TEARDOWN();
}

// ASSERT((mpBlockIndex == 0) || (NumBlocksInIndex != 0)) in
// BackupStoreFileEncodeStream::Recipe::Recipe once failed, apparently because
// a zero byte file had a block index but no entries in it. But this test
// doesn't reproduce the error, so it's not enabled for now.

bool test_replace_zero_byte_file_with_nonzero_byte_file()
{
	SETUP();

	TEST_THAT_OR(mkdir("testfiles/TestDir1", 0755) == 0, FAIL);
	FileStream emptyFile("testfiles/TestDir1/f2",
		O_WRONLY | O_CREAT | O_EXCL, 0755);
	wait_for_operation(5, "f2 to be old enough");

	BackupProtocolLocal2 client(0x01234567, "test",
		"backup/01234567/", 0, false);
	MockBackupDaemon bbackupd(client);
	TEST_THAT(configure_bbackupd(bbackupd, "testfiles/bbackupd.conf"));
	bbackupd.RunSyncNow();
	TEST_COMPARE_LOCAL(Compare_Same, client);

	MemBlockStream stream("Hello world");
	stream.CopyStreamTo(emptyFile);
	emptyFile.Close();
	wait_for_operation(5, "f2 to be old enough");

	bbackupd.RunSyncNow();
	TEST_COMPARE_LOCAL(Compare_Same, client);

	TEARDOWN();
}

// This caused the issue reported by Brendon Baumgartner and described in my
// email to the Box Backup list on Mon, 21 Apr 2014 at 18:44:38. If the
// directory disappears then we used to try to send an empty attributes block
// to the server, which is illegal.
bool test_backup_disappearing_directory()
{
	SETUP_WITH_BBSTORED();

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
		bbackupd, // rLocationResolver
		context,
		"localhost",
		BOX_PORT_BBSTORED_TEST,
		0x01234567,
		false, // ExtendedLogging
		false, // ExtendedLogFile
		"", // extendedLogFile
		bbackupd, // rProgressNotifier
		false // TcpNice
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

	BackupProtocolCallable& connection = clientContext.GetConnection();

	BackupClientFileAttributes attr;
	attr.ReadAttributes("testfiles/TestDir1",
		false /* put mod times in the attributes, please */);
	std::auto_ptr<IOStream> attrStream(new MemBlockStream(attr));
	BackupStoreFilenameClear dirname("Test1");
	std::auto_ptr<BackupProtocolSuccess>
		dirCreate(connection.QueryCreateDirectory(
			BACKUPSTORE_ROOT_DIRECTORY_ID, // containing directory
			0, // attrModTime,
			dirname, // dirname,
			attrStream));
		
	// Object ID for later creation
	int64_t oid = dirCreate->GetObjectID();
	BackupClientDirectoryRecordHooked record(oid, "Test1");

	TEST_COMPARE(Compare_Different);

	Location fakeLocation;
	record.SyncDirectory(params,
		BACKUPSTORE_ROOT_DIRECTORY_ID,
		"testfiles/TestDir1", // locationPath,
		"/whee", // remotePath
		fakeLocation);

	TEST_COMPARE(Compare_Same);

	// Run another backup, check that we haven't got an inconsistent
	// state that causes a crash.
	record.SyncDirectory(params,
		BACKUPSTORE_ROOT_DIRECTORY_ID,
		"testfiles/TestDir1", // locationPath,
		"/whee", // remotePath
		fakeLocation);
	TEST_COMPARE(Compare_Same);

	// Now recreate it and run another backup, check that we haven't got
	// an inconsistent state that causes a crash or prevents us from
	// creating the directory if it appears later.
	TEST_THAT(::mkdir("testfiles/TestDir1/dir23", 0755) == 0);
	TEST_COMPARE(Compare_Different);

	record.SyncDirectory(params,
		BACKUPSTORE_ROOT_DIRECTORY_ID,
		"testfiles/TestDir1", // locationPath,
		"/whee", // remotePath
		fakeLocation);
	TEST_COMPARE(Compare_Same);

	TEARDOWN();
}

// TODO FIXME check that directory modtimes are backed up by BackupClientDirectoryRecord.

bool test_ssl_keepalives()
{
	SETUP_WITH_BBSTORED();

#ifdef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
	BOX_NOTICE("Skipping intercept-based KeepAlive tests on this platform");
#else
	// Delete the test_base files unpacked by SETUP()
	TEST_THAT(::system("rm -r testfiles/TestDir1") == 0);
	// Unpack spacetest files instead
	TEST_THAT(::mkdir("testfiles/TestDir1", 0755) == 0);
	TEST_THAT(unpack_files("spacetest1", "testfiles/TestDir1"));

	// TODO FIXME dedent
	{
		#ifdef WIN32
		#error TODO: implement threads on Win32, or this test \
			will not finish properly
		#endif

		// something to diff against (empty file doesn't work)
		int fd = open("testfiles/TestDir1/spacetest/f1", O_WRONLY);
		TEST_THAT(fd > 0);

		char buffer[10000];
		memset(buffer, 0, sizeof(buffer));

		TEST_EQUAL_LINE(sizeof(buffer),
			write(fd, buffer, sizeof(buffer)),
			"Buffer write");
		TEST_THAT(close(fd) == 0);

		wait_for_operation(5, "f1 to be old enough");
	}
	
	// sleep to make it old enough to upload
	// is this really necessary? the files have old timestamps.
	bbackupd.RunSyncNow();

	// TODO FIXME dedent
	{
		#ifdef WIN32
		#error TODO: implement threads on Win32, or this test \
			will not finish properly
		#endif

		TEST_THAT(unlink("testfiles/bbackupd.log") == 0);

		// write again, to update the file's timestamp
		int fd = open("testfiles/TestDir1/spacetest/f1", O_WRONLY);
		TEST_THAT(fd > 0);
		char buffer[10000];
		memset(buffer, 0, sizeof(buffer));
		TEST_EQUAL_LINE(sizeof(buffer),
			write(fd, buffer, sizeof(buffer)),
			"Buffer write");
		TEST_THAT(close(fd) == 0);
		wait_for_operation(5, "modified file to be old enough");

		// two-second delay on the first read() of f1
		// should mean that a single keepalive is sent,
		// and diff does not abort.
		intercept_setup_delay("testfiles/TestDir1/spacetest/f1",
			0, 2000, SYS_read, 1);
		bbackupd.RunSyncNow();
		TEST_THAT(intercept_triggered());
		intercept_clear_setup();

		// check that keepalive was written to logs, and
		// diff was not aborted, i.e. upload was a diff
		FileStream fs("testfiles/bbackupd.log", O_RDONLY);
		IOStreamGetLine reader(fs);
		bool found1 = false;

		while (!reader.IsEOF())
		{
			std::string line;
			TEST_THAT(reader.GetLine(line));
			if (line == "Send GetBlockIndexByName(0x3,\"f1\")")
			{
				found1 = true;
				break;
			}
		}

		TEST_THAT_OR(found1, FAIL);
		// TODO FIXME dedent
		{
			std::string line;
			TEST_THAT(reader.GetLine(line));
			std::string comp = "Receive Success(0x";
			TEST_EQUAL_LINE(comp, line.substr(0, comp.size()), line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receiving stream, size 124", line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Send GetIsAlive()", line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receive IsAlive()", line);

			TEST_THAT(reader.GetLine(line));
			comp = "Send StoreFile(0x3,";
			TEST_EQUAL_LINE(comp, line.substr(0, comp.size()),
				line);
			comp = ",\"f1\")";
			std::string sub = line.substr(line.size() - comp.size());
			TEST_EQUAL_LINE(comp, sub, line);
			std::string comp2 = ",0x0,";
			sub = line.substr(line.size() - comp.size() -
				comp2.size() + 1, comp2.size());
			TEST_LINE(comp2 != sub, line);
		}

		// four-second delay on first read() of f1
		// should mean that no keepalives were sent,
		// because diff was immediately aborted
		// before any matching blocks could be found.
		intercept_setup_delay("testfiles/TestDir1/spacetest/f1", 
			0, 4000, SYS_read, 1);
		
		{
			fd = open("testfiles/TestDir1/spacetest/f1", O_WRONLY);
			TEST_THAT(fd > 0);
			// write again, to update the file's timestamp
			TEST_EQUAL_LINE(1, write(fd, "z", 1), "Buffer write");
			TEST_THAT(close(fd) == 0);

			// wait long enough to put file into sync window
			wait_for_operation(5, "locally modified file to "
				"mature for sync");

			bbackupd.RunSyncNow();
			TEST_THAT(intercept_triggered());
			intercept_clear_setup();
		}

		// check that the diff was aborted, i.e. upload was not a diff
		found1 = false;

		while (!reader.IsEOF())
		{
			std::string line;
			TEST_THAT(reader.GetLine(line));
			if (line == "Send GetBlockIndexByName(0x3,\"f1\")")
			{
				found1 = true;
				break;
			}
		}

		TEST_THAT_OR(found1, FAIL);
		// TODO FIXME dedent
		{
			std::string line;
			TEST_THAT(reader.GetLine(line));
			std::string comp = "Receive Success(0x";
			TEST_EQUAL_LINE(comp, line.substr(0, comp.size()),
				line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receiving stream, size 124", line);

			// delaying for 4 seconds in one step means that
			// the diff timer and the keepalive timer will
			// both expire, and the diff timer is honoured first,
			// so there will be no keepalives.

			TEST_THAT(reader.GetLine(line));
			comp = "Send StoreFile(0x3,";
			TEST_EQUAL_LINE(comp, line.substr(0, comp.size()),
				line);
			comp = ",0x0,\"f1\")";
			std::string sub = line.substr(line.size() - comp.size());
			TEST_EQUAL_LINE(comp, sub, line);
		}

		// Test that keepalives are sent while reading files
		{
			intercept_setup_delay("testfiles/TestDir1/spacetest/f1", 
				0, 1000, SYS_read, 3);

			// write again, to update the file's timestamp
			TEST_THAT(touch_and_wait("testfiles/TestDir1/spacetest/f1"));

			bbackupd.RunSyncNow();
			TEST_THAT(intercept_triggered());
			intercept_clear_setup();
		}

		// check that the diff was aborted, i.e. upload was not a diff
		found1 = false;

		while (!reader.IsEOF())
		{
			std::string line;
			TEST_THAT(reader.GetLine(line));
			if (line == "Send GetBlockIndexByName(0x3,\"f1\")")
			{
				found1 = true;
				break;
			}
		}

		TEST_THAT_OR(found1, FAIL);
		// TODO FIXME dedent
		{
			std::string line;
			TEST_THAT(reader.GetLine(line));
			std::string comp = "Receive Success(0x";
			TEST_EQUAL_LINE(comp, line.substr(0, comp.size()),
				line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receiving stream, size 124", line);

			// delaying for 3 seconds in steps of 1 second
			// means that the keepalive timer will expire 3 times,
			// and on the 3rd time the diff timer will expire too.
			// The diff timer is honoured first, so there will be 
			// only two keepalives.
			
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Send GetIsAlive()", line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receive IsAlive()", line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Send GetIsAlive()", line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receive IsAlive()", line);

			// but two matching blocks should have been found
			// already, so the upload should be a diff.

			TEST_THAT(reader.GetLine(line));
			comp = "Send StoreFile(0x3,";
			TEST_EQUAL_LINE(comp, line.substr(0, comp.size()),
				line);
			comp = ",\"f1\")";
			std::string sub = line.substr(line.size() - comp.size());
			TEST_EQUAL_LINE(comp, sub, line);
			std::string comp2 = ",0x0,";
			sub = line.substr(line.size() - comp.size() -
				comp2.size() + 1, comp2.size());
			TEST_LINE(comp2 != sub, line);
		}

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));
	}

	{
		// Test that keepalives are sent while reading large directories
		{
			intercept_setup_readdir_hook("testfiles/TestDir1/spacetest/d1", 
				readdir_test_hook_2);
			// time for two keepalives
			readdir_stop_time = time(NULL) + 2;
			TEST_THAT(::unlink("testfiles/bbackupd.log") == 0);

			bbackupd.RunSyncNow();
			TEST_THAT(intercept_triggered());
			intercept_clear_setup();
		}

		// check that keepalives were sent during the dir search
		FileStream fs("testfiles/bbackupd.log", O_RDONLY);
		IOStreamGetLine reader(fs);
		TEST_EQUAL("Send Version(0x1)", reader.GetLine());
		TEST_EQUAL("Receive Version(0x1)", reader.GetLine());
		TEST_EQUAL("Send Login(0x1234567,0x0)", reader.GetLine());
		TEST_STARTSWITH("Receive LoginConfirmed(", reader.GetLine());
		TEST_EQUAL("Send ListDirectory(0x1,0x2,0xc,false)", reader.GetLine());
		TEST_EQUAL("Receive Success(0x1)", reader.GetLine());
		TEST_STARTSWITH("Receiving stream, size ", reader.GetLine());
		TEST_EQUAL("Send GetIsAlive()", reader.GetLine());
		TEST_EQUAL("Receive IsAlive()", reader.GetLine());
		TEST_EQUAL("Send GetIsAlive()", reader.GetLine());
		TEST_EQUAL("Receive IsAlive()", reader.GetLine());
		TEST_EQUAL("Send ListDirectory(0x6,0xffff,0xc,true)",
			reader.GetLine()); // finished reading dir, download to compare

	// The following files should be on the server:
	// 00000001 -d---- 00002 (root)
	// 00000002 -d---- 00002 Test1
	// 00000003 -d---- 00002 Test1/spacetest
	// 00000004 f----- 00002 Test1/spacetest/f1
	// 00000005 f----- 00002 Test1/spacetest/f2
	// 00000006 -d---- 00002 Test1/spacetest/d1
	// 00000007 f----- 00002 Test1/spacetest/d1/f3
	// 00000008 f----- 00002 Test1/spacetest/d1/f4
	// 00000009 -d---- 00002 Test1/spacetest/d2
	// 0000000a -d---- 00002 Test1/spacetest/d3
	// 0000000b -d---- 00002 Test1/spacetest/d3/d4
	// 0000000c f----- 00002 Test1/spacetest/d3/d4/f5
	// 0000000d -d---- 00002 Test1/spacetest/d6
	// 0000000e -d---- 00002 Test1/spacetest/d7
	// 0000000f f--o-- 00002 Test1/spacetest/f1
	// 00000010 f--o-- 00002 Test1/spacetest/f1
	// 00000011 f----- 00002 Test1/spacetest/f1
	// This is 34 blocks total.

		std::auto_ptr<BackupProtocolCallable> client =
			connect_and_login(context, 0 /* read-write */);
		TEST_THAT(check_num_files(5, 3, 0, 9));
		TEST_THAT(check_num_blocks(*client, 10, 6, 0, 18, 34));
		client->QueryFinished();
	}
#endif // PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE

	TEARDOWN();
}

bool test_backup_pauses_when_store_is_full()
{
	SETUP_WITHOUT_FILES();
	unpack_files("spacetest1", "testfiles/TestDir1");
	TEST_THAT_OR(StartClient(), FAIL);

	// TODO FIXME dedent
	{
		// wait for files to be uploaded
		BOX_TRACE("Waiting for all outstanding files to be uploaded...")
		wait_for_sync_end();
		BOX_TRACE("Done. Comparing to check that it worked...")
		TEST_COMPARE(Compare_Same);

		// BLOCK
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context, 0 /* read-write */);
			TEST_THAT(check_num_files(5, 0, 0, 9));
			TEST_THAT(check_num_blocks(*client, 10, 0, 0, 18, 28));
			client->QueryFinished();
		}

		// Set limit to something very small.
		// 28 blocks are used at this point.
		// set soft limit to 0 to ensure that all deleted files
		// are deleted immediately by housekeeping
		TEST_THAT(change_account_limits("0B", "20B"));

		// Unpack some more files
		unpack_files("spacetest2", "testfiles/TestDir1");

		// Delete a file and a directory
		TEST_THAT(::unlink("testfiles/TestDir1/spacetest/f1") == 0);
		TEST_THAT(::system("rm -rf testfiles/TestDir1/spacetest/d7") == 0);

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

		BOX_TRACE("Waiting for sync for bbackupd to notice that the "
			"store is full");
		wait_for_sync_end();
		BOX_TRACE("Sync finished.");

		BOX_TRACE("Compare to check that there are differences");
		TEST_COMPARE(Compare_Different);

		// Check that the notify script was run
		TEST_THAT(TestFileExists("testfiles/notifyran.store-full.1"));
		// But only once!
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.2"));

		// BLOCK
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context, 0 /* read-write */);
			std::auto_ptr<BackupStoreDirectory> root_dir =
				ReadDirectory(*client, BACKUPSTORE_ROOT_DIRECTORY_ID);

			int64_t test_dir_id = SearchDir(*root_dir, "Test1");
			TEST_THAT_OR(test_dir_id, FAIL);
			std::auto_ptr<BackupStoreDirectory> test_dir =
				ReadDirectory(*client, test_dir_id);

			int64_t spacetest_dir_id = SearchDir(*test_dir, "spacetest");
			TEST_THAT_OR(spacetest_dir_id, FAIL);
			std::auto_ptr<BackupStoreDirectory> spacetest_dir =
				ReadDirectory(*client, spacetest_dir_id);

			int64_t d2_id = SearchDir(*spacetest_dir, "d2");
			int64_t d6_id = SearchDir(*spacetest_dir, "d6");
			TEST_THAT_OR(d2_id != 0, FAIL);
			TEST_THAT_OR(d6_id != 0, FAIL);
			std::auto_ptr<BackupStoreDirectory> d2_dir =
				ReadDirectory(*client, d2_id);
			std::auto_ptr<BackupStoreDirectory> d6_dir =
				ReadDirectory(*client, d6_id);

			// None of the new files should have been uploaded
			TEST_EQUAL(SearchDir(*d2_dir, "f6"), 0);
			TEST_EQUAL(SearchDir(*d6_dir, "d8"), 0);

			// But f1 and d7 should have been marked as deleted
			// (but not actually deleted yet)
			TEST_THAT(test_entry_deleted(*spacetest_dir, "f1"))
			TEST_THAT(test_entry_deleted(*spacetest_dir, "d7"))

			TEST_THAT(check_num_files(4, 0, 1, 9));
			TEST_THAT(check_num_blocks(*client, 8, 0, 2, 18, 28));
			client->QueryFinished();
		}
	}

	// Increase the limit again, check that all files are backed up on the
	// next run.
	TEST_THAT(change_account_limits("0B", "34B"));
	wait_for_sync_end();
	TEST_COMPARE(Compare_Same);

	TEARDOWN();
}

bool test_bbackupd_exclusions()
{
	SETUP_WITHOUT_FILES();

	TEST_THAT(unpack_files("spacetest1", "testfiles/TestDir1"));
	// Delete a file and a directory
	TEST_THAT(::unlink("testfiles/TestDir1/spacetest/f1") == 0);
	TEST_THAT(::system("rm -rf testfiles/TestDir1/spacetest/d7") == 0);

	// We need to be OVER the limit, i.e. >24 blocks, or
	// BackupClientContext will mark us over limit immediately on
	// connection.
	TEST_THAT(change_account_limits("0B", "25B"));

	// Initial run to get the files backed up
	{
		bbackupd.RunSyncNow();
		TEST_THAT(!bbackupd.StorageLimitExceeded());

		// BLOCK
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context, 0 /* read-write */);
			TEST_THAT(check_num_files(4, 0, 0, 8));
			TEST_THAT(check_num_blocks(*client, 8, 0, 0, 16, 24));
			client->QueryFinished();
		}
	}

	// Create a directory and then try to run a backup. This should try
	// to create the directory on the server, fail, and catch the error.
	// The directory that we create, spacetest/d6/d8, is included in
	// spacetest2.tgz, so we can ignore this for counting files after we
	// unpack spacetest2.tgz.
	TEST_THAT(::mkdir("testfiles/TestDir1/spacetest/d6/d8", 0755) == 0);
	bbackupd.RunSyncNow();
	TEST_THAT(bbackupd.StorageLimitExceeded());

	// BLOCK
	{
		TEST_THAT(unpack_files("spacetest2", "testfiles/TestDir1"));
		bbackupd.RunSyncNow();
		TEST_THAT(bbackupd.StorageLimitExceeded());

		// BLOCK
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context, 0 /* read-write */);
			TEST_THAT(check_num_files(4, 0, 0, 8));
			TEST_THAT(check_num_blocks(*client, 8, 0, 0, 16, 24));
			client->QueryFinished();
		}
	}

	// TODO FIXME dedent
	{
		// Start again with a new config that excludes d3 and f2,
		// and hence also d3/d4 and d3/d4/f5. bbackupd should mark
		// them as deleted and housekeeping should later clean up,
		// making space to upload the new files.
		// total required: (13-2-4+3)*2 = 20 blocks

		TEST_THAT(configure_bbackupd(bbackupd, "testfiles/bbackupd-exclude.conf"));
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

		BOX_INFO("Finding out whether bbackupd marked files as deleted");

		// TODO FIXME dedent
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context,
					BackupProtocolLogin::Flags_ReadOnly);
		
			std::auto_ptr<BackupStoreDirectory> rootDir =
				ReadDirectory(*client);

			int64_t testDirId = SearchDir(*rootDir, "Test1");
			TEST_THAT(testDirId != 0);

			std::auto_ptr<BackupStoreDirectory> Test1_dir =
				ReadDirectory(*client, testDirId);

			int64_t spacetestDirId = SearchDir(*Test1_dir,
				"spacetest");
			TEST_THAT(spacetestDirId != 0);

			std::auto_ptr<BackupStoreDirectory> spacetest_dir =
				ReadDirectory(*client, spacetestDirId);

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
				ReadDirectory(*client, d3_id);
			TEST_THAT(test_entry_deleted(*d3_dir, "d4"));

			int64_t d4_id = SearchDir(*d3_dir, "d4");
			TEST_THAT_OR(d4_id != 0, FAIL);

			std::auto_ptr<BackupStoreDirectory> d4_dir =
				ReadDirectory(*client, d4_id);
			TEST_THAT(test_entry_deleted(*d4_dir, "f5"));

			// d1/f3 and d1/f4 are the only two files on the
			// server which are not deleted, they use 2 blocks
			// each, the rest is directories and 2 deleted files
			// (f2 and d3/d4/f5)
			TEST_THAT(check_num_files(2, 0, 2, 8));
			TEST_THAT(check_num_blocks(*client, 4, 0, 4, 16, 24));

			// Log out.
			client->QueryFinished();
		}

		// Release our BackupClientContext and open connection, and
		// force housekeeping to run now.
		apClientContext.reset();
		TEST_THAT(run_housekeeping_and_check_account());

		BOX_INFO("Checking that the files were removed");
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context, 0 /* read-write */);

			std::auto_ptr<BackupStoreDirectory> rootDir =
				ReadDirectory(*client);

			int64_t testDirId = SearchDir(*rootDir, "Test1");
			TEST_THAT(testDirId != 0);

			std::auto_ptr<BackupStoreDirectory> Test1_dir =
				ReadDirectory(*client, testDirId);

			int64_t spacetestDirId = SearchDir(*Test1_dir,
				"spacetest");
			TEST_THAT(spacetestDirId != 0);

			std::auto_ptr<BackupStoreDirectory> spacetest_dir =
				ReadDirectory(*client, spacetestDirId);

			TEST_THAT(SearchDir(*spacetest_dir, "f1") == 0);
			TEST_THAT(SearchDir(*spacetest_dir, "f2") == 0);
			TEST_THAT(SearchDir(*spacetest_dir, "d3") == 0);
			TEST_THAT(SearchDir(*spacetest_dir, "d7") == 0);

			// f2, d3, d3/d4 and d3/d4/f5 have been removed.
			// The files were counted as deleted files before, the
			// deleted directories just as directories.
			TEST_THAT(check_num_files(2, 0, 0, 6));
			TEST_THAT(check_num_blocks(*client, 4, 0, 0, 12, 16));

			// Log out.
			client->QueryFinished();
		}

		// Need 22 blocks free to upload everything
		TEST_THAT_ABORTONFAIL(::system(BBSTOREACCOUNTS " -c "
			"testfiles/bbstored.conf setlimit 01234567 0B 22B") 
			== 0);
		TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");

		// Run another backup, now there should be enough space
		// for everything we want to upload.
		bbackupd.RunSyncNow();
		TEST_THAT(!bbackupd.StorageLimitExceeded());

		// Check that the contents of the store are the same 
		// as the contents of the disc 
		TEST_COMPARE(Compare_Same, "-c testfiles/bbackupd-exclude.conf");
		BOX_TRACE("done.");

		// BLOCK
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context, 0 /* read-write */);

			TEST_THAT(check_num_files(4, 0, 0, 7));
			TEST_THAT(check_num_blocks(*client, 8, 0, 0, 14, 22));

			// d2/f6, d6/d8 and d6/d8/f7 are new
			// i.e. 2 new files, 1 new directory

			client->QueryFinished();
		}
	}

	TEARDOWN();
}

bool test_bbackupd_uploads_files()
{
	SETUP_WITH_BBSTORED();

	// TODO FIXME dedent
	{
		// The files were all unpacked with timestamps in the past,
		// so no delay should be needed to make them eligible to be
		// backed up.
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);
	}

	// Check that no read error has been reported yet
	TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

	TEARDOWN();
}

bool test_bbackupd_responds_to_connection_failure()
{
	SETUP();
	TEST_THAT_OR(unpack_files("test_base"), FAIL);

#ifdef WIN32
	BOX_NOTICE("skipping test on this platform"); // requires fork
#else // !WIN32
	// TODO FIXME dedent
	{
		// create a new file to force an upload

		const char* new_file = "testfiles/TestDir1/force-upload-2";
		int fd = open(new_file, O_CREAT | O_EXCL | O_WRONLY, 0700);
		TEST_THAT_OR(fd >= 0,
			BOX_LOG_SYS_ERROR(BOX_FILE_MESSAGE(new_file,
				"failed to create new file"));
			FAIL);

		const char* control_string = "whee!\n";
		TEST_THAT(write(fd, control_string, 
			strlen(control_string)) ==
			(int)strlen(control_string));
		close(fd);

		wait_for_operation(5, "new file to be old enough");

		// Start a bbstored with a test hook that makes it terminate
		// on the first StoreFile command, breaking the connection to
		// bbackupd.
		class MyHook : public BackupStoreContext::TestHook
		{
		public:
			int trigger_count;
			MyHook() : trigger_count(0) { }

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

		class MockBackupProtocolLocal : public BackupProtocolLocal2
		{
		public:
			MyHook hook;
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

		MockBackupProtocolLocal client(0x01234567, "test",
			"backup/01234567/", 0, false);
		MockBackupDaemon bbackupd(client);
		TEST_THAT_OR(setup_test_bbackupd(bbackupd, false, false), FAIL);

		TEST_THAT(::system("rm -f testfiles/notifyran.store-full.*") == 0);
		std::auto_ptr<BackupClientContext> apClientContext;

		{
			Console& console(Logging::GetConsole());
			Logger::LevelGuard guard(console);

			if (console.GetLevel() < Log::TRACE)
			{
				console.Filter(Log::NOTHING);
			}

			apClientContext = bbackupd.RunSyncNowWithExceptionHandling();
		}

		// Should only have been triggered once
		TEST_EQUAL(1, client.hook.trigger_count);
		TEST_THAT(TestFileExists("testfiles/notifyran.backup-error.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-error.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.1"));
	}
#endif // !WIN32

	TEARDOWN();
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
		bbackupd.RunSyncNow();

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
		{
			int returnValue = ::system(BBACKUPQUERY " "
				"-c testfiles/bbackupd.conf "
				"-Wwarning \"restore Test1 testfiles/restore-symlink\" "
				"quit");
			TEST_RETURN(returnValue,
				BackupQueries::ReturnCode::Command_OK);
		}

		// make it accessible again
		TEST_THAT(chmod(SYM_DIR, 0755) == 0);

		// check that the original file was not overwritten
		FileStream fs(SYM_DIR "/a/subdir/content");
		IOStreamGetLine gl(fs);
		std::string line;
		TEST_THAT(gl.GetLine(line));
		TEST_THAT(line != "before");
		TEST_EQUAL("after", line);

		#undef SYM_DIR
	}
#endif

	TEARDOWN();
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
	TEARDOWN();
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
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);
	}

	// Now use a daemon with no temporary location, which should delete
	// it after 10 seconds
	{
		TEST_THAT(configure_bbackupd(bbackupd, "testfiles/bbackupd.conf"));

		// Initial run to start the countdown to destruction
		bbackupd.RunSyncNow();

		// Not deleted yet!
		TEST_THAT(search_for_file("Test2"));

		wait_for_operation(9, "just before Test2 should be deleted");
		bbackupd.RunSyncNow();
		TEST_THAT(search_for_file("Test2"));

		// Now wait until after it should be deleted
		wait_for_operation(2, "just after Test2 should be deleted");
		bbackupd.RunSyncNow();

		TEST_THAT(search_for_file("Test2"));
		std::auto_ptr<BackupProtocolCallable> client = connect_and_login(
			context, 0 /* read-write */);
		std::auto_ptr<BackupStoreDirectory> root_dir =
			ReadDirectory(*client, BACKUPSTORE_ROOT_DIRECTORY_ID);
		TEST_THAT(test_entry_deleted(*root_dir, "Test2"));
	}

	TEARDOWN();
}

// Check that read-only directories and their contents can be restored.
bool test_read_only_dirs_can_be_restored()
{
	SETUP_WITH_BBSTORED();

	// TODO FIXME dedent
	{
		{
			#ifdef WIN32
				// Cygwin chmod changes Windows file attributes
				TEST_THAT(::system("chmod 0555 testfiles/"
					"TestDir1/x1") == 0);
			#else
				TEST_THAT(chmod("testfiles/TestDir1/x1",
					0555) == 0);
			#endif

			bbackupd.RunSyncNow();
			TEST_COMPARE(Compare_Same, "", "-cEQ Test1 testfiles/TestDir1");

			// check that we can restore it
			TEST_THAT(restore("Test1", "testfiles/restore1"));
			TEST_COMPARE(Compare_Same, "", "-cEQ Test1 testfiles/restore1");

			// Try a restore with just the remote directory name,
			// check that it uses the same name in the local
			// directory.
			TEST_THAT(::mkdir("testfiles/restore-test", 0700) == 0);
			TEST_THAT(bbackupquery("\"lcd testfiles/restore-test\" "
				"\"restore Test1\"",
				"testfiles/restore-test/bbackupquery.memleaks"));
			TEST_COMPARE(Compare_Same, "", "-cEQ Test1 "
				"testfiles/restore-test/Test1");

			// put the permissions back to sensible values
			#ifdef WIN32
				TEST_THAT(::system("chmod 0755 testfiles/"
					"TestDir1/x1") == 0);
				TEST_THAT(::system("chmod 0755 testfiles/"
					"restore1/x1") == 0);
				TEST_THAT(::system("chmod 0755 testfiles/"
					"restore-test/Test1/x1") == 0);
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

	TEARDOWN();
}

// Check that filenames in UTF-8 can be backed up
bool test_unicode_filenames_can_be_backed_up()
{
	SETUP_WITH_BBSTORED();

#ifndef WIN32
	BOX_NOTICE("skipping test on this platform");
	// requires ConvertConsoleToUtf8()
#else
	// TODO FIXME dedent
	{
		// We have no guarantee that a random Unicode string can be
		// represented in the user's character set, so we go the 
		// other way, taking three random characters from the 
		// character set and converting them to Unicode. 
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

		std::string foreignCharsNative("\x91\x9b\x86");
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
		}

		bbackupd.RunSyncNow();

		// Compare to check that the file was uploaded
		TEST_COMPARE(Compare_Same);

		// Check that we can find it in directory listing
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context, 0);

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
		command = BBACKUPQUERY " -Wwarning "
			"-c testfiles/bbackupd.conf "
			"-q \"list Test1\" quit";
		pid_t bbackupquery_pid;
		std::auto_ptr<IOStream> queryout;
		queryout = LocalProcessStream(command.c_str(), 
			bbackupquery_pid);
		TEST_THAT(queryout.get() != NULL);
		TEST_THAT(bbackupquery_pid != -1);

		IOStreamGetLine reader(*queryout);
		std::string line;
		bool found = false;
		while (!reader.IsEOF())
		{
			TEST_THAT(reader.GetLine(line));
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
			TEST_THAT(reader2.GetLine(line));
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
		TEST_COMPARE(Compare_Same, "", "-cEQ Test1/" + systemDirName +
			" testfiles/TestDir1/" + systemDirName);

		// Check that bbackupquery can restore the dir when given
		// on the command line in system encoding.
		TEST_THAT(restore("Test1/" + systemDirName,
			"testfiles/restore-" + systemDirName));

		// Compare to make sure it was restored properly.
		TEST_COMPARE(Compare_Same, "", "-cEQ Test1/" + systemDirName +
			" testfiles/restore-" + systemDirName);

		std::string fileToUnlink = "testfiles/restore-" + 
			dirname + "/" + filename;
		TEST_THAT(::unlink(fileToUnlink.c_str()) == 0);

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
		TEST_THAT(::unlink(tmp.c_str()) == 0);

		// And after changing directory to an absolute path
		TEST_THAT(bbackupquery(
			"\"lcd " + cwd + "/testfiles\" "
			"\"cd Test1/" + systemDirName + "\" " + 
			"\"get " + systemFileName + "\""));

		// Compare to make sure it was restored properly.
		// The Get command does not restore attributes, so
		// we must compare without them (-A) to succeed.
		TEST_COMPARE(Compare_Same, "", "-cAEQ Test1/" + systemDirName +
			" testfiles/restore-" + systemDirName);

		// Compare without attributes. This should fail.
		TEST_COMPARE(Compare_Different, "", "-cEQ Test1/" + systemDirName +
			" testfiles/restore-" + systemDirName);

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));
	}
#endif // WIN32
	
	TEARDOWN();
}

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

			TEST_THAT(unlink(sync_control_file) == 0);
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

	TEARDOWN();
}

// Delete file and update another, create symlink.
bool test_delete_update_and_symlink_files()
{
	SETUP_WITH_BBSTORED();

	bbackupd.RunSyncNow();

	// TODO FIXME dedent
	{
		// Delete a file
		TEST_THAT(::unlink("testfiles/TestDir1/x1/dsfdsfs98.fd") == 0);

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

		bbackupd.RunSyncNow();

		// compare to make sure that it worked
		TEST_COMPARE(Compare_Same);

		// Try a quick compare, just for fun
		TEST_COMPARE(Compare_Same, "", "-acqQ");
	}

	TEARDOWN();
}

// Check that store errors are reported neatly. This test uses an independent
// daemon to check the daemon's backup loop delay, so it's easier to debug
// with the command: ./t -VTttest -e test_store_error_reporting
// --bbackupd-args=-kTtbbackupd
bool test_store_error_reporting()
{
	SETUP_WITH_BBSTORED();
	TEST_THAT(StartClient());
	wait_for_sync_end();

	// TODO FIXME dedent
	{
		// Check that store errors are reported neatly
		TEST_THAT(system("rm -f testfiles/notifyran.backup-error.*") == 0);

		// Break the store. We need a write lock on the account
		// while we do this, otherwise housekeeping might be running
		// and might rewrite the info files when it finishes,
		// undoing our breakage.
		std::string errs;
		std::auto_ptr<Configuration> config(
			Configuration::LoadAndVerify
				("testfiles/bbstored.conf", &BackupConfigFileVerify, errs));
		TEST_EQUAL_LINE(0, errs.size(), "Loading configuration file "
			"reported errors: " << errs);
		TEST_THAT_OR(config.get(), return false);
		std::auto_ptr<BackupStoreAccountDatabase> db(
			BackupStoreAccountDatabase::Read(
				config->GetKeyValue("AccountDatabase")));

		BackupStoreAccounts acc(*db);

		// Lock scope
		{
			NamedLock writeLock;
			acc.LockAccount(0x01234567, writeLock);

			TEST_THAT(::rename("testfiles/0_0/backup/01234567/info.rf",
				"testfiles/0_0/backup/01234567/info.rf.bak") == 0);
			TEST_THAT(::rename("testfiles/0_1/backup/01234567/info.rf",
				"testfiles/0_1/backup/01234567/info.rf.bak") == 0);
			TEST_THAT(::rename("testfiles/0_2/backup/01234567/info.rf",
				"testfiles/0_2/backup/01234567/info.rf.bak") == 0);
		}

		// Create a file to trigger an upload
		{
			int fd1 = open("testfiles/TestDir1/force-upload", 
				O_CREAT | O_EXCL | O_WRONLY, 0700);
			TEST_THAT(fd1 > 0);
			TEST_THAT(write(fd1, "just do it", 10) == 10);
			TEST_THAT(close(fd1) == 0);
		}

		wait_for_operation(4, "bbackupd to try to access the store");

		// Check that an error was reported just once
		TEST_THAT(TestFileExists("testfiles/notifyran.backup-error.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-error.2"));
		// Now kill bbackupd and start one that's running in
		// snapshot mode, check that it automatically syncs after
		// an error, without waiting for another sync command.
		TEST_THAT(StopClient());
		TEST_THAT(StartClient("testfiles/bbackupd-snapshot.conf"));
		sync_and_wait();

		// Check that the error was reported once more
		TEST_THAT(TestFileExists("testfiles/notifyran.backup-error.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-error.3"));

		// Fix the store (so that bbackupquery compare works)
		TEST_THAT(::rename("testfiles/0_0/backup/01234567/info.rf.bak",
			"testfiles/0_0/backup/01234567/info.rf") == 0);
		TEST_THAT(::rename("testfiles/0_1/backup/01234567/info.rf.bak",
			"testfiles/0_1/backup/01234567/info.rf") == 0);
		TEST_THAT(::rename("testfiles/0_2/backup/01234567/info.rf.bak",
			"testfiles/0_2/backup/01234567/info.rf") == 0);

		int store_fixed_time = time(NULL);

		// Check that we DO get errors on compare (cannot do this
		// until after we fix the store, which creates a race)
		TEST_COMPARE(Compare_Different);

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
		// extra seconds) from store_fixed_time, so check that it
		// hasn't run just before this time
		wait_for_operation(BACKUP_ERROR_DELAY_SHORTENED +
			(store_fixed_time - time(NULL)) - 1,
			"just before bbackupd recovers");
		TEST_THAT(!TestFileExists("testfiles/"
			"notifyran.backup-start.wait-snapshot.1"));

		// Should not have backed up, should still get errors
		TEST_COMPARE(Compare_Different);

		// wait another 2 seconds, bbackup should have run
		wait_for_operation(2, "bbackupd to recover");
		TEST_THAT(TestFileExists("testfiles/"
			"notifyran.backup-start.wait-snapshot.1"));
	
		// Check that it did get uploaded, and we have no more errors
		TEST_COMPARE(Compare_Same);

		TEST_THAT(::unlink("testfiles/notifyscript.tag") == 0);

		// Stop the snapshot bbackupd
		TEST_THAT(StopClient());

		// Break the store again
		TEST_THAT(::rename("testfiles/0_0/backup/01234567/info.rf",
			"testfiles/0_0/backup/01234567/info.rf.bak") == 0);
		TEST_THAT(::rename("testfiles/0_1/backup/01234567/info.rf",
			"testfiles/0_1/backup/01234567/info.rf.bak") == 0);
		TEST_THAT(::rename("testfiles/0_2/backup/01234567/info.rf",
			"testfiles/0_2/backup/01234567/info.rf.bak") == 0);

		// Modify a file to trigger an upload
		{
			int fd1 = open("testfiles/TestDir1/force-upload", 
				O_WRONLY, 0700);
			TEST_THAT(fd1 > 0);
			TEST_THAT(write(fd1, "and again", 9) == 9);
			TEST_THAT(close(fd1) == 0);
		}

		// Restart bbackupd in automatic mode
		TEST_THAT_OR(StartClient(), FAIL);
		sync_and_wait();

		// Fix the store again
		TEST_THAT(::rename("testfiles/0_0/backup/01234567/info.rf.bak",
			"testfiles/0_0/backup/01234567/info.rf") == 0);
		TEST_THAT(::rename("testfiles/0_1/backup/01234567/info.rf.bak",
			"testfiles/0_1/backup/01234567/info.rf") == 0);
		TEST_THAT(::rename("testfiles/0_2/backup/01234567/info.rf.bak",
			"testfiles/0_2/backup/01234567/info.rf") == 0);

		store_fixed_time = time(NULL);

		// Check that we DO get errors on compare (cannot do this
		// until after we fix the store, which creates a race)
		TEST_COMPARE(Compare_Different);

		// Test initial state
		TEST_THAT(!TestFileExists("testfiles/"
			"notifyran.backup-start.wait-automatic.1"));

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
		// extra seconds) from store_fixed_time, so check that it
		// hasn't run just before this time
		wait_for_operation(BACKUP_ERROR_DELAY_SHORTENED +
			(store_fixed_time - time(NULL)) - 1,
			"just before bbackupd recovers");
		TEST_THAT(!TestFileExists("testfiles/"
			"notifyran.backup-start.wait-automatic.1"));

		// Should not have backed up, should still get errors
		TEST_COMPARE(Compare_Different);

		// wait another 2 seconds, bbackup should have run
		wait_for_operation(2, "bbackupd to recover");
		TEST_THAT(TestFileExists("testfiles/"
			"notifyran.backup-start.wait-automatic.1"));
	
		// Check that it did get uploaded, and we have no more errors
		TEST_COMPARE(Compare_Same);

		TEST_THAT(::unlink("testfiles/notifyscript.tag") == 0);
	}

	TEARDOWN();
}

bool test_change_file_to_symlink_and_back()
{
	SETUP_WITH_BBSTORED();

	#ifndef WIN32
		// New symlink
		TEST_THAT(::symlink("does-not-exist",
			"testfiles/TestDir1/symlink-to-dir") == 0);
	#endif		

	bbackupd.RunSyncNow();

	// TODO FIXME dedent
	{
		// Bad case: delete a file/symlink, replace it with a directory.
		// Replace symlink with directory, add new directory.

		#ifndef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/symlink-to-dir")
				== 0);
		#endif

		TEST_THAT(::mkdir("testfiles/TestDir1/symlink-to-dir", 0755)
			== 0);
		TEST_THAT(::mkdir("testfiles/TestDir1/x1/dir-to-file", 0755)
			== 0);

		// NOTE: create a file within the directory to
		// avoid deletion by the housekeeping process later

		#ifndef WIN32
			TEST_THAT(::symlink("does-not-exist", 
				"testfiles/TestDir1/x1/dir-to-file/contents") 
				== 0);
		#endif

		wait_for_operation(5, "files to be old enough");
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);

		// And the inverse, replace a directory with a file/symlink

		#ifndef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/x1/dir-to-file"
				"/contents") == 0);
		#endif

		TEST_THAT(::rmdir("testfiles/TestDir1/x1/dir-to-file") == 0);

		#ifndef WIN32
			TEST_THAT(::symlink("does-not-exist", 
				"testfiles/TestDir1/x1/dir-to-file") == 0);
		#endif

		wait_for_operation(5, "files to be old enough");
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);
		
		// And then, put it back to how it was before.
		BOX_INFO("Replace symlink with directory (which was a symlink)");

		#ifndef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/x1"
				"/dir-to-file") == 0);
		#endif

		TEST_THAT(::mkdir("testfiles/TestDir1/x1/dir-to-file", 
			0755) == 0);

		#ifndef WIN32
			TEST_THAT(::symlink("does-not-exist", 
				"testfiles/TestDir1/x1/dir-to-file/contents2")
				== 0);
		#endif

		wait_for_operation(5, "files to be old enough");
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);

		// And finally, put it back to how it was before 
		// it was put back to how it was before
		// This gets lots of nasty things in the store with 
		// directories over other old directories.

		#ifndef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/x1/dir-to-file"
				"/contents2") == 0);
		#endif

		TEST_THAT(::rmdir("testfiles/TestDir1/x1/dir-to-file") == 0);

		#ifndef WIN32
			TEST_THAT(::symlink("does-not-exist", 
				"testfiles/TestDir1/x1/dir-to-file") == 0);
		#endif

		wait_for_operation(5, "files to be old enough");
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN();
}

bool test_file_rename_tracking()
{
	SETUP_WITH_BBSTORED();
	bbackupd.RunSyncNow();

	// TODO FIXME dedent
	{
		// rename an untracked file over an existing untracked file
		BOX_INFO("Rename over existing untracked file");
		int fd1 = open("testfiles/TestDir1/untracked-1", 
			O_CREAT | O_EXCL | O_WRONLY, 0700);
		int fd2 = open("testfiles/TestDir1/untracked-2",
			O_CREAT | O_EXCL | O_WRONLY, 0700);
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
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);

		#ifdef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/untracked-2")
				== 0);
		#endif

		TEST_THAT(::rename("testfiles/TestDir1/untracked-1", 
			"testfiles/TestDir1/untracked-2") == 0);
		TEST_THAT(!TestFileExists("testfiles/TestDir1/untracked-1"));
		TEST_THAT( TestFileExists("testfiles/TestDir1/untracked-2"));

		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);

		// case which went wrong: rename a tracked file over an
		// existing tracked file
		BOX_INFO("Rename over existing tracked file");
		fd1 = open("testfiles/TestDir1/tracked-1",
			O_CREAT | O_EXCL | O_WRONLY, 0700);
		fd2 = open("testfiles/TestDir1/tracked-2",
			O_CREAT | O_EXCL | O_WRONLY, 0700);
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
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);

		#ifdef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/tracked-2")
				== 0);
		#endif

		TEST_THAT(::rename("testfiles/TestDir1/tracked-1",
			"testfiles/TestDir1/tracked-2") == 0);
		TEST_THAT(!TestFileExists("testfiles/TestDir1/tracked-1"));
		TEST_THAT( TestFileExists("testfiles/TestDir1/tracked-2"));

		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);

		// case which went wrong: rename a tracked file
		// over a deleted file
		BOX_INFO("Rename an existing file over a deleted file");
		TEST_THAT(::unlink("testfiles/TestDir1/x1/dsfdsfs98.fd") == 0);
		TEST_THAT(::rename("testfiles/TestDir1/df9834.dsf",
			"testfiles/TestDir1/x1/dsfdsfs98.fd") == 0);

		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));
	}

	TEARDOWN();
}

// Files that suddenly appear, with timestamps before the last sync window,
// and files whose size or timestamp change, should still be uploaded, even
// though they look old.
bool test_upload_very_old_files()
{
	SETUP_WITH_BBSTORED();
	bbackupd.RunSyncNow();

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
		bbackupd.RunSyncNow();
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
				TEST_THAT(::system("chmod 0777 testfiles"
					"/TestDir1/sub23/rand.h") == 0);
			#else
				TEST_THAT(chmod("testfiles/TestDir1/sub23"
					"/rand.h", 0777) == 0);
			#endif

			FILE *f = fopen("testfiles/TestDir1/sub23/rand.h", 
				"w+");

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
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same); // files too new?

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));
	}

	TEARDOWN();
}

bool test_excluded_files_are_not_backed_up()
{
	// SETUP_WITH_BBSTORED();

	SETUP()

	BackupProtocolLocal2 client(0x01234567, "test", "backup/01234567/",
		0, false);
	MockBackupDaemon bbackupd(client);
	
	TEST_THAT_OR(setup_test_bbackupd(bbackupd,
		true, // do_unpack_files
		false // do_start_bbstored
		), FAIL);

	// TODO FIXME dedent
	{
		// Add some files and directories which are marked as excluded
		TEST_THAT(unpack_files("testexclude"));
		bbackupd.RunSyncNow();

		// compare with exclusions, should not find differences
		// TEST_COMPARE(Compare_Same);
		TEST_COMPARE_LOCAL(Compare_Same, client);

		// compare without exclusions, should find differences
		// TEST_COMPARE(Compare_Different, "", "-acEQ");
		TEST_COMPARE_LOCAL(Compare_Different, client, "acEQ");

		// check that the excluded files did not make it
		// into the store, and the included files did
		{
			/*
			std::auto_ptr<BackupProtocolCallable> pClient =
				connect_and_login(context,
				BackupProtocolLogin::Flags_ReadOnly);
			*/
			BackupProtocolCallable* pClient = &client;
			
			std::auto_ptr<BackupStoreDirectory> dir =
				ReadDirectory(*pClient);

			int64_t testDirId = SearchDir(*dir, "Test1");
			TEST_THAT(testDirId != 0);
			dir = ReadDirectory(*pClient, testDirId);
				
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
			dir = ReadDirectory(*pClient, sub23id);

			TEST_THAT(!SearchDir(*dir, "xx_not_this_dir_22"));
			TEST_THAT(!SearchDir(*dir, "somefile.excludethis"));

			// client->QueryFinished();
		}
	}

	TEARDOWN();
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

	TEARDOWN();
}

bool test_continuously_updated_file()
{
	SETUP_WITH_BBSTORED();
	TEST_THAT(StartClient());

	// TODO FIXME dedent
	{
		// Make sure everything happens at the same point in the 
		// sync cycle: wait until exactly the start of a sync
		wait_for_sync_start();

		// Then wait a second, to make sure the scan is complete
		::safe_sleep(1);

		{
			BOX_INFO("Open a file, then save something to it "
				"every second for 12 seconds");
			for(int l = 0; l < 12; ++l)
			{
				FILE *f = ::fopen("testfiles/TestDir1/continousupdate", "w+");
				TEST_THAT(f != 0);
				fprintf(f, "Loop iteration %d\n", l);
				fflush(f);
				fclose(f);
				safe_sleep(1);
			}
			
			// Check there's a difference
			int compareReturnValue = ::system("perl testfiles/"
				"extcheck1.pl");

			TEST_RETURN(compareReturnValue, 1);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			BOX_INFO("Keep on continuously updating file for "
				"28 seconds, check it is uploaded eventually");

			for(int l = 0; l < 28; ++l)
			{
				FILE *f = ::fopen("testfiles/TestDir1/"
					"continousupdate", "w+");
				TEST_THAT(f != 0);
				fprintf(f, "Loop 2 iteration %d\n", l);
				fflush(f);
				fclose(f);
				safe_sleep(1);
			}

			compareReturnValue = ::system("perl testfiles/"
				"extcheck2.pl");

			TEST_RETURN(compareReturnValue, 1);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		}
	}

	TEARDOWN();
}

bool test_delete_dir_change_attribute()
{
	SETUP_WITH_BBSTORED();
	bbackupd.RunSyncNow();

	// TODO FIXME dedent
	{
		// Delete a directory
		TEST_THAT(::system("rm -r testfiles/TestDir1/x1") == 0);

		// Change attributes on an existing file.
#ifdef WIN32
		TEST_EQUAL(0, system("chmod 0423 testfiles/TestDir1/df9834.dsf"));
#else
		TEST_THAT(::chmod("testfiles/TestDir1/df9834.dsf", 0423) == 0);
#endif

		TEST_COMPARE(Compare_Different);
		
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN();
}

bool test_restore_files_and_directories()
{
	SETUP_WITH_BBSTORED();
	bbackupd.RunSyncNow();

	// TODO FIXME dedent
	{
		int64_t deldirid = 0;
		int64_t restoredirid = 0;
		{
			// connect and log in
			std::auto_ptr<BackupProtocolCallable> client = 
				connect_and_login(context,
					BackupProtocolLogin::Flags_ReadOnly);

			// Find the ID of the Test1 directory
			restoredirid = GetDirID(*client, "Test1",
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
			deldirid = GetDirID(*client, "x1", restoredirid);
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
			printf("\n==== Try to restore to a path "
				"that doesn't exist\n");
			fflush(stdout);

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

	TEARDOWN();
}

bool test_compare_detects_attribute_changes()
{
	SETUP_WITH_BBSTORED();

#ifndef WIN32
	BOX_NOTICE("skipping test on this platform");
	// requires openfile(), GetFileTime() and attrib.exe
#else
	bbackupd.RunSyncNow()

	// TODO FIXME dedent
	{
		// make one of the files read-only, expect a compare failure
		compareReturnValue = ::system("attrib +r "
			"testfiles\\restore-Test1\\f1.dat");
		TEST_RETURN(compareReturnValue, 0);

		TEST_COMPARE(Compare_Different);
	
		// set it back, expect no failures
		compareReturnValue = ::system("attrib -r "
			"testfiles\\restore-Test1\\f1.dat");
		TEST_RETURN(compareReturnValue, 0);

		TEST_COMPARE(Compare_Same);

		// change the timestamp on a file, expect a compare failure
		char* testfile = "testfiles\\restore-Test1\\f1.dat";
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
#endif // WIN32

	TEARDOWN();
}

bool test_sync_new_files()
{
	SETUP_WITH_BBSTORED();
	bbackupd.RunSyncNow();

	// TODO FIXME dedent
	{
		// Add some more files and modify others
		// Use the m flag this time so they have a recent modification time
		TEST_THAT(unpack_files("test3", "testfiles", "-m"));
		
		// Wait and test
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Different);

		wait_for_operation(5, "newly added files to be old enough");
		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN();
}

bool test_rename_operations()
{
	SETUP_WITH_BBSTORED();

	TEST_THAT(unpack_files("test2"));
	TEST_THAT(unpack_files("test3"));
	bbackupd.RunSyncNow();
	TEST_COMPARE(Compare_Same);

	// TODO FIXME dedent
	{
		BOX_INFO("Rename directory");
		TEST_THAT(rename("testfiles/TestDir1/sub23/dhsfdss", 
			"testfiles/TestDir1/renamed-dir") == 0);

		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);

		// and again, but with quick flag
		TEST_COMPARE(Compare_Same, "", "-acqQ");

		// Rename some files -- one under the threshold, others above
		TEST_THAT(rename("testfiles/TestDir1/df324", 
			"testfiles/TestDir1/df324-ren") == 0);
		TEST_THAT(rename("testfiles/TestDir1/sub23/find2perl", 
			"testfiles/TestDir1/find2perl-ren") == 0);

		bbackupd.RunSyncNow();
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN();
}

// Check that modifying files with madly in the future timestamps still get added
bool test_sync_files_with_timestamps_in_future()
{
	SETUP_WITH_BBSTORED();
	bbackupd.RunSyncNow();

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
		bbackupd.RunSyncNow();
		wait_for_backup_operation("bbackup to sync future file");
		TEST_COMPARE(Compare_Same);
	}

	TEARDOWN();
}

// Check change of store marker pauses daemon
bool test_changing_client_store_marker_pauses_daemon()
{
	SETUP_WITH_BBSTORED();
	TEST_THAT(StartClient());

	// Wait for the client to upload all current files. We also time
	// approximately how long a sync takes.
	box_time_t sync_start_time = GetCurrentBoxTime();
	sync_and_wait();
	box_time_t sync_time = GetCurrentBoxTime() - sync_start_time;

	// Time how long a compare takes. On NetBSD it's 3 seconds, and that 
	// interferes with test timing unless we account for it.
	box_time_t compare_start_time = GetCurrentBoxTime();
	// There should be no differences right now (yet).
	TEST_COMPARE(Compare_Same);
	box_time_t compare_time = GetCurrentBoxTime() - compare_start_time;
	BOX_TRACE("Compare takes " << BOX_FORMAT_MICROSECONDS(compare_time));

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
					std::auto_ptr<BackupProtocolCallable>
						protocol = connect_to_bbstored(context);
					// Make sure the marker isn't zero,
					// because that's the default, and
					// it should have changed
					std::auto_ptr<BackupProtocolLoginConfirmed> loginConf(protocol->QueryLogin(0x01234567, 0));
					TEST_THAT(loginConf->GetClientStoreMarker() != 0);
					
					// Change it to something else
					BOX_INFO("Changing client store marker "
						"from " << loginConf->GetClientStoreMarker() <<
						" to 12");
					protocol->QuerySetClientStoreMarker(12);
					
					// Success!
					done = true;
					
					// Log out
					protocol->QueryFinished();
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
		BOX_TRACE("Compare starting, expecting differences");
		TEST_COMPARE(Compare_Different);
		BOX_TRACE("Compare finished, expected differences");

		// Wait out the expected delay in bbackupd. This is quite
		// time-sensitive, so we use sub-second precision.
		box_time_t wait = 
			SecondsToBoxTime(BACKUP_ERROR_DELAY_SHORTENED - 1) -
			compare_time * 2;
		BOX_TRACE("Waiting for " << BOX_FORMAT_MICROSECONDS(wait) <<
			" (plus another compare taking " <<
			BOX_FORMAT_MICROSECONDS(compare_time) << ") until "
			"just before bbackupd recovers");
		ShortSleep(wait, true);

		// bbackupd should not have recovered yet, so there should
		// still be differences.
		BOX_TRACE("Compare starting, expecting differences");
		TEST_COMPARE(Compare_Different);
		BOX_TRACE("Compare finished, expected differences");

		// Now wait for it to recover and finish a sync, and check
		// that the differences are gone (successful backup).
		wait = sync_time + SecondsToBoxTime(2);
		BOX_TRACE("Waiting for " << BOX_FORMAT_MICROSECONDS(wait) <<
			" until just after bbackupd recovers and finishes a sync");
		ShortSleep(wait, true);

		BOX_TRACE("Compare starting, expecting no differences");
		TEST_COMPARE(Compare_Same);
		BOX_TRACE("Compare finished, expected no differences");
	}

	TEARDOWN();
}

bool test_interrupted_restore_can_be_recovered()
{
	SETUP_WITH_BBSTORED();

#ifdef WIN32
	BOX_NOTICE("skipping test on this platform");
#else
	bbackupd.RunSyncNow();

	// TODO FIXME dedent
	{
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context,
					BackupProtocolLogin::Flags_ReadOnly);

			// Find the ID of the Test1 directory
			int64_t restoredirid = GetDirID(*client, "Test1",
				BackupProtocolListDirectory::RootDirectory);
			TEST_THAT_OR(restoredirid != 0, FAIL);

			do_interrupted_restore(context, restoredirid);
			int64_t resumesize = 0;
			TEST_THAT(FileExists("testfiles/"
				"restore-interrupt.boxbackupresume",
				&resumesize));
			// make sure it has recorded something to resume
			TEST_THAT(resumesize > 16);

			// Check that the restore fn returns resume possible,
			// rather than doing anything
			TEST_THAT(BackupClientRestore(*client, restoredirid,
				"Test1", "testfiles/restore-interrupt",
				true /* print progress dots */, 
				false /* restore deleted */, 
				false /* undelete after */, 
				false /* resume */,
				false /* keep going */) 
				== Restore_ResumePossible);

			// Then resume it
			TEST_THAT(BackupClientRestore(*client, restoredirid,
				"Test1", "testfiles/restore-interrupt",
				true /* print progress dots */, 
				false /* restore deleted */, 
				false /* undelete after */, 
				true /* resume */,
				false /* keep going */) 
				== Restore_Complete);

			client->QueryFinished();
			client.reset();

			// Then check it has restored the correct stuff
			TEST_COMPARE(Compare_Same);
		}
	}
#endif // !WIN32

	TEARDOWN();
}

bool assert_x1_deleted_or_not(bool expected_deleted)
{
	std::auto_ptr<BackupProtocolCallable> client =
		connect_and_login(context, 0 /* read-write */);
	
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

	bbackupd.RunSyncNow();
	TEST_COMPARE(Compare_Same);

	TEST_THAT(::unlink("testfiles/TestDir1/f1.dat") == 0);
	TEST_THAT(::system("rm -r testfiles/TestDir1/x1") == 0);
	TEST_COMPARE(Compare_Different);

	bbackupd.RunSyncNow();
	TEST_COMPARE(Compare_Same);
	TEST_THAT(assert_x1_deleted_or_not(true));

	// TODO FIXME dedent
	{
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context, 0 /* read-write */);

			// Find the ID of the Test1 directory
			int64_t restoredirid = GetDirID(*client, "Test1",
				BackupProtocolListDirectory::RootDirectory);
			TEST_THAT_OR(restoredirid != 0, FAIL);

			// Find ID of the deleted directory
			int64_t deldirid = GetDirID(*client, "x1", restoredirid);
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
			TEST_COMPARE(Compare_Same, "", "-cEQ Test1/x1 "
				"testfiles/restore-Test1-x1-2");
		}
		
		// Final check on notifications
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.2"));
	}

	// should have been undeleted by restore
	TEST_THAT(assert_x1_deleted_or_not(false));

	TEARDOWN();
}

bool test_locked_file_behaviour()
{
	SETUP_WITH_BBSTORED();

#ifndef WIN32
	BOX_NOTICE("skipping test on this platform");
#else
	// TODO FIXME dedent
	{
		// Test that locked files cannot be backed up,
		// and the appropriate error is reported.

		handle = openfile("testfiles/TestDir1/f1.dat", O_LOCK);
		TEST_THAT_OR(handle != INVALID_HANDLE_VALUE, FAIL);

		{
			// this sync should try to back up the file, 
			// and fail, because it's locked
			bbackupd.RunSyncNow();
			TEST_THAT(TestFileExists("testfiles/"
				"notifyran.read-error.1"));
			TEST_THAT(!TestFileExists("testfiles/"
				"notifyran.read-error.2"));
		}

		{
			// now close the file and check that it is
			// backed up on the next run.
			CloseHandle(handle);
			bbackupd.RunSyncNow();

			// still no read errors?
			TEST_THAT(!TestFileExists("testfiles/"
				"notifyran.read-error.2"));
			TEST_COMPARE(Compare_Same);
		}

		{
			// open the file again, compare and check that compare
			// reports the correct error message (and finishes)
			handle = openfile("testfiles/TestDir1/f1.dat",
				O_LOCK, 0);
			TEST_THAT_OR(handle != INVALID_HANDLE_VALUE, FAIL);

			TEST_COMPARE(Compare_Error);

			// close the file again, check that compare
			// works again
			CloseHandle(handle);
			TEST_COMPARE(Compare_Same);
		}
	}
#endif // WIN32

	TEARDOWN();
}

bool test_backup_many_files()
{
	SETUP_WITH_BBSTORED();

	unpack_files("test2");
	unpack_files("test3");
	unpack_files("testexclude");
	unpack_files("spacetest1", "testfiles/TestDir1");
	unpack_files("spacetest2", "testfiles/TestDir1");

	bbackupd.RunSyncNow();
	TEST_COMPARE(Compare_Same);

	TEARDOWN();
}

bool test_parse_incomplete_command()
{
	SETUP();

	{
		// This is not a complete command, it should not parse!
		BackupQueries::ParsedCommand cmd("-od", true);
		TEST_THAT(cmd.mFailed);
		TEST_EQUAL((void *)NULL, cmd.pSpec);
		TEST_EQUAL(0, cmd.mCompleteArgCount);
	}

	TEARDOWN();
}

bool test_parse_syncallowscript_output()
{
	SETUP();

	{
		BackupDaemon daemon;

		TEST_EQUAL(1234, daemon.ParseSyncAllowScriptOutput("test", "1234"));
		TEST_EQUAL(0, daemon.GetMaxBandwidthFromSyncAllowScript());

		TEST_EQUAL(1234, daemon.ParseSyncAllowScriptOutput("test", "1234 5"));
		TEST_EQUAL(5, daemon.GetMaxBandwidthFromSyncAllowScript());

		TEST_EQUAL(-1, daemon.ParseSyncAllowScriptOutput("test", "now"));
		TEST_EQUAL(0, daemon.GetMaxBandwidthFromSyncAllowScript());
	}

	TEARDOWN();
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

	context.Initialise(false /* client */,
			"testfiles/clientCerts.pem",
			"testfiles/clientPrivKey.pem",
			"testfiles/clientTrustedCAs.pem");

	TEST_THAT(test_basics());
	TEST_THAT(test_readdirectory_on_nonexistent_dir());
	TEST_THAT(test_bbackupquery_parser_escape_slashes());
	TEST_THAT(test_getobject_on_nonexistent_file());
	// TEST_THAT(test_replace_zero_byte_file_with_nonzero_byte_file());
	TEST_THAT(test_backup_disappearing_directory());
	TEST_THAT(test_ssl_keepalives());
	TEST_THAT(test_backup_pauses_when_store_is_full());
	TEST_THAT(test_bbackupd_exclusions());
	TEST_THAT(test_bbackupd_uploads_files());
	TEST_THAT(test_bbackupd_responds_to_connection_failure());
	TEST_THAT(test_absolute_symlinks_not_followed_during_restore());
	TEST_THAT(test_initially_missing_locations_are_not_forgotten());
	TEST_THAT(test_redundant_locations_deleted_on_time());
	TEST_THAT(test_read_only_dirs_can_be_restored());
	TEST_THAT(test_unicode_filenames_can_be_backed_up());
	TEST_THAT(test_sync_allow_script_can_pause_backup());
	TEST_THAT(test_delete_update_and_symlink_files());
	TEST_THAT(test_store_error_reporting());
	TEST_THAT(test_change_file_to_symlink_and_back());
	TEST_THAT(test_file_rename_tracking());
	TEST_THAT(test_upload_very_old_files());
	TEST_THAT(test_excluded_files_are_not_backed_up());
	TEST_THAT(test_read_error_reporting());
	TEST_THAT(test_continuously_updated_file());
	TEST_THAT(test_delete_dir_change_attribute());
	TEST_THAT(test_restore_files_and_directories());
	TEST_THAT(test_compare_detects_attribute_changes());
	TEST_THAT(test_sync_new_files());
	TEST_THAT(test_rename_operations());
	TEST_THAT(test_sync_files_with_timestamps_in_future());
	TEST_THAT(test_changing_client_store_marker_pauses_daemon());
	TEST_THAT(test_interrupted_restore_can_be_recovered());
	TEST_THAT(test_restore_deleted_files());
	TEST_THAT(test_locked_file_behaviour());
	TEST_THAT(test_backup_many_files());
	TEST_THAT(test_parse_incomplete_command());
	TEST_THAT(test_parse_syncallowscript_output());

	typedef std::map<std::string, std::string>::iterator s_test_status_iterator;
	for(s_test_status_iterator i = s_test_status.begin();
		i != s_test_status.end(); i++)
	{
		BOX_NOTICE("test result: " << i->second << ": " << i->first);
	}

#ifndef WIN32
	if(::getuid() == 0)
	{
		BOX_WARNING("This test was run as root. Some tests have been omitted.");
	}
#endif

	TEST_LINE(num_tests_selected > 0, "No tests matched the patterns "
		"specified on the command line");

	return (failures == 0 && num_tests_selected > 0);
}
