// win32test.cpp : Defines the entry point for the console application.
//

//#include <windows.h>
#include "Box.h"

#ifdef WIN32

#include <assert.h>
#include <AccCtrl.h>
#include <Aclapi.h>

#include "../../bin/bbackupd/BackupDaemon.h"
#include "BoxPortsAndFiles.h"
#include "emu.h"

int main(int argc, char* argv[])
{
	// ACL tests
	char* exename = getenv("WINDIR");

	PSID psidOwner;
	PSID psidGroup;
	PACL pDacl;
	PSECURITY_DESCRIPTOR pSecurityDesc;

	DWORD result = GetNamedSecurityInfo(
		exename, // pObjectName
		SE_FILE_OBJECT, // ObjectType
		DACL_SECURITY_INFORMATION | // SecurityInfo
		GROUP_SECURITY_INFORMATION |
		OWNER_SECURITY_INFORMATION,
		&psidOwner, // ppsidOwner,
		&psidGroup, // ppsidGroup,
		&pDacl,     // ppDacl,
		NULL,       // ppSacl,
		&pSecurityDesc // ppSecurityDescriptor
	);
	if (result != ERROR_SUCCESS)
	{
		printf("Error getting security info for '%s': error %d",
			exename, result);
	}
	assert(result == ERROR_SUCCESS);

	char namebuf[1024];
	char domainbuf[1024];
	SID_NAME_USE nametype;
	DWORD namelen = sizeof(namebuf);
	DWORD domainlen = sizeof(domainbuf);

	assert(LookupAccountSid(NULL, psidOwner, namebuf, &namelen,
		domainbuf, &domainlen, &nametype));

	printf("Owner:\n");
	printf("User name:   %s\n", namebuf);
	printf("Domain name: %s\n", domainbuf);
	printf("Name type:   %d\n", nametype);
	printf("\n");

	namelen = sizeof(namebuf);
	domainlen = sizeof(domainbuf);

	assert(LookupAccountSid(NULL, psidGroup, namebuf, &namelen,
		domainbuf, &domainlen, &nametype));

	printf("Group:\n");
	printf("User name:   %s\n", namebuf);
	printf("Domain name: %s\n", domainbuf);
	printf("Name type:   %d\n", nametype);
	printf("\n");

	ULONG numEntries;
	PEXPLICIT_ACCESS pEntries;
	result = GetExplicitEntriesFromAcl
	(
		pDacl,       // pAcl
		&numEntries, // pcCountOfExplicitEntries,
		&pEntries    // pListOfExplicitEntries
	);
	assert(result == ERROR_SUCCESS);

	printf("Found %lu explicit DACL entries for '%s'\n\n",
		(unsigned long)numEntries, exename);

	for (ULONG i = 0; i < numEntries; i++)
	{
		EXPLICIT_ACCESS* pEntry = &(pEntries[i]);
		printf("DACL entry %lu:\n", (unsigned long)i);

		DWORD perms = pEntry->grfAccessPermissions;
		printf("  Access permissions: ", perms);

		#define PRINT_PERM(name) \
		if (perms & name) \
		{ \
			printf(#name " "); \
			perms &= ~name; \
		}

		PRINT_PERM(FILE_ADD_FILE);
		PRINT_PERM(FILE_ADD_SUBDIRECTORY);
		PRINT_PERM(FILE_ALL_ACCESS);
		PRINT_PERM(FILE_APPEND_DATA);
		PRINT_PERM(FILE_CREATE_PIPE_INSTANCE);
		PRINT_PERM(FILE_DELETE_CHILD);
		PRINT_PERM(FILE_EXECUTE);
		PRINT_PERM(FILE_LIST_DIRECTORY);
		PRINT_PERM(FILE_READ_ATTRIBUTES);
		PRINT_PERM(FILE_READ_DATA);
		PRINT_PERM(FILE_READ_EA);
		PRINT_PERM(FILE_TRAVERSE);
		PRINT_PERM(FILE_WRITE_ATTRIBUTES);
		PRINT_PERM(FILE_WRITE_DATA);
		PRINT_PERM(FILE_WRITE_EA);
		PRINT_PERM(STANDARD_RIGHTS_READ);
		PRINT_PERM(STANDARD_RIGHTS_WRITE);
		PRINT_PERM(SYNCHRONIZE);
		PRINT_PERM(DELETE);
		PRINT_PERM(READ_CONTROL);
		PRINT_PERM(WRITE_DAC);
		PRINT_PERM(WRITE_OWNER);
		PRINT_PERM(MAXIMUM_ALLOWED);
		PRINT_PERM(GENERIC_ALL);
		PRINT_PERM(GENERIC_EXECUTE);
		PRINT_PERM(GENERIC_WRITE);
		PRINT_PERM(GENERIC_READ);
		printf("\n");

		if (perms)
		{
			printf("  Bits left over: %08x\n", perms);
		}
		assert(!perms);

		printf("  Access mode: ");
		switch(pEntry->grfAccessMode)
		{
		case NOT_USED_ACCESS:
			printf("NOT_USED_ACCESS\n"); break;
		case GRANT_ACCESS:
			printf("GRANT_ACCESS\n"); break;
		case DENY_ACCESS:
			printf("DENY_ACCESS\n"); break;
		case REVOKE_ACCESS:
			printf("REVOKE_ACCESS\n"); break;
		case SET_AUDIT_SUCCESS:
			printf("SET_AUDIT_SUCCESS\n"); break;
		case SET_AUDIT_FAILURE:
			printf("SET_AUDIT_FAILURE\n"); break;
		default:
			printf("Unknown (%08x)\n", pEntry->grfAccessMode);
		}

		printf("  Trustee: ");
		assert(pEntry->Trustee.pMultipleTrustee == NULL);
		assert(pEntry->Trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE);
		switch(pEntry->Trustee.TrusteeForm)
		{
		case TRUSTEE_IS_SID:
			{
				PSID trusteeSid = (PSID)(pEntry->Trustee.ptstrName);

				namelen = sizeof(namebuf);
				domainlen = sizeof(domainbuf);

				assert(LookupAccountSid(NULL, trusteeSid, namebuf, &namelen,
					domainbuf, &domainlen, &nametype));

				printf("SID of %s\\%s (%d)\n", domainbuf, namebuf, nametype);
			}
			break;
		case TRUSTEE_IS_NAME:
			printf("Name\n"); break;
		case TRUSTEE_BAD_FORM:
			printf("Bad form\n"); assert(0);
		case TRUSTEE_IS_OBJECTS_AND_SID:
			printf("Objects and SID\n"); break;
		case TRUSTEE_IS_OBJECTS_AND_NAME:
			printf("Objects and name\n"); break;
		default:
			printf("Unknown form\n"); assert(0);
		}

		printf("  Trustee type: ");
		switch(pEntry->Trustee.TrusteeType)
		{
		case TRUSTEE_IS_UNKNOWN:
			printf("Unknown type.\n"); break;
		case TRUSTEE_IS_USER:
			printf("User\n"); break;
		case TRUSTEE_IS_GROUP:
			printf("Group\n"); break;
		case TRUSTEE_IS_DOMAIN:
			printf("Domain\n"); break;
		case TRUSTEE_IS_ALIAS:
			printf("Alias\n"); break;
		case TRUSTEE_IS_WELL_KNOWN_GROUP:
			printf("Well-known group\n"); break;
		case TRUSTEE_IS_DELETED:
			printf("Deleted account\n"); break;
		case TRUSTEE_IS_INVALID:
			printf("Invalid trustee type\n"); break;
		case TRUSTEE_IS_COMPUTER:
			printf("Computer\n"); break;
		default:
			printf("Unknown type %d\n", pEntry->Trustee.TrusteeType); 
			assert(0);
		}

		printf("\n");
	}

	assert(LocalFree((HLOCAL)pEntries) == 0);
	assert(LocalFree((HLOCAL)pSecurityDesc) == 0);

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
			if (info) printf("File/Dir name is : %s\r\n", info->d_name);
		}
		while (info != NULL);

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
			if (info == NULL) break;
			std::string file(diry + info->d_name);
			stat(file.c_str(), &ourfs);
			if (info) printf("File/Dir name is : %s\r\n", info->d_name);
		}
		while ( info != NULL );

		closedir(ourDir);

	}

	stat("c:\\windows", &ourfs);
	stat("c:\\autoexec.bat", &ourfs);
	printf("Finished dir read\n");

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
		printf("is a normal file\n");
	}
	else
	{
		printf("is a directory?\n");
		exit(1);
	}

	lstat(getenv("WINDIR"), &ourfs);

	if ( S_ISDIR(ourfs.st_mode))
	{
		printf("is a directory\n");
	}
	else
	{
		printf("is a file?\n");
		exit(1);
	}

	//test the syslog functions
	openlog("Box Backup", 0,0);
	//the old ones are the best...
	syslog(LOG_ERR, "Hello World");
	syslog(LOG_ERR, "Value of int is: %i", 6);

	closelog();

	/*
	//first off get the path name for the default 
	char buf[MAX_PATH];
	
	GetModuleFileName(NULL, buf, sizeof(buf));
	std::string buffer(buf);
	std::string conf("-c " + buffer.substr(0,(buffer.find("win32test.exe"))) + "bbackupd.conf");
	//std::string conf( "-c " + buffer.substr(0,(buffer.find("bbackupd.exe"))) + "bbackupd.conf");
	*/

	return 0;
}

#endif // WIN32
