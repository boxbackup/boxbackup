// win32test.cpp : Defines the entry point for the console application.
//

//#include <windows.h>
#include "Box.h"
#include "BackupDaemon.h"
#include "BoxPortsAndFiles.h"


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
#if 0
	//remove - sleepycat include a version of getopt - mine never REALLY worked !
	//test our getopt function
	std::string commline("-q -c fgfgfg -f -l hello");

	int c;
	while((c = getopt(commline.size(), (char * const *)commline.c_str(), "qwc:l:")) != -1)
	{
		printf("switch = %c, param is %s\r\n", c, optarg);
	}
#endif
	//end of getopt test
	
	//now test our statfs funct
	stat("c:\\cert.cer", &ourfs);
	
	

	char *timee;
	
	timee = ctime(&ourfs.st_mtime);

	if ( S_ISREG(ourfs.st_mode))
	{
		printf("is a normal file");
	}
	else
	{
		printf("is a directory?");
	}

	lstat("c:\\windows", &ourfs);

	if ( S_ISDIR(ourfs.st_mode))
	{
		printf("is a directory");
	}
	else
	{
		printf("is a file?");
	}

	//test the syslog functions
	openlog("Box Backup", 0,0);
	//the old ones are the best...
	syslog(LOG_ERR, "Hello World");
	syslog(LOG_ERR, "Value of int is: %i", 6);

	closelog();

	//first off get the path name for the default 
	char buf[MAX_PATH];
	
	GetModuleFileName(NULL, buf, sizeof(buf));
	std::string buffer(buf);
	std::string conf("-c " + buffer.substr(0,(buffer.find("win32test.exe"))) + "bbackupd.conf");
	//std::string conf( "-c " + buffer.substr(0,(buffer.find("bbackupd.exe"))) + "bbackupd.conf");


	return 0;
}



