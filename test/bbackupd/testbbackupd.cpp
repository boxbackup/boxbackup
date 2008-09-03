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

#include "autogen_BackupProtocolServer.h"
#include "BackupClientCryptoKeys.h"
#include "BackupClientFileAttributes.h"
#include "BackupClientRestore.h"
#include "BackupDaemon.h"
#include "BackupDaemonConfigVerify.h"
#include "BackupQueries.h"
#include "BackupStoreConstants.h"
#include "BackupStoreContext.h"
#include "BackupStoreDaemon.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreException.h"
#include "BoxPortsAndFiles.h"
#include "BoxTime.h"
#include "BoxTimeToUnix.h"
#include "CollectInBufferStream.h"
#include "CommonException.h"
#include "Configuration.h"
#include "FileModificationTime.h"
#include "FileStream.h"
#include "IOStreamGetLine.h"
#include "LocalProcessStream.h"
#include "SSLLib.h"
#include "ServerControl.h"
#include "Socket.h"
#include "SocketStreamTLS.h"
#include "TLSContext.h"
#include "Test.h"
#include "Timer.h"
#include "Utils.h"

#include "autogen_BackupProtocolClient.h"
#include "intercept.h"
#include "ServerControl.h"

#include "MemLeakFindOn.h"

// ENOATTR may be defined in a separate header file which we may not have
#ifndef ENOATTR
#define ENOATTR ENODATA
#endif

// two cycles and a bit
#define TIME_TO_WAIT_FOR_BACKUP_OPERATION	12

// utility macro for comparing two strings in a line
#define TEST_EQUAL(_expected, _found, _line) \
{ \
	std::ostringstream _oss1; \
	_oss1 << _expected; \
	std::string _exp_str = _oss1.str(); \
	\
	std::ostringstream _oss2; \
	_oss2 << _found; \
	std::string _found_str = _oss2.str(); \
	\
	if(_exp_str != _found_str) \
	{ \
		std::string _line_str = _line; \
		printf("Expected <%s> but found <%s> in <%s>\n", \
			_exp_str.c_str(), _found_str.c_str(), _line_str.c_str()); \
		\
		std::ostringstream _oss3; \
		_oss3 << #_found << " != " << #_expected << " in " << _line; \
		\
		TEST_FAIL_WITH_MESSAGE(_oss3.str().c_str()); \
	} \
}

// utility macro for testing a line
#define TEST_LINE(_condition, _line) \
	TEST_THAT(_condition); \
	if (!(_condition)) \
	{ \
		printf("Test failed on <%s>\n", _line.c_str()); \
	}

void wait_for_backup_operation(int seconds = TIME_TO_WAIT_FOR_BACKUP_OPERATION)
{
	wait_for_operation(seconds);
}

int bbstored_pid = 0;
int bbackupd_pid = 0;

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
	struct stat s1, s2;
	TEST_THAT(::lstat(f1, &s1) == 0);
	TEST_THAT(::lstat(f2, &s2) == 0);

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

int test_basics()
{
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
		Logging::Guard guard(Log::ERROR);
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
		Logging::Guard guard(Log::ERROR);
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

	return 0;
}

int test_setupaccount()
{
	TEST_THAT_ABORTONFAIL(::system(BBSTOREACCOUNTS " -c "
		"testfiles/bbstored.conf create 01234567 0 1000B 2000B") == 0);
	TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");
	return 0;
}

int test_run_bbstored()
{
	std::string cmd = BBSTORED " " + bbstored_args + 
		" testfiles/bbstored.conf";
	bbstored_pid = LaunchServer(cmd, "testfiles/bbstored.pid");

	TEST_THAT(bbstored_pid != -1 && bbstored_pid != 0);

	if(bbstored_pid > 0)
	{
		::safe_sleep(1);
		TEST_THAT(ServerIsAlive(bbstored_pid));
		return 0;	// success
	}
	
	return 1;
}

int test_kill_bbstored(bool wait_for_process = false)
{
	TEST_THAT(KillServer(bbstored_pid, wait_for_process));
	::safe_sleep(1);
	TEST_THAT(!ServerIsAlive(bbstored_pid));
	if (!ServerIsAlive(bbstored_pid))
	{
		bbstored_pid = 0;
	}

	#ifdef WIN32
		TEST_THAT(unlink("testfiles/bbstored.pid") == 0);
	#else
		TestRemoteProcessMemLeaks("bbstored.memleaks");
	#endif
	
	return 0;
}

int64_t GetDirID(BackupProtocolClient &protocol, const char *name, int64_t InDirectory)
{
	protocol.QueryListDirectory(
			InDirectory,
			BackupProtocolClientListDirectory::Flags_Dir,
			BackupProtocolClientListDirectory::Flags_EXCLUDE_NOTHING,
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
			SocketStreamTLS conn;
			conn.Open(context, Socket::TypeINET, "localhost",
				22011);
			BackupProtocolClient protocol(conn);
			protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION);
			std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocol.QueryLogin(0x01234567, BackupProtocolClientLogin::Flags_ReadOnly));
			
			// Test the restoration
			TEST_THAT(BackupClientRestore(protocol, restoredirid, "testfiles/restore-interrupt", true /* print progress dots */) == Restore_Complete);

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
	BackupStoreFilenameClear child(rChildName.c_str());
	BackupStoreDirectory::Entry *en = i.FindMatchingClearName(child);
	if (en == 0) return 0;
	int64_t id = en->GetObjectID();
	TEST_THAT(id > 0);
	TEST_THAT(id != BackupProtocolClientListDirectory::RootDirectory);
	return id;
}

SocketStreamTLS sSocket;

std::auto_ptr<BackupProtocolClient> Connect(TLSContext& rContext)
{
	sSocket.Open(rContext, Socket::TypeINET, 
		"localhost", 22011);
	std::auto_ptr<BackupProtocolClient> connection;
	connection.reset(new BackupProtocolClient(sSocket));
	connection->Handshake();
	std::auto_ptr<BackupProtocolClientVersion> 
		serverVersion(connection->QueryVersion(
			BACKUP_STORE_SERVER_VERSION));
	if(serverVersion->GetVersion() != 
		BACKUP_STORE_SERVER_VERSION)
	{
		THROW_EXCEPTION(BackupStoreException, 
			WrongServerVersion);
	}
	return connection;
}

std::auto_ptr<BackupProtocolClient> ConnectAndLogin(TLSContext& rContext,
	int flags)
{
	std::auto_ptr<BackupProtocolClient> connection(Connect(rContext));
	connection->QueryLogin(0x01234567, flags);
	return connection;
}
	
std::auto_ptr<BackupStoreDirectory> ReadDirectory
(
	BackupProtocolClient& rClient,
	int64_t id
)
{
	std::auto_ptr<BackupProtocolClientSuccess> dirreply(
		rClient.QueryListDirectory(id, false, 0, false));
	std::auto_ptr<IOStream> dirstream(rClient.ReceiveStream());
	std::auto_ptr<BackupStoreDirectory> apDir(new BackupStoreDirectory());
	apDir->ReadFromStream(*dirstream, rClient.GetTimeout());
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
	int result;

	if (bbackupd_args.size() > 0)
	{
		result = daemon.Main("testfiles/bbackupd.conf", 2, argv);
	}
	else
	{
		result = daemon.Main("testfiles/bbackupd.conf", 1, argv);
	}
	
	TEST_EQUAL(0, result, "Daemon exit code");
	
	// ensure that no child processes end up running tests!
	TEST_EQUAL(own_pid, getpid(), "Forking test problem");
	if (getpid() != own_pid)
	{
		// abort!
		_exit(1);
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
	return pid;
}

bool stop_internal_daemon(int pid)
{
	bool killed_server = KillServer(pid, true);
	TEST_THAT(killed_server);
	return killed_server;
}

static struct dirent readdir_test_dirent;
static int readdir_test_counter = 0;
static int readdir_stop_time = 0;
static char stat_hook_filename[512];

// First test hook, during the directory scanning stage, returns empty.
// This will not match the directory on the store, so a sync will start.
// We set up the next intercept for the same directory by passing NULL.

struct dirent *readdir_test_hook_2(DIR *dir);

#ifdef LINUX_WEIRD_LSTAT
int lstat_test_hook(int ver, const char *file_name, struct stat *buf);
#else
int lstat_test_hook(const char *file_name, struct stat *buf);
#endif

struct dirent *readdir_test_hook_1(DIR *dir)
{
#ifndef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
	intercept_setup_readdir_hook(NULL, readdir_test_hook_2);
#endif
	return NULL;
}

// Second test hook, during the directory sync stage, keeps returning 
// new filenames until the timer expires, then disables the intercept.

struct dirent *readdir_test_hook_2(DIR *dir)
{
	if (time(NULL) >= readdir_stop_time)
	{
#ifndef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
		intercept_setup_readdir_hook(NULL, NULL);
		intercept_setup_lstat_hook  (NULL, NULL);
		// we will not be called again.
#endif
	}

	// fill in the struct dirent appropriately
	memset(&readdir_test_dirent, 0, sizeof(readdir_test_dirent));

	#ifdef HAVE_STRUCT_DIRENT_D_INO
		readdir_test_dirent.d_ino = ++readdir_test_counter;
	#endif

	snprintf(readdir_test_dirent.d_name, 
		sizeof(readdir_test_dirent.d_name),
		"test.%d", readdir_test_counter);

	// ensure that when bbackupd stats the file, it gets the 
	// right answer
	snprintf(stat_hook_filename, sizeof(stat_hook_filename),
		"testfiles/TestDir1/spacetest/d1/test.%d", 
		readdir_test_counter);

#ifndef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
	intercept_setup_lstat_hook(stat_hook_filename, lstat_test_hook);
#endif

	return &readdir_test_dirent;
}

#ifdef LINUX_WEIRD_LSTAT
int lstat_test_hook(int ver, const char *file_name, struct stat *buf)
#else
int lstat_test_hook(const char *file_name, struct stat *buf)
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
	TEST_THAT(en != 0);
	if (en == 0) return false;

	int16_t flags = en->GetFlags();
	TEST_THAT(flags && BackupStoreDirectory::Entry::Flags_Deleted);
	return flags && BackupStoreDirectory::Entry::Flags_Deleted;
}

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
		std::auto_ptr<BackupProtocolClient> client = ConnectAndLogin(
			context, 0 /* read-write */);
		
		{
			Logging::Guard guard(Log::ERROR);
			TEST_CHECK_THROWS(ReadDirectory(*client, 0x12345678),
				ConnectionException,
				Conn_Protocol_UnexpectedReply);
		}

		client->QueryFinished();
		sSocket.Close();
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
	
#if 1
// #ifdef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
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

		TEST_EQUAL(sizeof(buffer), write(fd, buffer, sizeof(buffer)),
			"Buffer write");
		TEST_THAT(close(fd) == 0);
		
		int pid = start_internal_daemon();
		wait_for_backup_operation();
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
		TEST_EQUAL(sizeof(buffer), write(fd, buffer, sizeof(buffer)),
			"Buffer write");
		TEST_THAT(close(fd) == 0);	

		wait_for_backup_operation();
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
			TEST_EQUAL(comp, line.substr(0, comp.size()), line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receiving stream, size 124", line, line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Send GetIsAlive()", line, line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receive IsAlive()", line, line);

			TEST_THAT(reader.GetLine(line));
			comp = "Send StoreFile(0x3,";
			TEST_EQUAL(comp, line.substr(0, comp.size()), line);
			comp = ",\"f1\")";
			std::string sub = line.substr(line.size() - comp.size());
			TEST_EQUAL(comp, sub, line);
			std::string comp2 = ",0x0,";
			sub = line.substr(line.size() - comp.size() -
				comp2.size() + 1, comp2.size());
			TEST_LINE(comp2 != sub, line);
		}
		
		if (failures > 0)
		{
			// stop early to make debugging easier
			Timers::Init();
			return 1;
		}

		// four-second delay on first read() of f1
		// should mean that no keepalives were sent,
		// because diff was immediately aborted
		// before any matching blocks could be found.
		intercept_setup_delay("testfiles/TestDir1/spacetest/f1", 
			0, 4000, SYS_read, 1);
		pid = start_internal_daemon();
		intercept_clear_setup();
		
		fd = open("testfiles/TestDir1/spacetest/f1", O_WRONLY);
		TEST_THAT(fd > 0);
		// write again, to update the file's timestamp
		TEST_EQUAL(sizeof(buffer), write(fd, buffer, sizeof(buffer)),
			"Buffer write");
		TEST_THAT(close(fd) == 0);	

		wait_for_backup_operation();
		// can't test whether intercept was triggered, because
		// it's in a different process.
		// TEST_THAT(intercept_triggered());
		TEST_THAT(stop_internal_daemon(pid));

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
			TEST_EQUAL(comp, line.substr(0, comp.size()), line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receiving stream, size 124", line, line);

			// delaying for 4 seconds in one step means that
			// the diff timer and the keepalive timer will
			// both expire, and the diff timer is honoured first,
			// so there will be no keepalives.

			TEST_THAT(reader.GetLine(line));
			comp = "Send StoreFile(0x3,";
			TEST_EQUAL(comp, line.substr(0, comp.size()), line);
			comp = ",0x0,\"f1\")";
			std::string sub = line.substr(line.size() - comp.size());
			TEST_EQUAL(comp, sub, line);
		}

		if (failures > 0)
		{
			// stop early to make debugging easier
			Timers::Init();
			return 1;
		}

		intercept_setup_delay("testfiles/TestDir1/spacetest/f1", 
			0, 1000, SYS_read, 3);
		pid = start_internal_daemon();
		intercept_clear_setup();
		
		fd = open("testfiles/TestDir1/spacetest/f1", O_WRONLY);
		TEST_THAT(fd > 0);
		// write again, to update the file's timestamp
		TEST_EQUAL(sizeof(buffer), write(fd, buffer, sizeof(buffer)),
			"Buffer write");
		TEST_THAT(close(fd) == 0);	

		wait_for_backup_operation();
		// can't test whether intercept was triggered, because
		// it's in a different process.
		// TEST_THAT(intercept_triggered());
		TEST_THAT(stop_internal_daemon(pid));

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
			TEST_EQUAL(comp, line.substr(0, comp.size()), line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receiving stream, size 124", line, line);

			// delaying for 3 seconds in steps of 1 second
			// means that the keepalive timer will expire 3 times,
			// and on the 3rd time the diff timer will expire too.
			// The diff timer is honoured first, so there will be 
			// only two keepalives.
			
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Send GetIsAlive()", line, line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receive IsAlive()", line, line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Send GetIsAlive()", line, line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receive IsAlive()", line, line);

			// but two matching blocks should have been found
			// already, so the upload should be a diff.

			TEST_THAT(reader.GetLine(line));
			comp = "Send StoreFile(0x3,";
			TEST_EQUAL(comp, line.substr(0, comp.size()), line);
			comp = ",\"f1\")";
			std::string sub = line.substr(line.size() - comp.size());
			TEST_EQUAL(comp, sub, line);
			std::string comp2 = ",0x0,";
			sub = line.substr(line.size() - comp.size() -
				comp2.size() + 1, comp2.size());
			TEST_LINE(comp2 != sub, line);
		}

		if (failures > 0)
		{
			// stop early to make debugging easier
			Timers::Init();
			return 1;
		}

		intercept_setup_readdir_hook("testfiles/TestDir1/spacetest/d1", 
			readdir_test_hook_1);
		
		// time for at least two keepalives
		readdir_stop_time = time(NULL) + 12 + 2;

		pid = start_internal_daemon();
		intercept_clear_setup();
		
		std::string touchfile = 
			"testfiles/TestDir1/spacetest/d1/touch-me";

		fd = open(touchfile.c_str(), O_CREAT | O_WRONLY);
		TEST_THAT(fd > 0);
		// write again, to update the file's timestamp
		TEST_EQUAL(sizeof(buffer), write(fd, buffer, sizeof(buffer)),
			"Buffer write");
		TEST_THAT(close(fd) == 0);	

		wait_for_backup_operation();
		// can't test whether intercept was triggered, because
		// it's in a different process.
		// TEST_THAT(intercept_triggered());
		TEST_THAT(stop_internal_daemon(pid));

		// check that keepalives were sent during the dir search
		found1 = false;

		// skip to next login
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
			TEST_EQUAL("Receive Success(0x3)", line, line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receiving stream, size 425", line, line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Send GetIsAlive()", line, line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receive IsAlive()", line, line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Send GetIsAlive()", line, line);
			TEST_THAT(reader.GetLine(line));
			TEST_EQUAL("Receive IsAlive()", line, line);
		}

		if (failures > 0)
		{
			// stop early to make debugging easier
			Timers::Init();
			return 1;
		}

		TEST_THAT(unlink(touchfile.c_str()) == 0);

		// restore timers for rest of tests
		Timers::Init();
	}
#endif // PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE

	std::string cmd = BBACKUPD " " + bbackupd_args + 
		" testfiles/bbackupd.conf";

	bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
	TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);
	::safe_sleep(1);

	TEST_THAT(ServerIsAlive(bbackupd_pid));
	TEST_THAT(ServerIsAlive(bbstored_pid));
	if (!ServerIsAlive(bbackupd_pid)) return 1;
	if (!ServerIsAlive(bbstored_pid)) return 1;

	if(bbackupd_pid > 0)
	{
		printf("\n==== Testing that backup pauses when "
			"store is full\n");

		// wait for files to be uploaded
		BOX_TRACE("Waiting for all outstanding files to be uploaded")
		wait_for_sync_end();
		BOX_TRACE("done.")

		// Set limit to something very small
		// 26 blocks will be used at this point.
		// (12 files + location * 2 for raidfile)
		// 20 is what we'll need in a minute
		// set soft limit to 0 to ensure that all deleted files
		// are deleted immediately by housekeeping
		TEST_THAT_ABORTONFAIL(::system(BBSTOREACCOUNTS " -c "
			"testfiles/bbstored.conf setlimit 01234567 0B 20B") 
			== 0);
		TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");

		// Unpack some more files
		#ifdef WIN32
			TEST_THAT(::system("tar xzvf testfiles/spacetest2.tgz "
				"-C testfiles/TestDir1") == 0);
		#else
			TEST_THAT(::system("gzip -d < testfiles/spacetest2.tgz "
				"| ( cd testfiles/TestDir1 && tar xf - )") == 0);
		#endif

		// Delete a file and a directory
		TEST_THAT(::unlink("testfiles/TestDir1/spacetest/f1") == 0);
		TEST_THAT(::system("rm -rf testfiles/TestDir1/spacetest/d7") == 0);

		// The following files should be on the server:
		// 00000001 -d---- 00002 (root)
		// 00000002 -d---- 00002 Test1
		// 00000003 -d---- 00002 Test1/spacetest
		// 00000004 f-X--- 00002 Test1/spacetest/f1
		// 00000005 f----- 00002 Test1/spacetest/f2
		// 00000006 -d---- 00002 Test1/spacetest/d1
		// 00000007 f----- 00002 Test1/spacetest/d1/f3
		// 00000008 f----- 00002 Test1/spacetest/d1/f4
		// 00000009 -d---- 00002 Test1/spacetest/d2
		// 0000000a -d---- 00002 Test1/spacetest/d3
		// 0000000b -d---- 00002 Test1/spacetest/d3/d4
		// 0000000c f----- 00002 Test1/spacetest/d3/d4/f5
		// 0000000d -d---- 00002 Test1/spacetest/d6
		// 0000000e -dX--- 00002 Test1/spacetest/d7
		// This is 28 blocks total, of which 2 in deleted files
		// and 18 in directories. Note that f1 and d7 may or may
		// not be deleted yet.
		//
		// spacetest1 + spacetest2 = 16 files = 32 blocks with raidfile
		// minus one file and one dir is 28 blocks
		//
		// d2/f6, d6/d8 and d6/d8/f7 are new
		// even if the client marks f1 and d7 as deleted, and
		// housekeeping deleted them, the backup cannot complete
		// if the limit is 20 blocks.

		BOX_TRACE("Waiting for bbackupd to notice that the "
			"store is full");
		wait_for_sync_end();
		BOX_TRACE("done.");

		BOX_TRACE("Compare to check that there are differences");
		int compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query0a.log "
			"-Werror \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Different);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		BOX_TRACE("done.");

		// Check that the notify script was run
		TEST_THAT(TestFileExists("testfiles/notifyran.store-full.1"));
		// But only once!
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.2"));
		
		// Kill the daemon
		terminate_bbackupd(bbackupd_pid);

		BOX_TRACE("Wait for housekeeping to remove the deleted files");
		wait_for_backup_operation(5);
		BOX_TRACE("done.");

		// This removes f1 and d7, which were previously marked
		// as deleted, so total usage drops by 4 blocks to 24.

		// BLOCK
		{
			std::auto_ptr<BackupProtocolClient> client =
				ConnectAndLogin(context, 0 /* read-write */);
		
			std::auto_ptr<BackupProtocolClientAccountUsage> usage(
				client->QueryGetAccountUsage());
			TEST_EQUAL(24, usage->GetBlocksUsed(), "blocks used");
			TEST_EQUAL(0,  usage->GetBlocksInDeletedFiles(),
				"deleted blocks");
			TEST_EQUAL(16, usage->GetBlocksInDirectories(),
				"directory blocks");

			client->QueryFinished();
			sSocket.Close();
		}

		if (failures > 0)
		{
			// stop early to make debugging easier
			return 1;
		}

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
		bbackupd.Configure("testfiles/bbackupd-exclude.conf");
		bbackupd.InitCrypto();
		BOX_TRACE("done.");

		// Should be marked as deleted by this run
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

		// All these should be marked as deleted but hopefully
		// not removed by housekeeping yet:
		// f1		deleted
		// f2		excluded
		// d1		excluded (why?)
		// d1/f3	excluded (why?)
		// d3		excluded
		// d3/d4	excluded
		// d3/d4/f5	excluded
		// d7		deleted
		// Careful with timing here, these files can already be
		// deleted by housekeeping. On Win32, housekeeping runs
		// immediately after disconnect, but only if enough time
		// has elapsed since the last housekeeping. Since the
		// backup run closely follows the last one, housekeeping
		// should not run afterwards. By waiting before
		// connecting to check the results, we should force
		// housekeeping to run after that check, so the next check
		// will see that the deleted files have been removed.

		#ifdef WIN32
			BOX_TRACE("Wait long enough that housekeeping "
				"will run again")
			wait_for_backup_operation(5);
			BOX_TRACE("done.");
		#endif

		BOX_TRACE("Find out whether bbackupd marked files as deleted");
		{
			std::auto_ptr<BackupProtocolClient> client =
				ConnectAndLogin(context, 0 /* read-write */);
		
			std::auto_ptr<BackupStoreDirectory> rootDir = 
				ReadDirectory(*client,
				BackupProtocolClientListDirectory::RootDirectory);

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

			std::auto_ptr<BackupProtocolClientAccountUsage> usage(
				client->QueryGetAccountUsage());
			TEST_EQUAL(24, usage->GetBlocksUsed(), "blocks used");
			TEST_EQUAL(4, usage->GetBlocksInDeletedFiles(),
				"deleted blocks");
			TEST_EQUAL(16, usage->GetBlocksInDirectories(),
				"directory blocks");
			// d1/f3 and d1/f4 are the only two files on the
			// server which are not deleted, they use 2 blocks
			// each, the rest is directories and 2 deleted files
			// (f1 and d3/d4/f5)

			// Log out.
			client->QueryFinished();
			sSocket.Close();
		}
		BOX_TRACE("done.");

		if (failures > 0)
		{
			// stop early to make debugging easier
			return 1;
		}

		// Wait for housekeeping to run
		BOX_TRACE("Wait for housekeeping to remove the deleted files");
		wait_for_backup_operation(5);
		BOX_TRACE("done.");

		BOX_TRACE("Check that the files were removed");
		{
			std::auto_ptr<BackupProtocolClient> client = 
				ConnectAndLogin(context, 0 /* read-write */);
			
			std::auto_ptr<BackupStoreDirectory> rootDir = 
				ReadDirectory(*client,
				BackupProtocolClientListDirectory::RootDirectory);

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

			std::auto_ptr<BackupProtocolClientAccountUsage> usage(
				client->QueryGetAccountUsage());
			TEST_EQUAL(16, usage->GetBlocksUsed(), "blocks used");
			TEST_EQUAL(0, usage->GetBlocksInDeletedFiles(),
				"deleted blocks");
			TEST_EQUAL(12, usage->GetBlocksInDirectories(),
				"directory blocks");
			// d1/f3 and d1/f4 are the only two files on the
			// server, they use 2 blocks each, the rest is
			// directories.

			// Log out.
			client->QueryFinished();
			sSocket.Close();
		}

		if (failures > 0)
		{
			// stop early to make debugging easier
			return 1;
		}

		// Need 22 blocks free to upload everything
		TEST_THAT_ABORTONFAIL(::system(BBSTOREACCOUNTS " -c "
			"testfiles/bbstored.conf setlimit 01234567 0B 22B") 
			== 0);
		TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");

		// Run another backup, now there should be enough space
		// for everything we want to upload.
		{
			Logging::Guard guard(Log::ERROR);
			bbackupd.RunSyncNow();
		}
		TEST_THAT(!bbackupd.StorageLimitExceeded());

		// Check that the contents of the store are the same 
		// as the contents of the disc 
		// (-a = all, -c = give result in return code)
		BOX_TRACE("Check that all files were uploaded successfully");
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd-exclude.conf "
			"-l testfiles/query1.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		BOX_TRACE("done.");

		// BLOCK
		{
			std::auto_ptr<BackupProtocolClient> client = 
				ConnectAndLogin(context, 0 /* read-write */);

			std::auto_ptr<BackupProtocolClientAccountUsage> usage(
				client->QueryGetAccountUsage());
			TEST_EQUAL(22, usage->GetBlocksUsed(), "blocks used");
			TEST_EQUAL(0, usage->GetBlocksInDeletedFiles(),
				"deleted blocks");
			TEST_EQUAL(14, usage->GetBlocksInDirectories(),
				"directory blocks");
			// d2/f6, d6/d8 and d6/d8/f7 are new
			// i.e. 2 new files, 1 new directory

			client->QueryFinished();
			sSocket.Close();
		}

		if (failures > 0)
		{
			// stop early to make debugging easier
			return 1;
		}

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
		BOX_TRACE("done.");

		// unpack the initial files again
		#ifdef WIN32
			TEST_THAT(::system("tar xzvf testfiles/test_base.tgz "
				"-C testfiles") == 0);
		#else
			TEST_THAT(::system("gzip -d < testfiles/test_base.tgz "
				"| ( cd testfiles && tar xf - )") == 0);
		#endif

		BOX_TRACE("Wait for bbackupd to upload more files");
		wait_for_backup_operation();
		BOX_TRACE("done.");
		
		// Check that the contents of the store are the same 
		// as the contents of the disc 
		// (-a = all, -c = give result in return code)
		BOX_TRACE("Check that all files were uploaded successfully");
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query1.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue, 
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		BOX_TRACE("done.");

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		if (failures > 0)
		{
			// stop early to make debugging easier
			return 1;
		}
	}

	#ifndef WIN32 // requires fork
	printf("\n==== Testing that bbackupd responds correctly to "
		"connection failure\n");

	{
		// Kill the daemons
		terminate_bbackupd(bbackupd_pid);
		test_kill_bbstored();

		// create a new file to force an upload

		char* new_file = "testfiles/TestDir1/force-upload-2";
		int fd = open(new_file, 
			O_CREAT | O_EXCL | O_WRONLY, 0700);
		if (fd <= 0)
		{
			perror(new_file);
		}
		TEST_THAT(fd > 0);
	
		char* control_string = "whee!\n";
		TEST_THAT(write(fd, control_string, 
			strlen(control_string)) ==
			(int)strlen(control_string));
		close(fd);

		// sleep to make it old enough to upload
		safe_sleep(4);

		class MyHook : public BackupStoreContext::TestHook
		{
			virtual std::auto_ptr<ProtocolObject> StartCommand(
				BackupProtocolObject& rCommand)
			{
				if (rCommand.GetType() ==
					BackupProtocolServerStoreFile::TypeID)
				{
					// terminate badly
					THROW_EXCEPTION(CommonException,
						Internal);
				}
				return std::auto_ptr<ProtocolObject>();
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

			if (!Logging::IsEnabled(Log::TRACE))
			{
				Logging::SetGlobalLevel(Log::NOTHING);
			}

			// BackupStoreDaemon must be destroyed before exit(),
			// to avoid memory leaks being reported.
			{
				BackupStoreDaemon bbstored;
				bbstored.SetTestHook(hook);
				bbstored.SetRunInForeground(true);
				bbstored.Main("testfiles/bbstored.conf");
			}

			Timers::Cleanup(); // avoid memory leaks
			exit(0);
		}

		// in fork parent
		bbstored_pid = WaitForServerStartup("testfiles/bbstored.pid",
			bbstored_pid);

		TEST_THAT(::system("rm -f testfiles/notifyran.store-full.*") == 0);

		// Ignore SIGPIPE so that when the connection is broken,
		// the daemon doesn't terminate.
		::signal(SIGPIPE, SIG_IGN);

		{
			Log::Level newLevel = Logging::GetGlobalLevel();

			if (!Logging::IsEnabled(Log::TRACE))
			{
				newLevel = Log::NOTHING;
			}

			Logging::Guard guard(newLevel);

			BackupDaemon bbackupd;
			bbackupd.Configure("testfiles/bbackupd.conf");
			bbackupd.InitCrypto();
			bbackupd.RunSyncNowWithExceptionHandling();
		}

		::signal(SIGPIPE, SIG_DFL);

		TEST_THAT(TestFileExists("testfiles/notifyran.backup-error.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-error.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.1"));

		test_kill_bbstored(true);

		if (failures > 0)
		{
			// stop early to make debugging easier
			return 1;
		}

		test_run_bbstored();

		cmd = BBACKUPD " " + bbackupd_args +
			" testfiles/bbackupd.conf";
		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);
		::safe_sleep(1);
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;
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
		path += DIRECTORY_SEPARATOR SYM_DIR 
			DIRECTORY_SEPARATOR "a"
			DIRECTORY_SEPARATOR "subdir";
		TEST_THAT(symlink(path.c_str(), SYM_DIR 
			DIRECTORY_SEPARATOR "b"
			DIRECTORY_SEPARATOR "link") == 0);

		::wait_for_operation(4);
		::sync_and_wait();

		// Check that the backup was successful, i.e. no differences
		int compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query1.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

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
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-Wwarning \"restore Test1 testfiles/restore-symlink\" "
			"quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Command_OK);

		// make it accessible again
		TEST_THAT(chmod(SYM_DIR, 0755) == 0);

		// check that the original file was not overwritten
		FileStream fs(SYM_DIR "/a/subdir/content");
		IOStreamGetLine gl(fs);
		std::string line;
		TEST_THAT(gl.GetLine(line));
		TEST_THAT(line != "before");
		TEST_EQUAL("after", line, line);

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
		TEST_EQUAL((stat_st.st_dev ^ 0xFFFF), lstat_st.st_dev,
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
	}
	#endif // !WIN32

	printf("\n==== Testing that redundant locations are deleted on time\n");

	// BLOCK
	{
		// Kill the daemon
		terminate_bbackupd(bbackupd_pid);

		// Start it with a config that has a temporary location
		// that will be created on the server
		std::string cmd = BBACKUPD " " + bbackupd_args + 
			" testfiles/bbackupd-temploc.conf";

		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);
		::safe_sleep(1);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		sync_and_wait();

		{
			std::auto_ptr<BackupProtocolClient> client =
				ConnectAndLogin(context,
				BackupProtocolClientLogin::Flags_ReadOnly);
			
			std::auto_ptr<BackupStoreDirectory> dir = 
				ReadDirectory(*client,
				BackupProtocolClientListDirectory::RootDirectory);
			int64_t testDirId = SearchDir(*dir, "Test2");
			TEST_THAT(testDirId != 0);

			client->QueryFinished();
			sSocket.Close();
		}

		// Kill the daemon
		terminate_bbackupd(bbackupd_pid);

		// Start it again with the normal config (no Test2)
		cmd = BBACKUPD " " + bbackupd_args +
			" testfiles/bbackupd.conf";
		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");

		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);

		::safe_sleep(1);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		// Test2 should be deleted after 10 seconds (4 runs)
		wait_for_sync_end();
		wait_for_sync_end();
		wait_for_sync_end();

		// not yet! should still be there

		{
			std::auto_ptr<BackupProtocolClient> client =
				ConnectAndLogin(context,
				BackupProtocolClientLogin::Flags_ReadOnly);
			
			std::auto_ptr<BackupStoreDirectory> dir = 
				ReadDirectory(*client,
				BackupProtocolClientListDirectory::RootDirectory);
			int64_t testDirId = SearchDir(*dir, "Test2");
			TEST_THAT(testDirId != 0);

			client->QueryFinished();
			sSocket.Close();
		}

		wait_for_sync_end();

		// NOW it should be gone

		{
			std::auto_ptr<BackupProtocolClient> client =
				ConnectAndLogin(context,
				BackupProtocolClientLogin::Flags_ReadOnly);
			
			std::auto_ptr<BackupStoreDirectory> root_dir = 
				ReadDirectory(*client,
				BackupProtocolClientListDirectory::RootDirectory);

			TEST_THAT(test_entry_deleted(*root_dir, "Test2"));

			client->QueryFinished();
			sSocket.Close();
		}
	}

	TEST_THAT(ServerIsAlive(bbackupd_pid));
	TEST_THAT(ServerIsAlive(bbstored_pid));
	if (!ServerIsAlive(bbackupd_pid)) return 1;
	if (!ServerIsAlive(bbstored_pid)) return 1;

	if(bbackupd_pid > 0)
	{
		printf("\n==== Check that read-only directories and "
			"their contents can be restored.\n");

		{
			#ifdef WIN32
				TEST_THAT(::system("chmod 0555 testfiles/"
					"TestDir1/x1") == 0);
			#else
				TEST_THAT(chmod("testfiles/TestDir1/x1",
					0555) == 0);
			#endif

			wait_for_sync_end(); // too new
			wait_for_sync_end(); // should be backed up now

			int compareReturnValue = ::system(BBACKUPQUERY " "
				"-Wwarning "
				"-c testfiles/bbackupd.conf "
				"\"compare -cEQ Test1 testfiles/TestDir1\" " 
				"quit");
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Compare_Same);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			// check that we can restore it
			compareReturnValue = ::system(BBACKUPQUERY " "
				"-Wwarning "
				"-c testfiles/bbackupd.conf "
				"\"restore Test1 testfiles/restore1\" "
				"quit");
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Command_OK);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			// check that it restored properly
			compareReturnValue = ::system(BBACKUPQUERY " "
				"-Wwarning "
				"-c testfiles/bbackupd.conf "
				"\"compare -cEQ Test1 testfiles/restore1\" " 
				"quit");
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Compare_Same);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			// put the permissions back to sensible values
			#ifdef WIN32
				TEST_THAT(::system("chmod 0755 testfiles/"
					"TestDir1/x1") == 0);
			#else
				TEST_THAT(chmod("testfiles/TestDir1/x1",
					0755) == 0);
			#endif

		}

		int compareReturnValue;

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
		// directory using a relative path
		std::string command = BBACKUPQUERY " "
			"-Wwarning "
			"-c testfiles/bbackupd.conf "
			"\"lcd testfiles/TestDir1/" + systemDirName + "\" "
			"quit";
		compareReturnValue = ::system(command.c_str());
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Command_OK);

		// and back out again
		command = BBACKUPQUERY " "
			"-Wwarning "
			"-c testfiles/bbackupd.conf "
			"\"lcd testfiles/TestDir1/" + systemDirName + "\" "
			"\"lcd ..\" quit";
		compareReturnValue = ::system(command.c_str());
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Command_OK);

		// and using an absolute path
		command = BBACKUPQUERY " "
			"-Wwarning "
			"-c testfiles/bbackupd.conf "
			"\"lcd " + cwd + "/testfiles/TestDir1/" + 
			systemDirName + "\" quit";
		compareReturnValue = ::system(command.c_str());
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Command_OK);

		// and back out again
		command = BBACKUPQUERY " "
			"-Wwarning "
			"-c testfiles/bbackupd.conf "
			"\"lcd " + cwd + "/testfiles/TestDir1/" + 
			systemDirName + "\" "
			"\"lcd ..\" quit";
		compareReturnValue = ::system(command.c_str());
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Command_OK);

		{
			FileStream fs(filepath.c_str(), O_CREAT | O_RDWR);

			std::string data("hello world\n");
			fs.Write(data.c_str(), data.size());
			TEST_EQUAL(12, fs.GetPosition(), "FileStream position");
			fs.Close();
		}

		wait_for_backup_operation();
		// Compare to check that the file was uploaded
		compareReturnValue = ::system(BBACKUPQUERY " -Wwarning "
			"-c testfiles/bbackupd.conf \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// Check that we can find it in directory listing
		{
			std::auto_ptr<BackupProtocolClient> client =
				ConnectAndLogin(context, 0);

			std::auto_ptr<BackupStoreDirectory> dir = ReadDirectory(
				*client, 
				BackupProtocolClientListDirectory::RootDirectory);

			int64_t baseDirId = SearchDir(*dir, "Test1");
			TEST_THAT(baseDirId != 0);
			dir = ReadDirectory(*client, baseDirId);

			int64_t testDirId = SearchDir(*dir, dirname.c_str());
			TEST_THAT(testDirId != 0);
			dir = ReadDirectory(*client, testDirId);
		
			TEST_THAT(SearchDir(*dir, filename.c_str()) != 0);
			// Log out
			client->QueryFinished();
			sSocket.Close();
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
		command = BBACKUPQUERY " -c testfiles/bbackupd.conf "
			"-Wwarning \"compare -cEQ Test1/" + systemDirName +
			" testfiles/TestDir1/" + systemDirName + "\" quit";

		compareReturnValue = ::system(command.c_str());
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);

		// Check that bbackupquery can restore the dir when given
		// on the command line in system encoding.
		command = BBACKUPQUERY " -c testfiles/bbackupd.conf "
			"-Wwarning \"restore Test1/" + systemDirName +
			" testfiles/restore-" + systemDirName + "\" quit";

		compareReturnValue = ::system(command.c_str());
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Command_OK);

		// Compare to make sure it was restored properly.
		command = BBACKUPQUERY " -c testfiles/bbackupd.conf "
			"-Wwarning \"compare -cEQ Test1/" + systemDirName +
			" testfiles/restore-" + systemDirName + "\" quit";

		compareReturnValue = ::system(command.c_str());
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);

		std::string fileToUnlink = "testfiles/restore-" + 
			dirname + "/" + filename;
		TEST_THAT(::unlink(fileToUnlink.c_str()) == 0);

		// Check that bbackupquery can get the file when given
		// on the command line in system encoding.
		command = BBACKUPQUERY " -c testfiles/bbackupd.conf "
			"-Wwarning \"get Test1/" + systemDirName + "/" + 
			systemFileName + " " + "testfiles/restore-" + 
			systemDirName + "/" + systemFileName + "\" quit";

		compareReturnValue = ::system(command.c_str());
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Command_OK);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// And after changing directory to a relative path
		command = BBACKUPQUERY " -c testfiles/bbackupd.conf "
			"-Wwarning "
			"\"lcd testfiles\" "
			"\"cd Test1/" + systemDirName + "\" " + 
			"\"get " + systemFileName + "\" quit";

		compareReturnValue = ::system(command.c_str());
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Command_OK);
		TestRemoteProcessMemLeaks("testfiles/bbackupquery.memleaks");

		// cannot overwrite a file that exists, so delete it
		std::string tmp = "testfiles/" + filename;
		TEST_THAT(::unlink(tmp.c_str()) == 0);

		// And after changing directory to an absolute path
		command = BBACKUPQUERY " -c testfiles/bbackupd.conf -Wwarning "
			"\"lcd " + cwd + "/testfiles\" "
			"\"cd Test1/" + systemDirName + "\" " + 
			"\"get " + systemFileName + "\" quit";

		compareReturnValue = ::system(command.c_str());
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Command_OK);
		TestRemoteProcessMemLeaks("testfiles/bbackupquery.memleaks");

		// Compare to make sure it was restored properly.
		// The Get command does not restore attributes, so
		// we must compare without them (-A) to succeed.
		command = BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-Wwarning \"compare -cAEQ Test1/" + systemDirName +
			" testfiles/restore-" + systemDirName + "\" quit";

		compareReturnValue = ::system(command.c_str());
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);

		// Compare without attributes. This should fail.
		command = BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-Werror \"compare -cEQ Test1/" + systemDirName +
			" testfiles/restore-" + systemDirName + "\" quit";
		compareReturnValue = ::system(command.c_str());
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Different);
#endif // WIN32

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		printf("\n==== Check that SyncAllowScript is executed and can "
			"pause backup\n");
		fflush(stdout);

		{
			wait_for_sync_end();
			// we now have 3 seconds before bbackupd
			// runs the SyncAllowScript again.

			char* sync_control_file = "testfiles" 
				DIRECTORY_SEPARATOR "syncallowscript.control";
			int fd = open(sync_control_file, 
				O_CREAT | O_EXCL | O_WRONLY, 0700);
			if (fd <= 0)
			{
				perror(sync_control_file);
			}
			TEST_THAT(fd > 0);
		
			char* control_string = "10\n";
			TEST_THAT(write(fd, control_string, 
				strlen(control_string)) ==
				(int)strlen(control_string));
			close(fd);

			// this will pause backups, bbackupd will check
			// every 10 seconds to see if they are allowed again.

			char* new_test_file = "testfiles"
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

			safe_sleep(1); // 2 seconds before
			TEST_THAT(stat("testfiles" DIRECTORY_SEPARATOR 
				"syncallowscript.notifyran.1", &st) != 0);
			safe_sleep(4); // 2 seconds after
			TEST_THAT(stat("testfiles" DIRECTORY_SEPARATOR 
				"syncallowscript.notifyran.1", &st) == 0);
			TEST_THAT(stat("testfiles" DIRECTORY_SEPARATOR 
				"syncallowscript.notifyran.2", &st) != 0);

			// next poll should happen within the next
			// 10 seconds (normally about 8 seconds)

			safe_sleep(6); // 2 seconds before
			TEST_THAT(stat("testfiles" DIRECTORY_SEPARATOR 
				"syncallowscript.notifyran.2", &st) != 0);
			safe_sleep(4); // 2 seconds after
			TEST_THAT(stat("testfiles" DIRECTORY_SEPARATOR 
				"syncallowscript.notifyran.2", &st) == 0);

			// bbackupquery compare might take a while
			// on slow machines, so start the timer now
			long start_time = time(NULL);

			// check that no backup has run (compare fails)
			compareReturnValue = ::system(BBACKUPQUERY " "
				"-Werror "
				"-c testfiles/bbackupd.conf "
				"-l testfiles/query3.log "
				"\"compare -acQ\" quit");
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Compare_Different);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			TEST_THAT(unlink(sync_control_file) == 0);
			wait_for_sync_start();
			long end_time = time(NULL);

			long wait_time = end_time - start_time + 2;
			// should be about 10 seconds
			printf("Waited for %ld seconds, should have been %s",
				wait_time, control_string);
			TEST_THAT(wait_time >= 8);
			TEST_THAT(wait_time <= 12);

			wait_for_sync_end();
			// check that backup has run (compare succeeds)
			compareReturnValue = ::system(BBACKUPQUERY " "
				"-Wwarning "
				"-c testfiles/bbackupd.conf "
				"-l testfiles/query3a.log "
				"\"compare -acQ\" quit");
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Compare_Same);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			if (failures > 0)
			{
				// stop early to make debugging easier
				return 1;
			}
		}

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

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
	
		// wait for backup daemon to do it's stuff, and compare again
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " -Wwarning "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query2.log "
			"\"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// Try a quick compare, just for fun
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query2q.log "
			"-Wwarning \"compare -acqQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		// Check that store errors are reported neatly
		printf("\n==== Create store error\n");
		TEST_THAT(system("rm -f testfiles/notifyran.backup-error.*")
			== 0);

		// break the store
		TEST_THAT(::rename("testfiles/0_0/backup/01234567/info.rf",
			"testfiles/0_0/backup/01234567/info.rf.bak") == 0);
		TEST_THAT(::rename("testfiles/0_1/backup/01234567/info.rf",
			"testfiles/0_1/backup/01234567/info.rf.bak") == 0);
		TEST_THAT(::rename("testfiles/0_2/backup/01234567/info.rf",
			"testfiles/0_2/backup/01234567/info.rf.bak") == 0);

		// Create a file to trigger an upload
		{
			int fd1 = open("testfiles/TestDir1/force-upload", 
				O_CREAT | O_EXCL | O_WRONLY, 0700);
			TEST_THAT(fd1 > 0);
			TEST_THAT(write(fd1, "just do it", 10) == 10);
			TEST_THAT(close(fd1) == 0);
		}

		wait_for_backup_operation(4);
		// Check that an error was reported just once
		TEST_THAT(TestFileExists("testfiles/notifyran.backup-error.1"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.backup-error.2"));
		// Now kill bbackupd and start one that's running in
		// snapshot mode, check that it automatically syncs after
		// an error, without waiting for another sync command.
		terminate_bbackupd(bbackupd_pid);
		std::string cmd = BBACKUPD " " + bbackupd_args + 
			" testfiles/bbackupd-snapshot.conf";
		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);
		::safe_sleep(1);
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

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

		// Check that we DO get errors on compare (cannot do this
		// until after we fix the store, which creates a race)
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3b.log "
			"-Werror \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Different);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");		

		// Test initial state
		TEST_THAT(!TestFileExists("testfiles/"
			"notifyran.backup-start.wait-snapshot.1"));

		// Set a tag for the notify script to distinguist from
		// previous runs.
		{
			int fd1 = open("testfiles/notifyscript.tag", 
				O_CREAT | O_EXCL | O_WRONLY, 0700);
			TEST_THAT(fd1 > 0);
			TEST_THAT(write(fd1, "wait-snapshot", 13) == 13);
			TEST_THAT(close(fd1) == 0);
		}

		// bbackupd should pause for about 90 seconds
		wait_for_backup_operation(85);
		TEST_THAT(!TestFileExists("testfiles/"
			"notifyran.backup-start.wait-snapshot.1"));

		// Should not have backed up, should still get errors
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3b.log "
			"-Werror \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Different);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");		

		// wait another 10 seconds, bbackup should have run
		wait_for_backup_operation(10);
		TEST_THAT(TestFileExists("testfiles/"
			"notifyran.backup-start.wait-snapshot.1"));
	
		// Check that it did get uploaded, and we have no more errors
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3b.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");		

		TEST_THAT(::unlink("testfiles/notifyscript.tag") == 0);

		// Stop the snapshot bbackupd
		terminate_bbackupd(bbackupd_pid);

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

		// Restart the old bbackupd, in automatic mode
		cmd = BBACKUPD " " + bbackupd_args + 
			" testfiles/bbackupd.conf";
		bbackupd_pid = LaunchServer(cmd, "testfiles/bbackupd.pid");
		TEST_THAT(bbackupd_pid != -1 && bbackupd_pid != 0);
		::safe_sleep(1);
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		sync_and_wait();

		// Fix the store again
		TEST_THAT(::rename("testfiles/0_0/backup/01234567/info.rf.bak",
			"testfiles/0_0/backup/01234567/info.rf") == 0);
		TEST_THAT(::rename("testfiles/0_1/backup/01234567/info.rf.bak",
			"testfiles/0_1/backup/01234567/info.rf") == 0);
		TEST_THAT(::rename("testfiles/0_2/backup/01234567/info.rf.bak",
			"testfiles/0_2/backup/01234567/info.rf") == 0);

		// Check that we DO get errors on compare (cannot do this
		// until after we fix the store, which creates a race)
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3b.log "
			"-Werror \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Different);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");		

		// Test initial state
		TEST_THAT(!TestFileExists("testfiles/"
			"notifyran.backup-start.wait-automatic.1"));

		// Set a tag for the notify script to distinguist from
		// previous runs.
		{
			int fd1 = open("testfiles/notifyscript.tag", 
				O_CREAT | O_EXCL | O_WRONLY, 0700);
			TEST_THAT(fd1 > 0);
			TEST_THAT(write(fd1, "wait-automatic", 14) == 14);
			TEST_THAT(close(fd1) == 0);
		}

		// bbackupd should pause for at least 90 seconds
		wait_for_backup_operation(85);
		TEST_THAT(!TestFileExists("testfiles/"
			"notifyran.backup-start.wait-automatic.1"));

		// Should not have backed up, should still get errors
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3b.log "
			"-Werror \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Different);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");		

		// wait another 10 seconds, bbackup should have run
		wait_for_backup_operation(10);
		TEST_THAT(TestFileExists("testfiles/"
			"notifyran.backup-start.wait-automatic.1"));
	
		// Check that it did get uploaded, and we have no more errors
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3b.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");		

		TEST_THAT(::unlink("testfiles/notifyscript.tag") == 0);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

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

		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3c.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");		

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

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

		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3d.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		// And then, put it back to how it was before.
		printf("\n==== Replace symlink with directory "
			"(which was a symlink)\n");

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

		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3e.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

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

		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3f.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

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
		wait_for_operation(5);
		// back up both files
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3g.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		#ifdef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/untracked-2")
				== 0);
		#endif

		TEST_THAT(::rename("testfiles/TestDir1/untracked-1", 
			"testfiles/TestDir1/untracked-2") == 0);
		TEST_THAT(!TestFileExists("testfiles/TestDir1/untracked-1"));
		TEST_THAT( TestFileExists("testfiles/TestDir1/untracked-2"));
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3g.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		// case which went wrong: rename a tracked file over an
		// existing tracked file
		printf("\n==== Rename over existing tracked file\n");
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
		wait_for_operation(5);
		// back up both files
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3h.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		#ifdef WIN32
			TEST_THAT(::unlink("testfiles/TestDir1/tracked-2")
				== 0);
		#endif

		TEST_THAT(::rename("testfiles/TestDir1/tracked-1", 
			"testfiles/TestDir1/tracked-2") == 0);
		TEST_THAT(!TestFileExists("testfiles/TestDir1/tracked-1"));
		TEST_THAT( TestFileExists("testfiles/TestDir1/tracked-2"));
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3i.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
	
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		// case which went wrong: rename a tracked file 
		// over a deleted file
		printf("\n==== Rename an existing file over a deleted file\n");
		TEST_THAT(!TestFileExists("testfiles/TestDir1/x1/dsfdsfs98.fd"));
		TEST_THAT(::rename("testfiles/TestDir1/df9834.dsf", 
			"testfiles/TestDir1/x1/dsfdsfs98.fd") == 0);
		
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3j.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		printf("\n==== Add files with old times, update "
			"attributes of one to latest time\n");

		// Move that file back
		TEST_THAT(::rename("testfiles/TestDir1/x1/dsfdsfs98.fd", 
			"testfiles/TestDir1/df9834.dsf") == 0);
		
		// Add some more files
		// Because the 'm' option is not used, these files will 
		// look very old to the daemon.
		// Lucky it'll upload them then!
		#ifdef WIN32
			TEST_THAT(::system("tar xzvf testfiles/test2.tgz "
				"-C testfiles") == 0);
		#else
			TEST_THAT(::system("gzip -d < testfiles/test2.tgz "
				"| ( cd  testfiles && tar xf - )") == 0);
			::chmod("testfiles/TestDir1/sub23/dhsfdss/blf.h", 0415);
		#endif
		
		// Wait and test
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3k.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

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

		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3l.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		// Add some files and directories which are marked as excluded
		printf("\n==== Add files and dirs for exclusion test\n");
		#ifdef WIN32
			TEST_THAT(::system("tar xzvf testfiles/testexclude.tgz "
				"-C testfiles") == 0);
		#else
			TEST_THAT(::system("gzip -d < "
				"testfiles/testexclude.tgz "
				"| ( cd testfiles && tar xf - )") == 0);
		#endif

		// Wait and test
		wait_for_sync_end();
		wait_for_sync_end();
		
		// compare with exclusions, should not find differences
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3m.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// compare without exclusions, should find differences
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3n.log "
			"-Werror \"compare -acEQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Different);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		// check that the excluded files did not make it
		// into the store, and the included files did
		printf("\n==== Check that exclude/alwaysinclude commands "
			"actually work\n");

		{
			std::auto_ptr<BackupProtocolClient> client = 
				ConnectAndLogin(context,
				BackupProtocolClientLogin::Flags_ReadOnly);
			
			std::auto_ptr<BackupStoreDirectory> dir = ReadDirectory(
				*client,
				BackupProtocolClientListDirectory::RootDirectory);

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
			sSocket.Close();
		}

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

#ifndef WIN32
		// These tests only work as non-root users.
		if(::getuid() != 0)
		{
			// Check that read errors are reported neatly
			printf("\n==== Add unreadable files\n");
			
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
			wait_for_backup_operation();
			compareReturnValue = ::system(BBACKUPQUERY " "
				"-c testfiles/bbackupd.conf "
				"-l testfiles/query3o.log "
				"-Werror \"compare -acQ\" quit");

			// should find differences
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Compare_Error);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

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

		printf("\n==== Continuously update file, "
			"check isn't uploaded\n");
		
		// Make sure everything happens at the same point in the 
		// sync cycle: wait until exactly the start of a sync
		wait_for_sync_start();

		// Then wait a second, to make sure the scan is complete
		::safe_sleep(1);

		{
			// Open a file, then save something to it every second
			for(int l = 0; l < 12; ++l)
			{
				FILE *f = ::fopen("testfiles/TestDir1/continousupdate", "w+");
				TEST_THAT(f != 0);
				fprintf(f, "Loop iteration %d\n", l);
				fflush(f);
				fclose(f);

				printf(".");
				fflush(stdout);
				safe_sleep(1);
			}
			printf("\n");
			fflush(stdout);
			
			// Check there's a difference
			compareReturnValue = ::system("perl testfiles/"
				"extcheck1.pl");

			TEST_RETURN(compareReturnValue, 1);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			printf("\n==== Keep on continuously updating file, "
				"check it is uploaded eventually\n");

			for(int l = 0; l < 28; ++l)
			{
				FILE *f = ::fopen("testfiles/TestDir1/"
					"continousupdate", "w+");
				TEST_THAT(f != 0);
				fprintf(f, "Loop 2 iteration %d\n", l);
				fflush(f);
				fclose(f);

				printf(".");
				fflush(stdout);
				safe_sleep(1);
			}
			printf("\n");
			fflush(stdout);

			compareReturnValue = ::system("perl testfiles/"
				"extcheck2.pl");

			TEST_RETURN(compareReturnValue, 1);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		}
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		printf("\n==== Delete directory, change attributes\n");
	
		// Delete a directory
		TEST_THAT(::system("rm -rf testfiles/TestDir1/x1") == 0);
		// Change attributes on an original file.
		::chmod("testfiles/TestDir1/df9834.dsf", 0423);
		
		// Wait and test
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query4.log "
			"-Werror \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
	
		printf("\n==== Restore files and directories\n");
		int64_t deldirid = 0;
		int64_t restoredirid = 0;
		{
			// connect and log in
			std::auto_ptr<BackupProtocolClient> client = 
				ConnectAndLogin(context,
				BackupProtocolClientLogin::Flags_ReadOnly);

			// Find the ID of the Test1 directory
			restoredirid = GetDirID(*client, "Test1", 
				BackupProtocolClientListDirectory::RootDirectory);
			TEST_THAT(restoredirid != 0);

			// Test the restoration
			TEST_THAT(BackupClientRestore(*client, restoredirid, 
				"testfiles/restore-Test1", 
				true /* print progress dots */) 
				== Restore_Complete);

			// On Win32 we can't open another connection
			// to the server, so we'll compare later.

			// Make sure you can't restore a restored directory
			TEST_THAT(BackupClientRestore(*client, restoredirid, 
				"testfiles/restore-Test1", 
				true /* print progress dots */) 
				== Restore_TargetExists);
			
			// Find ID of the deleted directory
			deldirid = GetDirID(*client, "x1", restoredirid);
			TEST_THAT(deldirid != 0);

			// Just check it doesn't bomb out -- will check this 
			// properly later (when bbackupd is stopped)
			TEST_THAT(BackupClientRestore(*client, deldirid, 
				"testfiles/restore-Test1-x1", 
				true /* print progress dots */, 
				true /* deleted files */) 
				== Restore_Complete);

			// Make sure you can't restore to a nonexistant path
			printf("\n==== Try to restore to a path "
				"that doesn't exist\n");
			fflush(stdout);

			{
				Logging::Guard guard(Log::FATAL);
				TEST_THAT(BackupClientRestore(*client,
					restoredirid, 
					"testfiles/no-such-path/subdir", 
					true /* print progress dots */) 
					== Restore_TargetPathNotFound);
			}

			// Log out
			client->QueryFinished();
			sSocket.Close();
		}

		// Compare the restored files
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query10.log "
			"-Wwarning "
			"\"compare -cEQ Test1 testfiles/restore-Test1\" "
			"quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

#ifdef WIN32
		// make one of the files read-only, expect a compare failure
		compareReturnValue = ::system("attrib +r "
			"testfiles\\restore-Test1\\f1.dat");
		TEST_RETURN(compareReturnValue, 0);

		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query10a.log "
			"-Werror "
			"\"compare -cEQ Test1 testfiles/restore-Test1\" "
			"quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Different);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
	
		// set it back, expect no failures
		compareReturnValue = ::system("attrib -r "
			"testfiles\\restore-Test1\\f1.dat");
		TEST_RETURN(compareReturnValue, 0);

		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf -l testfiles/query10a.log "
			"-Wwarning "
			"\"compare -cEQ Test1 testfiles/restore-Test1\" "
			"quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

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
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query10a.log "
			"-Werror "
			"\"compare -cEQ Test1 testfiles/restore-Test1\" "
			"quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Different);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// last access time is not backed up, so it cannot be compared
		TEST_THAT(set_file_time(testfile, creationTime, lastModTime,
			dummyTime));
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query10a.log "
			"-Wwarning "
			"\"compare -cEQ Test1 testfiles/restore-Test1\" "
			"quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// last write time is backed up, so changing it should cause
		// a compare failure
		TEST_THAT(set_file_time(testfile, creationTime, dummyTime,
			lastAccessTime));
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query10a.log "
			"-Werror "
			"\"compare -cEQ Test1 testfiles/restore-Test1\" "
			"quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Different);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// set back to original values, check that compare succeeds
		TEST_THAT(set_file_time(testfile, creationTime, lastModTime,
			lastAccessTime));
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query10a.log "
			"-Wwarning "
			"\"compare -cEQ Test1 testfiles/restore-Test1\" "
			"quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
#endif // WIN32

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		printf("\n==== Add files with current time\n");
	
		// Add some more files and modify others
		// Use the m flag this time so they have a recent modification time
		#ifdef WIN32
			TEST_THAT(::system("tar xzvmf testfiles/test3.tgz "
				"-C testfiles") == 0);
		#else
			TEST_THAT(::system("gzip -d < testfiles/test3.tgz "
				"| ( cd testfiles && tar xmf - )") == 0);
		#endif
		
		// Wait and test
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query5.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		// Rename directory
		printf("\n==== Rename directory\n");
		TEST_THAT(rename("testfiles/TestDir1/sub23/dhsfdss", 
			"testfiles/TestDir1/renamed-dir") == 0);
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query6.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// and again, but with quick flag
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query6q.log "
			"-Wwarning \"compare -acqQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// Rename some files -- one under the threshold, others above
		printf("\n==== Rename files\n");
		TEST_THAT(rename("testfiles/TestDir1/continousupdate", 
			"testfiles/TestDir1/continousupdate-ren") == 0);
		TEST_THAT(rename("testfiles/TestDir1/df324", 
			"testfiles/TestDir1/df324-ren") == 0);
		TEST_THAT(rename("testfiles/TestDir1/sub23/find2perl", 
			"testfiles/TestDir1/find2perl-ren") == 0);
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query6.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

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
		wait_for_backup_operation();
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query3e.log "
			"-Wwarning \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Same);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

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
					std::auto_ptr<BackupProtocolClient>
						protocol = Connect(context);
					// Make sure the marker isn't zero,
					// because that's the default, and
					// it should have changed
					std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocol->QueryLogin(0x01234567, 0));
					TEST_THAT(loginConf->GetClientStoreMarker() != 0);
					
					// Change it to something else
					protocol->QuerySetClientStoreMarker(12);
					
					// Success!
					done = true;
					
					// Log out
					protocol->QueryFinished();
					sSocket.Close();
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

		printf("\n==== Check change of store marker pauses daemon\n");
		
		// Make a change to a file, to detect whether or not 
		// it's hanging around waiting to retry.
		{
			FILE *f = ::fopen("testfiles/TestDir1/fileaftermarker", "w");
			TEST_THAT(f != 0);
			::fprintf(f, "Lovely file you got there.");
			::fclose(f);
		}

		// Wait and test that there *are* differences
		wait_for_backup_operation((TIME_TO_WAIT_FOR_BACKUP_OPERATION * 
			3) / 2); // little bit longer than usual
		compareReturnValue = ::system(BBACKUPQUERY " "
			"-c testfiles/bbackupd.conf "
			"-l testfiles/query6.log "
			"-Werror \"compare -acQ\" quit");
		TEST_RETURN(compareReturnValue,
			BackupQueries::ReturnCode::Compare_Different);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
	
		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		printf("\n==== Waiting for bbackupd to recover\n");
		// 100 seconds - (12*3/2)
		wait_for_operation(82);

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

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

			std::auto_ptr<BackupProtocolClient> client = 
				ConnectAndLogin(context,
				BackupProtocolClientLogin::Flags_ReadOnly);

			// Check that the restore fn returns resume possible,
			// rather than doing anything
			TEST_THAT(BackupClientRestore(*client, restoredirid, 
				"testfiles/restore-interrupt", 
				true /* print progress dots */) 
				== Restore_ResumePossible);

			// Then resume it
			TEST_THAT(BackupClientRestore(*client, restoredirid, 
				"testfiles/restore-interrupt", 
				true /* print progress dots */, 
				false /* deleted files */, 
				false /* undelete server */, 
				true /* resume */) 
				== Restore_Complete);

			client->QueryFinished();
			sSocket.Close();

			// Then check it has restored the correct stuff
			compareReturnValue = ::system(BBACKUPQUERY " "
				"-c testfiles/bbackupd.conf "
				"-l testfiles/query14.log "
				"-Wwarning \"compare -cEQ Test1 "
				"testfiles/restore-interrupt\" quit");
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Compare_Same);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		}
#endif // !WIN32

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

		printf("\n==== Check restore deleted files\n");

		{
			std::auto_ptr<BackupProtocolClient> client = 
				ConnectAndLogin(context, 0 /* read-write */);

			// Do restore and undelete
			TEST_THAT(BackupClientRestore(*client, deldirid, 
				"testfiles/restore-Test1-x1-2", 
				true /* print progress dots */, 
				true /* deleted files */, 
				true /* undelete on server */) 
				== Restore_Complete);

			client->QueryFinished();
			sSocket.Close();

			// Do a compare with the now undeleted files
			compareReturnValue = ::system(BBACKUPQUERY " "
				"-c testfiles/bbackupd.conf "
				"-l testfiles/query11.log "
				"-Wwarning "
				"\"compare -cEQ Test1/x1 "
				"testfiles/restore-Test1-x1-2\" quit");
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Compare_Same);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		}
		
		// Final check on notifications
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.2"));

		TEST_THAT(ServerIsAlive(bbackupd_pid));
		TEST_THAT(ServerIsAlive(bbstored_pid));
		if (!ServerIsAlive(bbackupd_pid)) return 1;
		if (!ServerIsAlive(bbstored_pid)) return 1;

#ifdef WIN32
		printf("\n==== Testing locked file behaviour:\n");

		// Test that locked files cannot be backed up,
		// and the appropriate error is reported.
		// Wait for the sync to finish, so that we have time to work
		wait_for_sync_start();
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

			// this sync should try to back up the file, 
			// and fail, because it's locked
			wait_for_sync_end();
			TEST_THAT(TestFileExists("testfiles/"
				"notifyran.read-error.1"));
			TEST_THAT(!TestFileExists("testfiles/"
				"notifyran.read-error.2"));

			// now close the file and check that it is
			// backed up on the next run.
			CloseHandle(handle);
			wait_for_sync_end();
			TEST_THAT(!TestFileExists("testfiles/"
				"notifyran.read-error.2"));

			// compare, and check that it works
			// reports the correct error message (and finishes)
			compareReturnValue = ::system(BBACKUPQUERY " "
				"-c testfiles/bbackupd.conf "
				"-l testfiles/query15a.log "
				"-Wwarning \"compare -acQ\" quit");
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Compare_Same);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			// open the file again, compare and check that compare
			// reports the correct error message (and finishes)
			handle = openfile("testfiles/TestDir1/lockedfile",
				O_LOCK, 0);
			TEST_THAT(handle != INVALID_HANDLE_VALUE);

			compareReturnValue = ::system(BBACKUPQUERY " "
				"-c testfiles/bbackupd.conf "
				"-l testfiles/query15.log "
				"-Werror \"compare -acQ\" quit");
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Compare_Error);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			// close the file again, check that compare
			// works again
			CloseHandle(handle);

			compareReturnValue = ::system(BBACKUPQUERY " "
				"-c testfiles/bbackupd.conf "
				"-l testfiles/query15a.log "
				"-Wwarning \"compare -acQ\" quit");
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Compare_Same);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");
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

		if(bbackupd_pid != -1 && bbackupd_pid != 0)
		{
			// Wait and compare (a little bit longer than usual)
			wait_for_backup_operation(
				(TIME_TO_WAIT_FOR_BACKUP_OPERATION*3) / 2); 
			compareReturnValue = ::system(BBACKUPQUERY " "
				"-c testfiles/bbackupd.conf "
				"-l testfiles/query4a.log "
				"-Wwarning \"compare -acQ\" quit");
			TEST_RETURN(compareReturnValue,
				BackupQueries::ReturnCode::Compare_Same);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			// Kill it again
			terminate_bbackupd(bbackupd_pid);
		}
	}

	/*
	// List the files on the server - why?
	::system(BBACKUPQUERY " -q -c testfiles/bbackupd.conf "
		"-l testfiles/queryLIST.log \"list -rotdh\" quit");
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

	// Do the tests

	int r = test_basics();
	if(r != 0) return r;
	
	r = test_setupaccount();
	if(r != 0) return r;

	r = test_run_bbstored();
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
	
	test_kill_bbstored();

	return 0;
}
