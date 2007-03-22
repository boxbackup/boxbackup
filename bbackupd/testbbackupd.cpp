// --------------------------------------------------------------------------
//
// File
//		Name:    testbbackupd.cpp
//		Purpose: test backup daemon (and associated client bits)
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef HAVE_SYS_XATTR_H
#include <cerrno>
#include <sys/xattr.h>
#endif
#include <map>

#ifdef HAVE_SYSCALL
#include <sys/syscall.h>
#endif

#include "Test.h"
#include "BackupClientFileAttributes.h"
#include "CommonException.h"
#include "BackupStoreException.h"
#include "FileModificationTime.h"
#include "autogen_BackupProtocolClient.h"
#include "SSLLib.h"
#include "TLSContext.h"
#include "SocketStreamTLS.h"
#include "BoxPortsAndFiles.h"
#include "BackupStoreConstants.h"
#include "Socket.h"
#include "BackupClientRestore.h"
#include "BackupStoreDirectory.h"
#include "BackupClientCryptoKeys.h"
#include "CollectInBufferStream.h"
#include "Utils.h"
#include "BoxTime.h"
#include "BoxTimeToUnix.h"
#include "BackupDaemon.h"
#include "Timer.h"
#include "FileStream.h"
#include "IOStreamGetLine.h"
#include "intercept.h"
#include "ServerControl.h"

#include "MemLeakFindOn.h"

// ENOATTR may be defined in a separate header file which we may not have
#ifndef ENOATTR
#define ENOATTR ENODATA
#endif

// two cycles and a bit
#define TIME_TO_WAIT_FOR_BACKUP_OPERATION	12

void wait_for_backup_operation(int seconds = TIME_TO_WAIT_FOR_BACKUP_OPERATION)
{
	wait_for_operation(seconds);
}

int bbstored_pid = 0;

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
		if((s2.st_mode & S_IFMT) != S_IFLNK) return false;
		
		char p1[PATH_MAX], p2[PATH_MAX];
		int p1l = ::readlink(f1, p1, PATH_MAX);
		int p2l = ::readlink(f2, p2, PATH_MAX);
		TEST_THAT(p1l != -1 && p2l != -1);
		// terminate strings properly
		p1[p1l] = '\0';
		p2[p2l] = '\0';
		return strcmp(p1, p2) == 0;
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
	BackupClientFileAttributes t2;
	t2.ReadAttributes("testfiles/test2");
	TEST_THAT(t2.IsSymLink());
	// Check that it's actually been encrypted (search for symlink name encoded in it)
	void *te = ::memchr(t2.GetBuffer(), 't', t2.GetSize() - 3);
	TEST_THAT(te == 0 || ::memcmp(te, "test", 4) != 0);
	
	BackupClientFileAttributes t3;
	TEST_CHECK_THROWS(t3.ReadAttributes("doesn't exist"), CommonException, OSFileError);

	// Create some more files
	FILE *f = fopen("testfiles/test1_n", "w");
	fclose(f);
	f = fopen("testfiles/test2_n", "w");
	fclose(f);
	
	// Apply attributes to these new files
	t1.WriteAttributes("testfiles/test1_n");
	t2.WriteAttributes("testfiles/test2_n");
	TEST_CHECK_THROWS(t1.WriteAttributes("testfiles/test1_nXX"), CommonException, OSFileError);
	TEST_CHECK_THROWS(t3.WriteAttributes("doesn't exist"), BackupStoreException, AttributesNotLoaded);

	// Test that atttributes are vaguely similar
	TEST_THAT(attrmatch("testfiles/test1", "testfiles/test1_n"));
	TEST_THAT(attrmatch("testfiles/test2", "testfiles/test2_n"));
	
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
	TEST_THAT_ABORTONFAIL(::system("../../bin/bbstoreaccounts/bbstoreaccounts -c testfiles/bbstored.conf create 01234567 0 1000B 2000B") == 0);
	TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");
	return 0;
}

int test_run_bbstored()
{
	bbstored_pid = LaunchServer("../../bin/bbstored/bbstored testfiles/bbstored.conf", "testfiles/bbstored.pid");
	TEST_THAT(bbstored_pid != -1 && bbstored_pid != 0);
	if(bbstored_pid > 0)
	{
		::sleep(1);
		TEST_THAT(ServerIsAlive(bbstored_pid));
		return 0;	// success
	}
	
	return 1;
}

int test_kill_bbstored()
{
	TEST_THAT(KillServer(bbstored_pid));
	::sleep(1);
	TEST_THAT(!ServerIsAlive(bbstored_pid));
	TestRemoteProcessMemLeaks("bbstored.memleaks");
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
			conn.Open(context, Socket::TypeINET, "localhost", BOX_PORT_BBSTORED);
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

int start_internal_daemon()
{
	// ensure that no child processes end up running tests!
	int own_pid = getpid();
		
	BackupDaemon daemon;
	const char* fake_argv[] = { "bbackupd", "testfiles/bbackupd.conf" };
	
	int result = daemon.Main(BOX_FILE_BBACKUPD_DEFAULT_CONFIG, 2, 
		fake_argv);
	
	TEST_THAT(result == 0);
	if (result != 0)
	{
		printf("Daemon exited with code %d\n", result);
	}
	
	// ensure that no child processes end up running tests!
	TEST_THAT(getpid() == own_pid);
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
		sleep(1);

		pid = ReadPidFile("testfiles/bbackupd.pid");
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

void stop_internal_daemon(int pid)
{
	TEST_THAT(KillServer(pid));

	/*
	int status;
	TEST_THAT(waitpid(pid, &status, 0) == pid);
	TEST_THAT(WIFEXITED(status));
	
	if (WIFEXITED(status))
	{
		TEST_THAT(WEXITSTATUS(status) == 0);
	}
	*/
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
#endif
		// we will not be called again.
	}

	// fill in the struct dirent appropriately
	memset(&readdir_test_dirent, 0, sizeof(readdir_test_dirent));
	readdir_test_dirent.d_ino = ++readdir_test_counter;
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

int test_bbackupd()
{
//	// First, wait for a normal period to make sure the last changes attributes are within a normal backup timeframe.
//	wait_for_backup_operation();

	// Connection gubbins
	TLSContext context;
	context.Initialise(false /* client */,
			"testfiles/clientCerts.pem",
			"testfiles/clientPrivKey.pem",
			"testfiles/clientTrustedCAs.pem");

	// unpack the files for the initial test
	TEST_THAT(::system("rm -rf testfiles/TestDir1") == 0);
	TEST_THAT(::system("mkdir testfiles/TestDir1") == 0);
	TEST_THAT(::system("gzip -d < testfiles/spacetest1.tgz | ( cd testfiles/TestDir1 && tar xf - )") == 0);
	
#ifdef PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE
	printf("Skipping intercept-based KeepAlive tests on this platform.\n");
#else
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
		TEST_THAT(write(fd, buffer, sizeof(buffer)) == sizeof(buffer));
		TEST_THAT(close(fd) == 0);
		
		int pid = start_internal_daemon();
		wait_for_backup_operation();
		stop_internal_daemon(pid);

		intercept_setup_delay("testfiles/TestDir1/spacetest/f1", 
			0, 2000, SYS_read, 1);
		TEST_THAT(unlink("testfiles/bbackupd.log") == 0);

		pid = start_internal_daemon();
		
		fd = open("testfiles/TestDir1/spacetest/f1", O_WRONLY);
		TEST_THAT(fd > 0);
		// write again, to update the file's timestamp
		TEST_THAT(write(fd, buffer, sizeof(buffer)) == sizeof(buffer));
		TEST_THAT(close(fd) == 0);	

		wait_for_backup_operation();
		// can't test whether intercept was triggered, because
		// it's in a different process.
		// TEST_THAT(intercept_triggered());
		stop_internal_daemon(pid);

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
			TEST_THAT(line == "Receive Success(0xe)");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Receiving stream, size 124");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Send GetIsAlive()");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Receive IsAlive()");

			TEST_THAT(reader.GetLine(line));
			std::string comp = "Send StoreFile(0x3,";
			TEST_THAT(line.substr(0, comp.size()) == comp);
			comp = ",0xe,\"f1\")";
			TEST_THAT(line.substr(line.size() - comp.size())
				== comp);
		}
		
		intercept_setup_delay("testfiles/TestDir1/spacetest/f1", 
			0, 4000, SYS_read, 1);
		pid = start_internal_daemon();
		
		fd = open("testfiles/TestDir1/spacetest/f1", O_WRONLY);
		TEST_THAT(fd > 0);
		// write again, to update the file's timestamp
		TEST_THAT(write(fd, buffer, sizeof(buffer)) == sizeof(buffer));
		TEST_THAT(close(fd) == 0);	

		wait_for_backup_operation();
		// can't test whether intercept was triggered, because
		// it's in a different process.
		// TEST_THAT(intercept_triggered());
		stop_internal_daemon(pid);

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
			TEST_THAT(line == "Receive Success(0xf)");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Receiving stream, size 124");

			// delaying for 4 seconds in one step means that
			// the diff timer and the keepalive timer will
			// both expire, and the diff timer is honoured first,
			// so there will be no keepalives.

			TEST_THAT(reader.GetLine(line));
			std::string comp = "Send StoreFile(0x3,";
			TEST_THAT(line.substr(0, comp.size()) == comp);
			comp = ",0x0,\"f1\")";
			TEST_THAT(line.substr(line.size() - comp.size())
				== comp);
		}

		intercept_setup_delay("testfiles/TestDir1/spacetest/f1", 
			0, 1000, SYS_read, 3);
		pid = start_internal_daemon();
		
		fd = open("testfiles/TestDir1/spacetest/f1", O_WRONLY);
		TEST_THAT(fd > 0);
		// write again, to update the file's timestamp
		TEST_THAT(write(fd, buffer, sizeof(buffer)) == sizeof(buffer));
		TEST_THAT(close(fd) == 0);	

		wait_for_backup_operation();
		// can't test whether intercept was triggered, because
		// it's in a different process.
		// TEST_THAT(intercept_triggered());
		stop_internal_daemon(pid);

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
			TEST_THAT(line == "Receive Success(0x10)");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Receiving stream, size 124");

			// delaying for 3 seconds in steps of 1 second
			// means that the keepalive timer will expire 3 times,
			// and on the 3rd time the diff timer will expire too.
			// The diff timer is honoured first, so there will be 
			// only two keepalives.
			
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Send GetIsAlive()");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Receive IsAlive()");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Send GetIsAlive()");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Receive IsAlive()");

			TEST_THAT(reader.GetLine(line));
			std::string comp = "Send StoreFile(0x3,";
			TEST_THAT(line.substr(0, comp.size()) == comp);
			comp = ",0x0,\"f1\")";
			TEST_THAT(line.substr(line.size() - comp.size())
				== comp);
		}

		intercept_setup_readdir_hook("testfiles/TestDir1/spacetest/d1", 
			readdir_test_hook_1);
		
		// time for at least two keepalives
		readdir_stop_time = time(NULL) + 12 + 2;

		pid = start_internal_daemon();
		
		std::string touchfile = 
			"testfiles/TestDir1/spacetest/d1/touch-me";

		fd = open(touchfile.c_str(), O_CREAT | O_WRONLY);
		TEST_THAT(fd > 0);
		// write again, to update the file's timestamp
		TEST_THAT(write(fd, buffer, sizeof(buffer)) == sizeof(buffer));
		TEST_THAT(close(fd) == 0);	

		wait_for_backup_operation();
		// can't test whether intercept was triggered, because
		// it's in a different process.
		// TEST_THAT(intercept_triggered());
		stop_internal_daemon(pid);

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
			TEST_THAT(line == "Receive Success(0x3)");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Receiving stream, size 425");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Send GetIsAlive()");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Receive IsAlive()");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Send GetIsAlive()");
			TEST_THAT(reader.GetLine(line));
			TEST_THAT(line == "Receive IsAlive()");
		}

		TEST_THAT(unlink(touchfile.c_str()) == 0);

		// restore timers for rest of tests
		Timers::Init();
	}
#endif // PLATFORM_CLIB_FNS_INTERCEPTION_IMPOSSIBLE

	int pid = LaunchServer("../../bin/bbackupd/bbackupd testfiles/bbackupd.conf", "testfiles/bbackupd.pid");
	TEST_THAT(pid != -1 && pid != 0);
	if(pid > 0)
	{
		::sleep(1);
		TEST_THAT(ServerIsAlive(pid));

		// First, check storage space handling -- wait for file to be uploaded
		wait_for_backup_operation();
		//TEST_THAT_ABORTONFAIL(::system("../../bin/bbstoreaccounts/bbstoreaccounts -c testfiles/bbstored.conf info 01234567") == 0);
		// Set limit to something very small
		// About 28 blocks will be used at this point. bbackupd will only pause if the size used is
		// greater than soft limit + 1/3 of (hard - soft). Set small values for limits accordingly.
		TEST_THAT_ABORTONFAIL(::system("../../bin/bbstoreaccounts/bbstoreaccounts -c testfiles/bbstored.conf setlimit 01234567 10B 40B") == 0);
		TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");

		// Unpack some more files
		TEST_THAT(::system("gzip -d < testfiles/spacetest2.tgz | ( cd testfiles/TestDir1 && tar xf - )") == 0);
		// Delete a file and a directory
		TEST_THAT(::unlink("testfiles/TestDir1/spacetest/d1/f3") == 0);
		TEST_THAT(::system("rm -rf testfiles/TestDir1/spacetest/d3/d4") == 0);
		wait_for_backup_operation();

		// Make sure there are some differences
		int compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query0a.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 2*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// Put the limit back
		TEST_THAT_ABORTONFAIL(::system("../../bin/bbstoreaccounts/bbstoreaccounts -c testfiles/bbstored.conf setlimit 01234567 1000B 2000B") == 0);
		TestRemoteProcessMemLeaks("bbstoreaccounts.memleaks");
		
		// Check that the notify script was run
		TEST_THAT(TestFileExists("testfiles/notifyran.store-full.1"));
		// But only once!
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.2"));
		
		// unpack the initial files again
		TEST_THAT(::system("gzip -d < testfiles/test_base.tgz | ( cd testfiles && tar xf - )") == 0);

		// wait for it to do it's stuff
		wait_for_backup_operation();
		
		// Check that the contents of the store are the same as the contents
		// of the disc (-a = all, -c = give result in return code)
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query1.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		printf("Delete file and update another, create symlink.\n");
		
		// Delete a file
		TEST_THAT(::unlink("testfiles/TestDir1/x1/dsfdsfs98.fd") == 0);
		// New symlink
		TEST_THAT(::symlink("does-not-exist", "testfiles/TestDir1/symlink-to-dir") == 0);
		
		// Update a file (will be uploaded as a diff)
		{
			// Check that the file is over the diffing threshold in the bbstored.conf file
			TEST_THAT(TestGetFileSize("testfiles/TestDir1/f45.df") > 1024);
			
			// Add a bit to the end
			FILE *f = ::fopen("testfiles/TestDir1/f45.df", "a");
			TEST_THAT(f != 0);
			::fprintf(f, "EXTRA STUFF");
			::fclose(f);
			TEST_THAT(TestGetFileSize("testfiles/TestDir1/f45.df") > 1024);
		}
	
		// wait for backup daemon to do it's stuff, and compare again
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query2.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		// Try a quick compare, just for fun
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query2q.log \"compare -acq\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		// Bad case: delete a file/symlink, replace it with a directory
		printf("Replace symlink with directory, add new directory\n");
		TEST_THAT(::unlink("testfiles/TestDir1/symlink-to-dir") == 0);
		TEST_THAT(::mkdir("testfiles/TestDir1/symlink-to-dir", 0755) == 0);
		TEST_THAT(::mkdir("testfiles/TestDir1/x1/dir-to-file", 0755) == 0);
		// NOTE: create a file within the directory to avoid deletion by the housekeeping process later
		TEST_THAT(::symlink("does-not-exist", "testfiles/TestDir1/x1/dir-to-file/contents") == 0);
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query3s.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");		

		// And the inverse, replace a directory with a file/symlink
		printf("Replace directory with symlink\n");
		TEST_THAT(::unlink("testfiles/TestDir1/x1/dir-to-file/contents") == 0);
		TEST_THAT(::rmdir("testfiles/TestDir1/x1/dir-to-file") == 0);
		TEST_THAT(::symlink("does-not-exist", "testfiles/TestDir1/x1/dir-to-file") == 0);
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query3s.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		// And then, put it back to how it was before.
		printf("Replace symlink with directory (which was a symlink)\n");
		TEST_THAT(::unlink("testfiles/TestDir1/x1/dir-to-file") == 0);
		TEST_THAT(::mkdir("testfiles/TestDir1/x1/dir-to-file", 0755) == 0);
		TEST_THAT(::symlink("does-not-exist", "testfiles/TestDir1/x1/dir-to-file/contents2") == 0);
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query3s.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		// And finally, put it back to how it was before it was put back to how it was before
		// This gets lots of nasty things in the store with directories over other old directories.
		printf("Put it all back to how it was\n");
		TEST_THAT(::unlink("testfiles/TestDir1/x1/dir-to-file/contents2") == 0);
		TEST_THAT(::rmdir("testfiles/TestDir1/x1/dir-to-file") == 0);
		TEST_THAT(::symlink("does-not-exist", "testfiles/TestDir1/x1/dir-to-file") == 0);
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query3s.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// case which went wrong: rename a tracked file over a deleted file
		printf("Rename an existing file over a deleted file\n");
		TEST_THAT(::rename("testfiles/TestDir1/df9834.dsf", "testfiles/TestDir1/x1/dsfdsfs98.fd") == 0);
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query3s.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		printf("Add files with old times, update attributes of one to latest time\n");

		// Move that file back
		TEST_THAT(::rename("testfiles/TestDir1/x1/dsfdsfs98.fd", "testfiles/TestDir1/df9834.dsf") == 0);
		
		// Add some more files
		// Because the 'm' option is not used, these files will look very old to the daemon.
		// Lucky it'll upload them then!
		TEST_THAT(::system("gzip -d < testfiles/test2.tgz | ( cd  testfiles && tar xf - )") == 0);
		::chmod("testfiles/TestDir1/sub23/dhsfdss/blf.h", 0415);
		
		// Wait and test
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query3.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		// Check that modifying files with old timestamps still get added
		printf("Modify existing file, but change timestamp to rather old\n");
		// Time critical, so sync
		TEST_THAT(::system("../../bin/bbackupctl/bbackupctl -q -c testfiles/bbackupd.conf wait-for-sync") == 0);
		TestRemoteProcessMemLeaks("bbackupctl.memleaks");
		// Then wait a second, to make sure the scan is complete
		::sleep(1);
		// Then modify an existing file
		{
			chmod("testfiles/TestDir1/sub23/rand.h", 0777);	// in the archive, it's read only
			FILE *f = fopen("testfiles/TestDir1/sub23/rand.h", "w+");
			TEST_THAT(f != 0);
			fprintf(f, "MODIFIED!\n");
			fclose(f);
			// and then move the time backwards!
			struct timeval times[2];
			BoxTimeToTimeval(SecondsToBoxTime((time_t)(365*24*60*60)), times[1]);
			times[0] = times[1];
			TEST_THAT(::utimes("testfiles/TestDir1/sub23/rand.h", times) == 0);
		}
		// Wait and test
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query3e.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// Add some files and directories which are marked as excluded
		printf("Add files and dirs for exclusion test\n");
		TEST_THAT(::system("gzip -d < testfiles/testexclude.tgz | ( cd testfiles && tar xf - )") == 0);
		// Wait and test
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query3c.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query3d.log \"compare -acE\" quit");
		TEST_THAT(compareReturnValue == 2*256);	// should find differences
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// These tests only work as non-root users.
		if(::getuid() != 0)
		{
			// Check that read errors are reported neatly
			printf("Add unreadable files\n");
			{
				// Dir and file which can't be read
				TEST_THAT(::mkdir("testfiles/TestDir1/sub23/read-fail-test-dir", 0000) == 0);
				int fd = ::open("testfiles/TestDir1/read-fail-test-file", O_CREAT | O_WRONLY, 0000);
				TEST_THAT(fd != -1);
				::close(fd);
			}

			// Wait and test...
			wait_for_backup_operation();
			compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query3e.log \"compare -ac\" quit");

			// Check that unreadable files were found
			TEST_THAT(compareReturnValue == 3*256);	

			// Check for memory leaks during compare
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			// Check that it was reported correctly
			TEST_THAT(TestFileExists("testfiles/notifyran.read-error.1"));

			// Check that the error was only reorted once
			TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.2"));

			// Set permissions on file and dir to stop errors in the future
			::chmod("testfiles/TestDir1/sub23/read-fail-test-dir", 0770);
			::chmod("testfiles/TestDir1/read-fail-test-file", 0770);
		}

		printf("Continuously update file, check isn't uploaded\n");
		
		// Make sure everything happens at the same point in the sync cycle: wait until exactly the start of a sync
		TEST_THAT(::system("../../bin/bbackupctl/bbackupctl -c testfiles/bbackupd.conf wait-for-sync") == 0);
		TestRemoteProcessMemLeaks("bbackupctl.memleaks");
		// Then wait a second, to make sure the scan is complete
		::sleep(1);

		{
			// Open a file, then save something to it every second
			for(int l = 0; l < 12; ++l)
			{
				FILE *f = ::fopen("testfiles/TestDir1/continousupdate", "w+");
				TEST_THAT(f != 0);
				fprintf(f, "Loop iteration %d\n", l);
				fflush(f);
				sleep(1);
				printf(".");
				fflush(stdout);
				::fclose(f);
			}
			printf("\n");
			
			// Check there's a difference
			compareReturnValue = ::system("testfiles/extcheck1.pl");
			TEST_THAT(compareReturnValue == 1*256);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			printf("Keep on continuously updating file, check it is uploaded eventually\n");

			for(int l = 0; l < 28; ++l)
			{
				FILE *f = ::fopen("testfiles/TestDir1/continousupdate", "w+");
				TEST_THAT(f != 0);
				fprintf(f, "Loop 2 iteration %d\n", l);
				fflush(f);
				sleep(1);
				printf(".");
				fflush(stdout);
				::fclose(f);
			}
			printf("\n");

			compareReturnValue = ::system("testfiles/extcheck2.pl");
			TEST_THAT(compareReturnValue == 1*256);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		}
		
		printf("Delete directory, change attributes\n");
	
		// Delete a directory
		TEST_THAT(::system("rm -rf testfiles/TestDir1/x1") == 0);
		// Change attributes on an original file.
		::chmod("testfiles/TestDir1/df9834.dsf", 0423);
		
		// Wait and test
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query4.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
	
		printf("Restore files and directories\n");
		int64_t deldirid = 0;
		int64_t restoredirid = 0;
		{
			// connect and log in
			SocketStreamTLS conn;
			conn.Open(context, Socket::TypeINET, "localhost", BOX_PORT_BBSTORED);
			BackupProtocolClient protocol(conn);
			protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION);
			std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocol.QueryLogin(0x01234567, BackupProtocolClientLogin::Flags_ReadOnly));

			// Find the ID of the Test1 directory
			restoredirid = GetDirID(protocol, "Test1", BackupProtocolClientListDirectory::RootDirectory);
			TEST_THAT(restoredirid != 0);

			// Test the restoration
			TEST_THAT(BackupClientRestore(protocol, restoredirid, "testfiles/restore-Test1", true /* print progress dots */) == Restore_Complete);

			// Compare it
			compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query10.log \"compare -cE Test1 testfiles/restore-Test1\" quit");
			TEST_THAT(compareReturnValue == 1*256);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			// Make sure you can't restore a restored directory
			TEST_THAT(BackupClientRestore(protocol, restoredirid, "testfiles/restore-Test1", true /* print progress dots */) == Restore_TargetExists);
			
			// Find ID of the deleted directory
			deldirid = GetDirID(protocol, "x1", restoredirid);
			TEST_THAT(deldirid != 0);

			// Just check it doesn't bomb out -- will check this properly later (when bbackupd is stopped)
			TEST_THAT(BackupClientRestore(protocol, deldirid, "testfiles/restore-Test1-x1", true /* print progress dots */, true /* deleted files */) == Restore_Complete);

			// Log out
			protocol.QueryFinished();
		}

		printf("Add files with current time\n");
	
		// Add some more files and modify others
		// Use the m flag this time so they have a recent modification time
		TEST_THAT(::system("gzip -d < testfiles/test3.tgz | ( cd testfiles && tar xmf - )") == 0);
		
		// Wait and test
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query5.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		// Rename directory
		printf("Rename directory\n");
		TEST_THAT(rename("testfiles/TestDir1/sub23/dhsfdss", "testfiles/TestDir1/renamed-dir") == 0);
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query6.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		// and again, but with quick flag
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query6q.log \"compare -acq\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// Rename some files -- one under the threshold, others above
		printf("Rename files\n");
		TEST_THAT(rename("testfiles/TestDir1/continousupdate", "testfiles/TestDir1/continousupdate-ren") == 0);
		TEST_THAT(rename("testfiles/TestDir1/df324", "testfiles/TestDir1/df324-ren") == 0);
		TEST_THAT(rename("testfiles/TestDir1/sub23/find2perl", "testfiles/TestDir1/find2perl-ren") == 0);
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query6.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		// Check that modifying files with madly in the future timestamps still get added
		printf("Create a file with timestamp to way ahead in the future\n");
		// Time critical, so sync
		TEST_THAT(::system("../../bin/bbackupctl/bbackupctl -q -c testfiles/bbackupd.conf wait-for-sync") == 0);
		TestRemoteProcessMemLeaks("bbackupctl.memleaks");
		// Then wait a second, to make sure the scan is complete
		::sleep(1);
		// Then modify an existing file
		{
			FILE *f = fopen("testfiles/TestDir1/sub23/in-the-future", "w");
			TEST_THAT(f != 0);
			fprintf(f, "Back to the future!\n");
			fclose(f);
			// and then move the time forwards!
			struct timeval times[2];
			BoxTimeToTimeval(GetCurrentBoxTime() + SecondsToBoxTime((time_t)(365*24*60*60)), times[1]);
			times[0] = times[1];
			TEST_THAT(::utimes("testfiles/TestDir1/sub23/in-the-future", times) == 0);
		}
		// Wait and test
		wait_for_backup_operation();
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query3e.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 1*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");

		printf("Change client store marker\n");

		// Then... connect to the server, and change the client store marker. See what that does!
		{
			bool done = false;
			int tries = 4;
			while(!done && tries > 0)
			{
				try
				{
					SocketStreamTLS conn;
					conn.Open(context, Socket::TypeINET, "localhost", BOX_PORT_BBSTORED);
					BackupProtocolClient protocol(conn);
					protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION);
					std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocol.QueryLogin(0x01234567, 0));	// read-write
					// Make sure the marker isn't zero, because that's the default, and it should have changed
					TEST_THAT(loginConf->GetClientStoreMarker() != 0);
					
					// Change it to something else
					protocol.QuerySetClientStoreMarker(12);
					
					// Success!
					done = true;
					
					// Log out
					protocol.QueryFinished();
				}
				catch(...)
				{
					tries--;
				}
			}
			TEST_THAT(done);
		}
		
		printf("Check change of store marker pauses daemon\n");
		
		// Make a change to a file, to detect whether or not it's hanging around
		// waiting to retry.
		{
			FILE *f = ::fopen("testfiles/TestDir1/fileaftermarker", "w");
			TEST_THAT(f != 0);
			::fprintf(f, "Lovely file you got there.");
			::fclose(f);
		}

		// Wait and test that there *are* differences
		wait_for_backup_operation((TIME_TO_WAIT_FOR_BACKUP_OPERATION*3) / 2); // little bit longer than usual
		compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query6.log \"compare -ac\" quit");
		TEST_THAT(compareReturnValue == 2*256);
		TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		
		printf("Interrupted restore\n");
		{
			do_interrupted_restore(context, restoredirid);
			int64_t resumesize = 0;
			TEST_THAT(FileExists("testfiles/restore-interrupt.boxbackupresume", &resumesize));
			TEST_THAT(resumesize > 16);	// make sure it has recorded something to resume

			printf("\nResume restore\n");

			SocketStreamTLS conn;
			conn.Open(context, Socket::TypeINET, "localhost", BOX_PORT_BBSTORED);
			BackupProtocolClient protocol(conn);
			protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION);
			std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocol.QueryLogin(0x01234567, 0));	// read-write

			// Check that the restore fn returns resume possible, rather than doing anything
			TEST_THAT(BackupClientRestore(protocol, restoredirid, "testfiles/restore-interrupt", true /* print progress dots */) == Restore_ResumePossible);

			// Then resume it
			TEST_THAT(BackupClientRestore(protocol, restoredirid, "testfiles/restore-interrupt", true /* print progress dots */, false /* deleted files */, false /* undelete server */, true /* resume */) == Restore_Complete);

			protocol.QueryFinished();

			// Then check it has restored the correct stuff
			compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query14.log \"compare -cE Test1 testfiles/restore-interrupt\" quit");
			TEST_THAT(compareReturnValue == 1*256);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		}

		printf("Check restore deleted files\n");
		{
			SocketStreamTLS conn;
			conn.Open(context, Socket::TypeINET, "localhost", BOX_PORT_BBSTORED);
			BackupProtocolClient protocol(conn);
			protocol.QueryVersion(BACKUP_STORE_SERVER_VERSION);
			std::auto_ptr<BackupProtocolClientLoginConfirmed> loginConf(protocol.QueryLogin(0x01234567, 0));	// read-write

			// Do restore and undelete
			TEST_THAT(BackupClientRestore(protocol, deldirid, "testfiles/restore-Test1-x1-2", true /* print progress dots */, true /* deleted files */, true /* undelete on server */) == Restore_Complete);

			protocol.QueryFinished();

			// Do a compare with the now undeleted files
			compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query11.log \"compare -cE Test1/x1 testfiles/restore-Test1-x1-2\" quit");
			TEST_THAT(compareReturnValue == 1*256);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");
		}
		
		// Final check on notifications
		TEST_THAT(!TestFileExists("testfiles/notifyran.store-full.2"));
		TEST_THAT(!TestFileExists("testfiles/notifyran.read-error.2"));

		// Kill the daemon
		TEST_THAT(KillServer(pid));
		::sleep(1);
		TEST_THAT(!ServerIsAlive(pid));
		TestRemoteProcessMemLeaks("bbackupd.memleaks");
		
		// Start it again
		pid = LaunchServer("../../bin/bbackupd/bbackupd testfiles/bbackupd.conf", "testfiles/bbackupd.pid");
		TEST_THAT(pid != -1 && pid != 0);
		if(pid != -1 && pid != 0)
		{
			// Wait and comapre
			wait_for_backup_operation((TIME_TO_WAIT_FOR_BACKUP_OPERATION*3) / 2); // little bit longer than usual
			compareReturnValue = ::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/query4.log \"compare -ac\" quit");
			TEST_THAT(compareReturnValue == 1*256);
			TestRemoteProcessMemLeaks("bbackupquery.memleaks");

			// Kill it again
			TEST_THAT(KillServer(pid));
			::sleep(1);
			TEST_THAT(!ServerIsAlive(pid));
			TestRemoteProcessMemLeaks("bbackupd.memleaks");
		}
	}

	// List the files on the server
	::system("../../bin/bbackupquery/bbackupquery -q -c testfiles/bbackupd.conf -l testfiles/queryLIST.log \"list -rotdh\" quit");
	TestRemoteProcessMemLeaks("bbackupquery.memleaks");
	
	if(::getuid() == 0)
	{
		::printf("WARNING: This test was run as root. Some tests have been omitted.\n");
	}
	
	return 0;
}

int test(int argc, const char *argv[])
{
	// SSL library
	SSLLib::Initialise();

	// Keys for subsystems
	BackupClientCryptoKeys_Setup("testfiles/bbackupd.keys");

	// Initial files
	TEST_THAT(::system("gzip -d < testfiles/test_base.tgz | ( cd testfiles && tar xf - )") == 0);

	// Do the tests

	int r = test_basics();
	if(r != 0) return r;
	
	r = test_setupaccount();
	if(r != 0) return r;

	r = test_run_bbstored();
	if(r != 0) return r;
	
	r = test_bbackupd();
	if(r != 0) return r;
	
	test_kill_bbstored();

	return 0;
}
