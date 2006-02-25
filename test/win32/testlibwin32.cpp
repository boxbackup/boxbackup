// win32test.cpp : Defines the entry point for the console application.
//

//#include <windows.h>
#include "Box.h"

#ifdef WIN32

#include <assert.h>

#include "../../bin/bbackupd/BackupDaemon.h"
#include "BoxPortsAndFiles.h"
#include "emu.h"

int main(int argc, char* argv[])
{
	chdir("c:\\tmp");
	openfile("test", O_CREAT, 0);
	struct stat ourfs;
	//test our opendir, readdir and closedir
	//functions
	DIR *ourDir = opendir("C:");

	if ( ourDir != NULL )
	{
		struct dirent *info;
		do
		{
			info = readdir(ourDir);
			if ( info ) printf("File/Dir name is : %s\r\n", info->d_name);

		}while ( info != NULL );

		closedir(ourDir);

	}
	
	std::string diry("C:\\Projects\\boxbuild\\testfiles\\");
	ourDir = opendir(diry.c_str());
	if ( ourDir != NULL )
	{
		struct dirent *info;
		do
		{
			info = readdir(ourDir);
			if ( info == NULL ) break;
			std::string file(diry + info->d_name);
			stat(file.c_str(), &ourfs);
			if ( info ) printf("File/Dir name is : %s\r\n", info->d_name);

		}while ( info != NULL );

		closedir(ourDir);

	}

	stat("c:\\windows", &ourfs);
	stat("c:\\autoexec.bat", &ourfs);
	printf("Finished dir read");

	//test our getopt function
	char * test_argv[] = 
	{
		"foobar.exe",
		"-qwc",
		"-",
		"-c",
		"fgfgfg",
		"-f",
		"-l",
		"hello",
		"-",
		"force-sync",
		NULL
	};
	int test_argc;
	for (test_argc = 0; test_argv[test_argc]; test_argc++) { }
	const char* opts = "qwc:l:";

	assert(getopt(test_argc, test_argv, opts) == 'q');
	assert(getopt(test_argc, test_argv, opts) == 'w');
	assert(getopt(test_argc, test_argv, opts) == 'c');
	assert(strcmp(optarg, "-") == 0);
	assert(getopt(test_argc, test_argv, opts) == 'c');
	assert(strcmp(optarg, "fgfgfg") == 0);
	assert(getopt(test_argc, test_argv, opts) == '?');
	assert(optopt == 'f');
	assert(getopt(test_argc, test_argv, opts) == 'l');
	assert(strcmp(optarg, "hello") == 0);
	assert(getopt(test_argc, test_argv, opts) == -1);
	// assert(optopt == 0); // no more options
	assert(strcmp(test_argv[optind], "-") == 0);
	assert(strcmp(test_argv[optind+1], "force-sync") == 0);
	//end of getopt test
	
	//now test our statfs funct
	stat("c:\\cert.cer", &ourfs);

	char *timee;
	
	timee = ctime(&ourfs.st_mtime);

	if (S_ISREG(ourfs.st_mode))
	{
		printf("is a normal file");
	}
	else
	{
		printf("is a directory?");
		exit(1);
	}

	lstat(getenv("WINDIR"), &ourfs);

	if ( S_ISDIR(ourfs.st_mode))
	{
		printf("is a directory");
	}
	else
	{
		printf("is a file?");
		exit(1);
	}

	//test the syslog functions
	openlog("Box Backup", 0,0);
	//the old ones are the best...
	syslog(LOG_ERR, "Hello World");
	syslog(LOG_ERR, "Value of int is: %i", 6);

	closelog();

	//first off get the path name for the default 
	char buf[MAX_PATH];
	
	/*
	GetModuleFileName(NULL, buf, sizeof(buf));
	std::string buffer(buf);
	std::string conf("-c " + buffer.substr(0,(buffer.find("win32test.exe"))) + "bbackupd.conf");
	//std::string conf( "-c " + buffer.substr(0,(buffer.find("bbackupd.exe"))) + "bbackupd.conf");
	*/

	return 0;
}

#endif // WIN32
