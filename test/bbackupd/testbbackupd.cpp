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

#include "autogen_BackupProtocol.h"
#include "BackupClientCryptoKeys.h"
#include "BackupClientFileAttributes.h"
#include "BackupClientRestore.h"
#include "BackupDaemon.h"
#include "BackupDaemonConfigVerify.h"
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

int bbackupd_pid = 0;

bool StartClient(const std::string& bbackupd_conf_file = "testfiles/bbackupd.conf")
{
	TEST_THAT_OR(bbackupd_pid == 0, FAIL);

	std::string cmd = BBACKUPD " " + bbackupd_args + " " + bbackupd_conf_file;
	bbackupd_pid = LaunchServer(cmd.c_str(), "testfiles/bbackupd.pid");

	TEST_THAT_OR(bbackupd_pid != -1 && bbackupd_pid != 0, FAIL);
	::sleep(1);
	TEST_THAT_OR(ServerIsAlive(bbackupd_pid), FAIL);

	return true;
}

bool StopClient(bool wait_for_process = false)
{
	TEST_THAT_OR(bbackupd_pid != 0, FAIL);
	TEST_THAT_OR(ServerIsAlive(bbackupd_pid), FAIL);
	TEST_THAT_OR(KillServer(bbackupd_pid, wait_for_process), FAIL);
	::sleep(1);

	TEST_THAT_OR(!ServerIsAlive(bbackupd_pid), FAIL);
	bbackupd_pid = 0;

	#ifdef WIN32
		int unlink_result = unlink("testfiles/bbackupd.pid");
		TEST_EQUAL_LINE(0, unlink_result, "unlink testfiles/bbackupd.pid");
		if(unlink_result != 0)
		{
			FAIL;
		}
	#else
		TestRemoteProcessMemLeaks("bbackupd.memleaks");
	#endif

	return true;
}

#ifdef HAVE_SYS_XATTR_H
bool readxattr_into_map(const char *filename, std::map<std::string,std::string> &rOutput)
{
	rOutput.clear();
	
	ssize_t xattrNamesBufferSize = llistxattr(filename, NULL, 0);
	if(xattrNamesBufferSize < 0)
	{
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
#ifdef WIN32
	std::string cmd("tar xzv ");
	cmd += tar_options + " -f testfiles/" + archive_file + ".tgz " +
		"-C " + destination_dir;
#else
	std::string cmd("gzip -d < testfiles/");
	cmd += archive_file + ".tgz | ( cd " + destination_dir + " && tar xv " +
		tar_options + ")";
#endif

	TEST_THAT_OR(::system(cmd.c_str()) == 0, return false);
	return true;
}

Daemon* spDaemon = NULL;

bool configure_bbackupd(BackupDaemon& bbackupd, const std::string& config_file)
{
	// Stop bbackupd initialisation from changing the console logging level
	Logger& console(Logging::GetConsole());
	Logger::LevelGuard guard(console, console.GetLevel());

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
	for (int i = 0; i < args.size(); i++)
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
		s_test_status[test_name] = "failed";
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

std::auto_ptr<BackupProtocolClient> Connect(TLSContext& rContext)
{
	SocketStreamTLS* pConn = new SocketStreamTLS;
	std::auto_ptr<SocketStream> apConn(pConn);
	pConn->Open(rContext, Socket::TypeINET, "localhost", 22011);

	std::auto_ptr<BackupProtocolClient> client;
	client.reset(new BackupProtocolClient(apConn));
	client->Handshake();
	std::auto_ptr<BackupProtocolVersion> 
		serverVersion(client->QueryVersion(
			BACKUP_STORE_SERVER_VERSION));
	if(serverVersion->GetVersion() != 
		BACKUP_STORE_SERVER_VERSION)
	{
		THROW_EXCEPTION(BackupStoreException, 
			WrongServerVersion);
	}
	return client;
}

std::auto_ptr<BackupProtocolClient> ConnectAndLogin(TLSContext& rContext,
	int flags)
{
	std::auto_ptr<BackupProtocolClient> client(Connect(rContext));
	client->QueryLogin(0x01234567, flags);
	return client;
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
		new BackupStoreDirectory(rClient.ReceiveStream()));
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
	::safe_sleep(1);

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

#define TEST_COMPARE(...) \
	TEST_THAT(compare(BackupQueries::ReturnCode::__VA_ARGS__));

int test_bbackupd()
{
	// First, wait for a normal period to make sure the last changes 
	// attributes are within a normal backup timeframe.
	// wait_for_backup_operation();

	// Connection gubbins
	TLSContext context;
	context.Initialise(false /* client */,
			"testfiles/clientCerts.pem",
			"testfiles/clientPrivKey.pem",
			"testfiles/clientTrustedCAs.pem");

	printf("\n==== Testing that ReadDirectory on nonexistent directory "
		"does not crash\n");
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

	printf("\n==== Testing that GetObject on nonexistent file outputs the "
		"correct error message\n");
	{
		std::auto_ptr<BackupProtocolCallable> connection =
			connect_and_login(context, 0 /* read-write */);
		std::string errs;
		std::auto_ptr<Configuration> config(
			Configuration::LoadAndVerify
				("testfiles/bbackupd.conf", &BackupDaemonConfigVerify, errs));
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
		
	// unpack the files for the initial test
	TEST_THAT(::system("rm -rf testfiles/TestDir1") == 0);
	TEST_THAT(::mkdir("testfiles/TestDir1", 0777) == 0);

	#ifdef WIN32
		TEST_THAT(::system("tar xzvf testfiles/spacetest1.tgz "
			"-C testfiles/TestDir1") == 0);
	#else
		TEST_THAT(::system("gzip -d < testfiles/spacetest1.tgz "
			"| ( cd testfiles/TestDir1 && tar xf - )") == 0);
	#endif

#ifdef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
	printf("\n==== Skipping intercept-based KeepAlive tests "
		"on this platform.\n");
#else
	printf("\n==== Testing SSL KeepAlive messages\n");

	{
		#ifdef WIN32
		#error TODO: implement threads on Win32, or this test \
			will not finish properly
		#endif

		// bbackupd daemon will try to initialise timers itself
		Timers::Cleanup();
		
		// something to diff against (empty file doesn't work)
		int fd = open("testfiles/TestDir1/spacetest/f1", O_WRONLY);
		TEST_THAT(fd > 0);

		char buffer[10000];
		memset(buffer, 0, sizeof(buffer));

		TEST_EQUAL_LINE(sizeof(buffer),
			write(fd, buffer, sizeof(buffer)),
			"Buffer write");
		TEST_THAT(close(fd) == 0);
		
		int pid = start_internal_daemon();
		wait_for_backup_operation("internal daemon to run a sync");
		TEST_THAT(stop_internal_daemon(pid));

		// two-second delay on the first read() of f1
		// should mean that a single keepalive is sent,
		// and diff does not abort.
		intercept_setup_delay("testfiles/TestDir1/spacetest/f1", 
			0, 2000, SYS_read, 1);
		TEST_THAT(unlink("testfiles/bbackupd.log") == 0);

		pid = start_internal_daemon();
		intercept_clear_setup();
		
		fd = open("testfiles/TestDir1/spacetest/f1", O_WRONLY);
		TEST_THAT(fd > 0);
		// write again, to update the file's timestamp
		TEST_EQUAL_LINE(sizeof(buffer),
			write(fd, buffer, sizeof(buffer)),
			"Buffer write");
		TEST_THAT(close(fd) == 0);	

		wait_for_backup_operation("internal daemon to sync "
			"spacetest/f1");
		// can't test whether intercept was triggered, because
		// it's in a different process.
		// TEST_THAT(intercept_triggered());
		TEST_THAT(stop_internal_daemon(pid));

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

		TEST_THAT(found1);
		if (found1)
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

		// Remaining tests require timer system to be initialised already
		Timers::Init();
		
		// stop early to make debugging easier
		if (failures) return 1;

		// four-second delay on first read() of f1
		// should mean that no keepalives were sent,
		// because diff was immediately aborted
		// before any matching blocks could be found.
		intercept_setup_delay("testfiles/TestDir1/spacetest/f1", 
			0, 4000, SYS_read, 1);
		
		{
			BackupDaemon bbackupd;
			bbackupd.Configure("testfiles/bbackupd.conf");
			bbackupd.InitCrypto();
		
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

		TEST_THAT(found1);
		if (found1)
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

		if (failures > 0)
		{
			// stop early to make debugging easier
			return 1;
		}

		// Test that keepalives are sent while reading files
		{
			BackupDaemon bbackupd;
			bbackupd.Configure("testfiles/bbackupd.conf");
			bbackupd.InitCrypto();
		
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

		TEST_THAT(found1);
		if (found1)
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

		if (failures > 0)
		{
			// stop early to make debugging easier
			Timers::Init();
			return 1;
		}

		// Test that keepalives are sent while reading large directories
		{
			BackupDaemon bbackupd;
			bbackupd.Configure("testfiles/bbackupd.conf");
			bbackupd.InitCrypto();
	
			intercept_setup_readdir_hook("testfiles/TestDir1/spacetest/d1", 
				readdir_test_hook_2);
			// time for at least two keepalives
			readdir_stop_time = time(NULL) + 12 + 2;

			bbackupd.RunSyncNow();
			TEST_THAT(intercept_triggered());
			intercept_clear_setup();
		}

		// check that keepalives were sent during the dir search
		found1 = false;

		// skip to next login
		while (!reader.IsEOF())
		{
			std::string line;
			TEST_THAT(reader.GetLine(line));
			if (line == "Send ListDirectory(0x3,0xffff,0xc,true)")
			{
				found1 = true;
				break;
			}
		}

		TEST_THAT(found1);
		if (found1)
		{
			found1 = false;

			while (!reader.IsEOF())
			{
				std::string line;
				TEST_THAT(reader.GetLine(line));
				if (line == "Send ListDirectory(0x3,0xffffffff,0xc,true)")
				{
					found1 = true;
					break;
				}
			}
		}

		if (found1)
		{
			std::string line;
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receive Success(0x3)", line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receiving stream, size 425", line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Send GetIsAlive()", line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receive IsAlive()", line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Send GetIsAlive()", line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receive IsAlive()", line);
		}

		if (failures > 0)
		{
			// stop early to make debugging easier
			Timers::Init();
			return 1;
		}
	}
#endif // PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE

	// Check that no read error has been reported yet
	TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

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

	// BLOCK
	{
#ifdef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
		// No diffs were created, because intercept tests were skipped
		int expected_num_old = 0;
		int expected_blocks_old = 0;
#else // !PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
		// Some diffs were created by the intercept tests above
		int expected_num_old = 3;
		int expected_blocks_old = 6;
#endif

		std::auto_ptr<BackupProtocolCallable> client =
			connect_and_login(context, 0 /* read-write */);
		TEST_THAT(check_num_files(5, expected_num_old, 0, 9));
		TEST_THAT(check_num_blocks(*client, 10, expected_blocks_old,
			0, 18, 28 + expected_blocks_old));
		client->QueryFinished();
	}

	std::string cmd = BBACKUPD " " + bbackupd_args + 
		" testfiles/bbackupd.conf";

	bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
	TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);
	::safe_sleep(1);

	TEST_THAT(ServerIsAlive(bbackupd_pid));
	TEST_THAT(ServerIsAlive(bbstored_pid));
	if (!ServerIsAlive(bbackupd_pid)) return 1;
	if (!ServerIsAlive(bbstored_pid)) return 1;
	if (failures) return 1;

	if(bbackupd_pid > 0)
	{
		printf("\n==== Testing that backup pauses when "
			"store is full\n");

		// wait for files to be uploaded
		BOX_TRACE("Waiting for all outstanding files to be uploaded")
		wait_for_sync_end();
		BOX_TRACE("Done. Comparing to check that it worked...")
		TEST_COMPARE(Compare_Same);

		// Set limit to something very small
		// 26 blocks will be used at this point.
		// (12 files + location * 2 for raidfile)
		// 20 is what we'll need in a minute
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
		
		// Kill the daemon
		terminate_bbackupd(bbackupd_pid);

		wait_for_operation(5, "housekeeping to remove the "
			"deleted files");

		// This removes f1 and d7, which were previously marked
		// as deleted, so total usage drops by 4 blocks to 24.

		// BLOCK
		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context, 0 /* read-write */);
			TEST_THAT(check_num_files(4, 0, 0, 8));
			TEST_THAT(check_num_blocks(*client, 8, 0, 0, 16, 24));
			client->QueryFinished();
		}

		if (failures) return 1;

		// ensure time is different to refresh the cache
		::safe_sleep(1);

		BOX_TRACE("Restart bbackupd with more exclusions");
		// Start again with a new config that excludes d3 and f2,
		// and hence also d3/d4 and d3/d4/f5. bbackupd should mark
		// them as deleted and housekeeping should clean up,
		// making space to upload the new files.
		// total required: (13-2-4+3)*2 = 20 blocks
		/*
		cmd = BBACKUPD " " + bbackupd_args +
			" testfiles/bbackupd-exclude.conf";
		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		*/

		BackupDaemon bbackupd;

		TEST_THAT(configure_bbackupd(bbackupd, "testfiles/bbackupd-exclude.conf"));
		// Should be marked as deleted by this run. Hold onto the
		// BackupClientContext to stop housekeeping from running.
		// wait_for_sync_end();
		{
			// Logging::Guard guard(Log::ERROR);
			bbackupd.RunSyncNow();
		}

		TEST_THAT(bbackupd.StorageLimitExceeded());

		// Check that the notify script was run
		// TEST_THAT(TestFileExists("testfiles/notifyran.store-full.2"));
		// But only twice!
		// TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.3"));

		// All these should be marked as deleted but not removed by
		// housekeeping yet:
		// f1		deleted
		// f2		excluded
		// d3		excluded
		// d3/d4	excluded
		// d3/d4/f5	excluded
		// Careful with timing here, these files will be removed by
		// housekeeping the next time it runs. On Win32, housekeeping
		// runs immediately after disconnect, but only if enough time
		// has elapsed since the last housekeeping. Since the
		// backup run closely follows the last one, housekeeping
		// should not run afterwards. On other platforms, we want to
		// get in immediately after the backup and hope that
		// housekeeping doesn't beat us to it.

		BOX_TRACE("Find out whether bbackupd marked files as deleted");
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
			TEST_THAT(d3_id != 0);

			std::auto_ptr<BackupStoreDirectory> d3_dir =
				ReadDirectory(*client, d3_id);
			TEST_THAT(test_entry_deleted(*d3_dir, "d4"));

			int64_t d4_id = SearchDir(*d3_dir, "d4");
			TEST_THAT(d4_id != 0);

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

		if (failures) return 1;

#ifdef WIN32
		// Housekeeping runs automatically at the end of each backup,
		// and didn't run last time (see comments above), so run it
		// manually.
		TEST_THAT(run_housekeeping_and_check_account());
#else
		wait_for_operation(5, "housekeeping to remove the "
			"deleted files");
#endif

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

		if (failures) return 1;

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

		if (failures) return 1;

		// Put the limit back
		TEST_THAT_ABORTONFAIL(::system(BBSTOREACCOUNTS " -c "
			"testfiles/bbstored.conf setlimit 01234567 "
			"1000B 2000B") == 0);
		TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");
	
		// Start again with the old config
		BOX_TRACE("Restart bbackupd with original configuration");
		// terminate_bbackupd();
		cmd = BBACKUPD " " + bbackupd_args +
			" testfiles/bbackupd.conf";
		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);
		::safe_sleep(1);
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;
		BOX_TRACE("done.");

		// unpack the initial files again
		#ifdef WIN32
			TEST_THAT(::system("tar xzvf testfiles/test_base.tgz "
				"-C testfiles") == 0);
		#else
			TEST_THAT(::system("gzip -d < testfiles/test_base.tgz "
				"| ( cd testfiles && tar xf - )") == 0);
		#endif

		wait_for_backup_operation("bbackupd to upload more files");
		
		// Check that the contents of the store are the same 
		// as the contents of the disc 
		// (-a = all, -c = give result in return code)
		BOX_TRACE("Check that all files were uploaded successfully");
		TEST_COMPARE(Compare_Same);
		BOX_TRACE("done.");

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;
	}

	// Check that no read error has been reported yet
	TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

	#ifndef WIN32 // requires fork
	printf("\n==== Testing that bbackupd responds correctly to "
		"connection failure\n");

	{
		// Kill the daemons
		terminate_bbackupd(bbackupd_pid);
		TEST_THAT(StopServer());

		// create a new file to force an upload

		const char* new_file = "testfiles/TestDir1/force-upload-2";
		int fd = open(new_file, 
			O_CREAT | O_EXCL | O_WRONLY, 0700);
		if (fd <= 0)
		{
			perror(new_file);
		}
		TEST_THAT(fd > 0);
	
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
					THROW_EXCEPTION(CommonException,
						Internal);
				}
				return std::auto_ptr<BackupProtocolMessage>();
			}
		};
		MyHook hook;

		bbstored_pid = fork();

		if (bbstored_pid < 0)
		{
			BOX_LOG_SYS_ERROR("failed to fork()");
			return 1;
		}

		if (bbstored_pid == 0)
		{
			// in fork child
			TEST_THAT(setsid() != -1);

			// BackupStoreDaemon must be destroyed before exit(),
			// to avoid memory leaks being reported.
			{
				BackupStoreDaemon bbstored;
				bbstored.SetTestHook(hook);
				bbstored.SetSingleProcess(true);
				bbstored.Main("testfiles/bbstored.conf");
			}

			Timers::Cleanup(); // avoid memory leaks
			exit(0);
		}

		// in fork parent
		TEST_EQUAL(bbstored_pid, WaitForServerStartup("testfiles/bbstored.pid",
			bbstored_pid));

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
			bbackupd.Configure("testfiles/bbackupd.conf");
			bbackupd.InitCrypto();
			bbackupd.RunSyncNowWithExceptionHandling();
		}

		::signal(SIGPIPE, SIG_DFL);

		TEST_THAT(TestFileExists("testfiles/notifyran.backup-error.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-error.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.1"));

		TEST_THAT(StopServer(true));

		if (failures > 0)
		{
			// stop early to make debugging easier
			return 1;
		}

		TEST_THAT(StartServer());

		cmd = BBACKUPD " " + bbackupd_args +
			" testfiles/bbackupd.conf";
		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);
		::safe_sleep(1);
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;
	}
	#endif // !WIN32

	#ifndef WIN32
	printf("\n==== Testing that absolute symlinks are not followed "
		"during restore\n");

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

		wait_for_operation(4, "symlinks to be old enough");
		sync_and_wait();

		// Check that the backup was successful, i.e. no differences
		TEST_COMPARE(Compare_Same);

		// now stop bbackupd and update the test file,
		// make the original directory unreadable
		terminate_bbackupd(bbackupd_pid);

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

		/*
 		// This is not worth testing or fixing.
 		//
		#ifndef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
		printf("\n==== Testing that symlinks to other filesystems "
			"can be backed up as roots\n");

		intercept_setup_lstat_post_hook(lstat_test_post_hook);
		TEST_THAT(symlink("TestDir1", "testfiles/symlink-to-TestDir1")
			== 0);

		struct stat stat_st, lstat_st;
		TEST_THAT(stat("testfiles/symlink-to-TestDir1", &stat_st) == 0);
		TEST_THAT(lstat("testfiles/symlink-to-TestDir1", &lstat_st) == 0);
		TEST_EQUAL_LINE((stat_st.st_dev ^ 0xFFFF), lstat_st.st_dev,
			"stat vs lstat");

		BackupDaemon bbackupd;
		bbackupd.Configure("testfiles/bbackupd-symlink.conf");
		bbackupd.InitCrypto();
		bbackupd.RunSyncNow();
		intercept_clear_setup();

		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query0a.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// and again using the symlink during compare
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd-symlink.conf "
			"-l testfiles/query0a.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		#endif
		*/

		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);
		::safe_sleep(1);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;
	}
	#endif // !WIN32

	printf("\n==== Testing that nonexistent locations are backed up "
		"if they are created later\n");
	
	// ensure that the directory does not exist at the start
	TEST_THAT(::system("rm -rf testfiles/TestDir2") == 0);

	// BLOCK
	{
		// Kill the daemon
		terminate_bbackupd(bbackupd_pid);

		// Delete any old result marker files
		TEST_RETURN(0, system("rm -f testfiles/notifyran.*"));

		// Start it with a config that has a temporary location
		// whose path does not exist yet
		std::string cmd = BBACKUPD " " + bbackupd_args + 
			" testfiles/bbackupd-temploc.conf";

		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-start.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-start.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-ok.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-finish.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-finish.2"));

		sync_and_wait();
		TEST_COMPARE(Compare_Same);

		TEST_THAT( TestFileExists("testfiles/notifyran.backup-start.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-start.2"));
		TEST_THAT( TestFileExists("testfiles/notifyran.read-error.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-ok.1"));
		TEST_THAT( TestFileExists("testfiles/notifyran.backup-finish.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-finish.2"));
		
		// Did it actually get created? Should not have been!
		std::auto_ptr<BackupProtocolCallable> client =
			connect_and_login(context,
				BackupProtocolLogin::Flags_ReadOnly);
		
		std::auto_ptr<BackupStoreDirectory> dir = 
			ReadDirectory(*client);
		int64_t testDirId = SearchDir(*dir, "Test2");
		TEST_THAT(testDirId == 0);
		client->QueryFinished();
	}

	// create the location directory and unpack some files into it
	TEST_THAT(::mkdir("testfiles/TestDir2", 0777) == 0);
	TEST_THAT(unpack_files("spacetest1", "testfiles/TestDir2"));

	// check that the files are backed up now
	sync_and_wait();
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
	{
		std::auto_ptr<BackupProtocolCallable> client =
			connect_and_login(context,
				BackupProtocolLogin::Flags_ReadOnly);
		
		std::auto_ptr<BackupStoreDirectory> dir = 
			ReadDirectory(*client,
			BackupProtocolListDirectory::RootDirectory);
		int64_t testDirId = SearchDir(*dir, "Test2");
		TEST_THAT(testDirId != 0);

		client->QueryFinished();
	}

	printf("\n==== Testing that redundant locations are deleted on time\n");

	// BLOCK
	{
		// Kill the daemon
		terminate_bbackupd(bbackupd_pid);

		// Start it again with the normal config (no Test2)
		cmd = BBACKUPD " " + bbackupd_args + " testfiles/bbackupd.conf";
		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");

		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);

		::safe_sleep(1);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		// Test2 should be deleted after 10 seconds (4 runs)
		wait_for_sync_end();
		wait_for_sync_end();
		wait_for_sync_end();

		// not yet! should still be there

		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context,
					BackupProtocolLogin::Flags_ReadOnly);

			std::auto_ptr<BackupStoreDirectory> dir = 
				ReadDirectory(*client);
			int64_t testDirId = SearchDir(*dir, "Test2");
			TEST_THAT(testDirId != 0);

			client->QueryFinished();
		}

		wait_for_sync_end();

		// NOW it should be gone

		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context,
					BackupProtocolLogin::Flags_ReadOnly);

			std::auto_ptr<BackupStoreDirectory> root_dir = 
				ReadDirectory(*client);

			TEST_THAT(test_entry_deleted(*root_dir, "Test2"));

			client->QueryFinished();
		}

		std::auto_ptr<BackupProtocolCallable> client = connect_and_login(
			context, 0 /* read-write */);
		std::auto_ptr<BackupStoreDirectory> root_dir =
			ReadDirectory(*client, BACKUPSTORE_ROOT_DIRECTORY_ID);
		TEST_THAT(test_entry_deleted(*root_dir, "Test2"));
	}

	TEST_THAT(ServerIsAlive(bbackupd_pid));
	TEST_THAT(ServerIsAlive(bbstored_pid));
	if (!ServerIsAlive(bbackupd_pid)) return 1;
	if (!ServerIsAlive(bbstored_pid)) return 1;
	if (failures) return 1;

	if(bbackupd_pid > 0)
	{
		// Delete any old result marker files
		TEST_RETURN(0, system("rm -f testfiles/notifyran.*"));

		printf("\n==== Check that read-only directories and "
			"their contents can be restored.\n");

		int compareReturnValue;

		{
			#ifdef WIN32
				// Cygwin chmod changes Windows file attributes
				TEST_THAT(::system("chmod 0555 testfiles/"
					"TestDir1/x1") == 0);
			#else
				TEST_THAT(chmod("testfiles/TestDir1/x1",
					0555) == 0);
			#endif

			wait_for_sync_end(); // too new
			wait_for_sync_end(); // should be backed up now
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
			#else
				TEST_THAT(chmod("testfiles/TestDir1/x1",
					0755) == 0);
				TEST_THAT(chmod("testfiles/restore1/x1",
					0755) == 0);
			#endif
		}

#ifdef WIN32
		printf("\n==== Check that filenames in UTF-8 "
			"can be backed up\n");

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

		wait_for_backup_operation("upload of file with unicode name");

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
#endif // WIN32

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		printf("\n==== Check that SyncAllowScript is executed and can "
			"pause backup\n");
		fflush(stdout);

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

			if (failures) return 1;
		}

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		printf("\n==== Delete file and update another, "
			"create symlink.\n");
		
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

		// wait for backup daemon to do it's stuff
		sync_and_wait();

		// compare to make sure that it worked
		TEST_COMPARE(Compare_Same);

		// Try a quick compare, just for fun
		TEST_COMPARE(Compare_Same, "", "-acqQ");
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		// Check that store errors are reported neatly
		printf("\n==== Create store error\n");
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
		TEST_THAT(config.get() != 0);
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
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

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
		wait_for_operation(BACKUP_ERROR_RETRY_SECONDS +
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
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

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
		wait_for_operation(BACKUP_ERROR_RETRY_SECONDS +
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

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		// Bad case: delete a file/symlink, replace it with a directory
		printf("\n==== Replace symlink with directory, "
			"add new directory\n");

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

		wait_for_backup_operation("bbackupd to sync the changes");
		TEST_COMPARE(Compare_Same);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		// And the inverse, replace a directory with a file/symlink
		printf("\n==== Replace directory with symlink\n");

		#ifndef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/x1/dir-to-file"
				"/contents") == 0);
		#endif

		TEST_THAT(::rmdir("testfiles/TestDir1/x1/dir-to-file") == 0);

		#ifndef WIN32
			TEST_THAT(::symlink("does-not-exist", 
				"testfiles/TestDir1/x1/dir-to-file") == 0);
		#endif

		wait_for_backup_operation("bbackupd to sync the changes");

		TEST_COMPARE(Compare_Same);
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

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

		wait_for_backup_operation("bbackupd to sync the changes");

		TEST_COMPARE(Compare_Same);
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		// And finally, put it back to how it was before 
		// it was put back to how it was before
		// This gets lots of nasty things in the store with 
		// directories over other old directories.
		printf("\n==== Put it all back to how it was\n");

		#ifndef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/x1/dir-to-file"
				"/contents2") == 0);
		#endif

		TEST_THAT(::rmdir("testfiles/TestDir1/x1/dir-to-file") == 0);

		#ifndef WIN32
			TEST_THAT(::symlink("does-not-exist", 
				"testfiles/TestDir1/x1/dir-to-file") == 0);
		#endif

		wait_for_backup_operation("bbackupd to sync the changes");

		TEST_COMPARE(Compare_Same);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		// rename an untracked file over an 
		// existing untracked file
		printf("\n==== Rename over existing untracked file\n");
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
		wait_for_backup_operation("bbackupd to sync the "
			"untracked files");

		TEST_COMPARE(Compare_Same);

		#ifdef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/untracked-2")
				== 0);
		#endif

		TEST_THAT(::rename("testfiles/TestDir1/untracked-1", 
			"testfiles/TestDir1/untracked-2") == 0);
		TEST_THAT(!TestFileExists("testfiles/TestDir1/untracked-1"));
		TEST_THAT( TestFileExists("testfiles/TestDir1/untracked-2"));

		wait_for_backup_operation("bbackupd to sync the untracked "
			"files again");

		TEST_COMPARE(Compare_Same);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

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
		
		// back up both files
		sync_and_wait();

		// compare to make sure that it worked
		TEST_COMPARE(Compare_Same);

		#ifdef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/tracked-2")
				== 0);
		#endif

		TEST_THAT(::rename("testfiles/TestDir1/tracked-1",
			"testfiles/TestDir1/tracked-2") == 0);
		TEST_THAT(!TestFileExists("testfiles/TestDir1/tracked-1"));
		TEST_THAT( TestFileExists("testfiles/TestDir1/tracked-2"));

		wait_for_backup_operation("bbackupd to sync the tracked "
			"files again");

		TEST_COMPARE(Compare_Same);
	
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		// case which went wrong: rename a tracked file
		// over a deleted file
		printf("\n==== Rename an existing file over a deleted file\n");
		TEST_THAT(!TestFileExists("testfiles/TestDir1/x1/dsfdsfs98.fd"));
		TEST_THAT(::rename("testfiles/TestDir1/df9834.dsf", 
			"testfiles/TestDir1/x1/dsfdsfs98.fd") == 0);
		
		wait_for_backup_operation("bbackupd to sync");

		TEST_COMPARE(Compare_Same);

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		printf("\n==== Add files with old times, update "
			"attributes of one to latest time\n");

		// Move that file back
		TEST_THAT(::rename("testfiles/TestDir1/x1/dsfdsfs98.fd", 
			"testfiles/TestDir1/df9834.dsf") == 0);
		
		// Add some more files
		// Because the 'm' option is not used, these files will
		// look very old to the daemon.
		// Lucky it'll upload them then!
		TEST_THAT(unpack_files("test2"));

		#ifndef WIN32
			::chmod("testfiles/TestDir1/sub23/dhsfdss/blf.h", 0415);
		#endif
		
		// Wait and test
		wait_for_backup_operation("bbackupd to sync old files");

		TEST_COMPARE(Compare_Same);
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

		// Check that modifying files with old timestamps
		// still get added
		printf("\n==== Modify existing file, but change timestamp "
			"to rather old\n");
		wait_for_sync_end();

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
		wait_for_sync_end(); // files too new
		wait_for_sync_end(); // should (not) be backed up this time

		TEST_COMPARE(Compare_Same);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		// Check that no read error has been reported yet
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

		// Add some files and directories which are marked as excluded
		printf("\n==== Add files and dirs for exclusion test\n");
		TEST_THAT(unpack_files("testexclude"));

		// Wait and test
		wait_for_sync_end();
		wait_for_sync_end();
		
		// compare with exclusions, should not find differences
		TEST_COMPARE(Compare_Same);

		// compare without exclusions, should find differences
		TEST_COMPARE(Compare_Different, "", "-acEQ");

		// check that the excluded files did not make it
		// into the store, and the included files did
		printf("\n==== Check that exclude/alwaysinclude commands "
			"actually work\n");

		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context,
				BackupProtocolLogin::Flags_ReadOnly);
			
			std::auto_ptr<BackupStoreDirectory> dir =
				ReadDirectory(*client);

			int64_t testDirId = SearchDir(*dir, "Test1");
			TEST_THAT(testDirId != 0);
			dir = ReadDirectory(*client, testDirId);
				
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
			dir = ReadDirectory(*client, sub23id);

			TEST_THAT(!SearchDir(*dir, "xx_not_this_dir_22"));
			TEST_THAT(!SearchDir(*dir, "somefile.excludethis"));
			client->QueryFinished();
		}

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

#ifndef WIN32
		// These tests only work as non-root users.
		if(::getuid() != 0)
		{
			// Check that the error has not been reported yet
			TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.1"));

			// Check that read errors are reported neatly
			BOX_INFO("Add unreadable files");

			{
				// Dir and file which can't be read
				TEST_THAT(::mkdir("testfiles/TestDir1/sub23"
					"/read-fail-test-dir", 0000) == 0);
				int fd = ::open("testfiles/TestDir1"
					"/read-fail-test-file", 
					O_CREAT | O_WRONLY, 0000);
				TEST_THAT(fd != -1);
				::close(fd);
			}

			// Wait and test...
			wait_for_backup_operation("bbackupd to try to sync "
				"unreadable file");

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
#endif

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		printf("\n==== Continuously update file, "
			"check isn't uploaded\n");
		
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
			compareReturnValue = ::system("perl testfiles/"
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
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		printf("\n==== Delete directory, change attributes\n");
	
		// Delete a directory
		TEST_THAT(::system("rm -rf testfiles/TestDir1/x1") == 0);
		// Change attributes on an original file.
		::chmod("testfiles/TestDir1/df9834.dsf", 0423);
		
		// Wait and test
		wait_for_backup_operation("bbackupd to sync deletion "
			"of directory");

		TEST_COMPARE(Compare_Same);
	
		printf("\n==== Restore files and directories\n");
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
			TEST_THAT(restoredirid != 0);

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
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

#ifdef WIN32
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
#endif // WIN32

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		printf("\n==== Add files with current time\n");
	
		// Add some more files and modify others
		// Use the m flag this time so they have a recent modification time
		TEST_THAT(unpack_files("test3", "testfiles", "-m"));
		
		// Wait and test
		wait_for_backup_operation("bbackupd to sync new files");

		TEST_COMPARE(Compare_Same);
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		// Rename directory
		printf("\n==== Rename directory\n");
		TEST_THAT(rename("testfiles/TestDir1/sub23/dhsfdss", 
			"testfiles/TestDir1/renamed-dir") == 0);

		wait_for_backup_operation("bbackupd to sync renamed directory");

		TEST_COMPARE(Compare_Same);

		// and again, but with quick flag
		TEST_COMPARE(Compare_Same, "", "-acqQ");

		// Rename some files -- one under the threshold, others above
		printf("\n==== Rename files\n");
		TEST_THAT(rename("testfiles/TestDir1/continousupdate", 
			"testfiles/TestDir1/continousupdate-ren") == 0);
		TEST_THAT(rename("testfiles/TestDir1/df324", 
			"testfiles/TestDir1/df324-ren") == 0);
		TEST_THAT(rename("testfiles/TestDir1/sub23/find2perl", 
			"testfiles/TestDir1/find2perl-ren") == 0);

		wait_for_backup_operation("bbackupd to sync renamed files");

		TEST_COMPARE(Compare_Same);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		// Check that modifying files with madly in the future 
		// timestamps still get added
		printf("\n==== Create a file with timestamp way ahead "
			"in the future\n");

		// Time critical, so sync
		wait_for_sync_start();

		// Then wait a second, to make sure the scan is complete
		::safe_sleep(1);

		// Then modify an existing file
		{
			FILE *f = fopen("testfiles/TestDir1/sub23/"
				"in-the-future", "w");
			TEST_THAT(f != 0);
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
		wait_for_backup_operation("bbackup to sync future file");
		TEST_COMPARE(Compare_Same);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		printf("\n==== Change client store marker\n");

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
					protocol->QuerySetClientStoreMarker(12);
					
					// Success!
					done = true;
					
					// Log out
					protocol->QueryFinished();
				}
				catch(...)
				{
					tries--;
				}
			}
			TEST_THAT(done);
		}
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		printf("\n==== Check change of store marker pauses daemon\n");
		
		// Make a change to a file, to detect whether or not 
		// it's hanging around waiting to retry.
		{
			FILE *f = ::fopen("testfiles/TestDir1/fileaftermarker", "w");
			TEST_THAT(f != 0);
			::fprintf(f, "Lovely file you got there.");
			::fclose(f);
		}

		// Wait a little bit longer than usual
		wait_for_operation((TIME_TO_WAIT_FOR_BACKUP_OPERATION * 
			3) / 2, "bbackupd to detect changed store marker");

		// Test that there *are* differences
		TEST_COMPARE(Compare_Different);
	
		wait_for_operation(BACKUP_ERROR_RETRY_SECONDS,
			"bbackupd to recover");

		// Then check it has backed up successfully.
		TEST_COMPARE(Compare_Same);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

#ifndef WIN32
		printf("\n==== Interrupted restore\n");
		{
			do_interrupted_restore(context, restoredirid);
			int64_t resumesize = 0;
			TEST_THAT(FileExists("testfiles/"
				"restore-interrupt.boxbackupresume",
				&resumesize));
			// make sure it has recorded something to resume
			TEST_THAT(resumesize > 16);	

			printf("\n==== Resume restore\n");

			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context,
					BackupProtocolLogin::Flags_ReadOnly);

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
#endif // !WIN32

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		printf("\n==== Check restore deleted files\n");

		{
			std::auto_ptr<BackupProtocolCallable> client =
				connect_and_login(context, 0 /* read-write */);

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

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

#ifdef WIN32
		printf("\n==== Testing locked file behaviour:\n");

		// Test that locked files cannot be backed up,
		// and the appropriate error is reported.
		// Wait for the sync to finish, so that we have time to work
		wait_for_sync_end();
		// Now we have about three seconds to work

		handle = openfile("testfiles/TestDir1/lockedfile",
			O_CREAT | O_EXCL | O_LOCK, 0);
		TEST_THAT(handle != INVALID_HANDLE_VALUE);

		if (handle != 0)
		{
			// first sync will ignore the file, it's too new
			wait_for_sync_end();
			TEST_THAT(!TestFileExists("testfiles/"
				"notifyran.read-error.1"));
		}

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		if (handle != 0)
		{
			// this sync should try to back up the file, 
			// and fail, because it's locked
			wait_for_sync_end();
			TEST_THAT(TestFileExists("testfiles/"
				"notifyran.read-error.1"));
			TEST_THAT(!TestFileExists("testfiles/"
				"notifyran.read-error.2"));
		}

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		if (handle != 0)
		{
			// now close the file and check that it is
			// backed up on the next run.
			CloseHandle(handle);
			wait_for_sync_end();

			// still no read errors?
			TEST_THAT(!TestFileExists("testfiles/"
				"notifyran.read-error.2"));
			TEST_COMPARE(Compare_Same);
		}

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		if (handle != 0)
		{
			// open the file again, compare and check that compare
			// reports the correct error message (and finishes)
			handle = openfile("testfiles/TestDir1/lockedfile",
				O_LOCK, 0);
			TEST_THAT(handle != INVALID_HANDLE_VALUE);

			TEST_COMPARE(Compare_Error);

			// close the file again, check that compare
			// works again
			CloseHandle(handle);
		}

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		if (handle != 0)
		{
			TEST_COMPARE(Compare_Same);
		}
#endif

		// Kill the daemon
		terminate_bbackupd(bbackupd_pid);
		
		// Start it again
		cmd = BBACKUPD " " + bbackupd_args +
			" testfiles/bbackupd.conf";
		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");

		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
		if (failures) return 1;

		if(bbackupd_pid != -1 && bbackupd_pid != 0)
		{
			// Wait and compare (a little bit longer than usual)
			wait_for_operation(
				(TIME_TO_WAIT_FOR_BACKUP_OPERATION*3) / 2,
				"bbackupd to sync everything"); 
			TEST_COMPARE(Compare_Same);

			// Kill it again
			terminate_bbackupd(bbackupd_pid);
		}
	}

	/*
	// List the files on the server - why?
	::system(BBACKUPQUERY " -q -c testfiles/bbackupd.conf "
		"-l testfiles/queryLIST.log \"list -Rotdh\" quit");
	TestRemoteProcessMemLeaks("bbackupquery.memleaks");
	*/

	#ifndef WIN32	
		if(::getuid() == 0)
		{
			::printf("WARNING: This test was run as root. "
				"Some tests have been omitted.\n");
		}
	#endif
	
	return 0;
}

int test(int argc, const char *argv[])
{
	{
		// This is not a complete command, it should not parse!
		BackupQueries::ParsedCommand cmd("-od", true);
		TEST_THAT(cmd.mFailed);
		TEST_EQUAL(0, cmd.pSpec);
		TEST_EQUAL(0, cmd.mCompleteArgCount);
	}

	{
		BackupDaemon daemon;

		TEST_EQUAL(1234, daemon.ParseSyncAllowScriptOutput("test", "1234"));
		TEST_EQUAL(0, daemon.GetMaxBandwidthFromSyncAllowScript());

		TEST_EQUAL(1234, daemon.ParseSyncAllowScriptOutput("test", "1234 5"));
		TEST_EQUAL(5, daemon.GetMaxBandwidthFromSyncAllowScript());

		TEST_EQUAL(-1, daemon.ParseSyncAllowScriptOutput("test", "now"));
		TEST_EQUAL(0, daemon.GetMaxBandwidthFromSyncAllowScript());
	}

	// SSL library
	SSLLib::Initialise();

	// Keys for subsystems
	BackupClientCryptoKeys_Setup("testfiles/bbackupd.keys");

	// Initial files
	#ifdef WIN32
		TEST_THAT(::system("tar xzvf testfiles/test_base.tgz "
			"-C testfiles") == 0);
	#else
		TEST_THAT(::system("gzip -d < testfiles/test_base.tgz "
			"| ( cd testfiles && tar xf - )") == 0);
	#endif

	{
		std::string errs;
		std::auto_ptr<Configuration> config(
			Configuration::LoadAndVerify
				("testfiles/bbstored.conf", &BackupConfigFileVerify, errs));
		TEST_EQUAL_LINE(0, errs.size(), "Loading configuration file "
			"reported errors: " << errs);
		TEST_THAT(config.get() != 0);
		// Initialise the raid file controller
		RaidFileController &rcontroller(RaidFileController::GetController());
		rcontroller.Initialise(config->GetKeyValue("RaidFileConf").c_str());
	}

	// Do the tests
	TEST_THAT(test_basics());

	int r = (StartServer() ? 0 : 1);
	TEST_THAT(r == 0);
	if(r != 0) return r;
	
	r = test_bbackupd();
	if(r != 0)
	{
		if (bbackupd_pid)
		{
			KillServer(bbackupd_pid);
		}
		if (bbstored_pid)
		{
			KillServer(bbstored_pid);
		}
		return r;
	}
	
	TEST_THAT(StopServer());

	return 0;
}
