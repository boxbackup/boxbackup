#include <time.h>
#include <windows.h>

typedef int uid_t;
typedef int gid_t;
typedef int u_int32_t;

#include "emu.h"

int main(int argc, char** argv)
{
	time_t time_now = time(NULL);
	char* time_str = strdup(asctime(gmtime(&time_now)));
	time_str[24] = 0;

	printf("Time now is %d (%s)\n", time_now, time_str);

	char testfile[80];
	snprintf(testfile, sizeof(testfile), "test.%d", time_now);
	printf("Test file is: %s\n", testfile);

	_unlink(testfile);

	/*
	int fd = open(testfile, O_RDWR | O_CREAT | O_EXCL);
	if (fd < 0)
	{
		perror("open");
		exit(1);
	}
	close(fd);
	*/

	HANDLE fh = CreateFileA(testfile, FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, 
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);

	if (!fh)
	{
		fprintf(stderr, "Failed to open file '%s': error %d\n",
			testfile, GetLastError());
		exit(1);
	}

	BY_HANDLE_FILE_INFORMATION fi;

	if (!GetFileInformationByHandle(fh, &fi))
	{
		fprintf(stderr, "Failed to get file information for '%s': "
			"error %d\n", testfile, GetLastError());
		exit(1);
	}

	if (!CloseHandle(fh))
	{
		fprintf(stderr, "Failed to close file: error %d\n",
			GetLastError());
		exit(1);
	}

	time_t created_time = ConvertFileTimeToTime_t(&fi.ftCreationTime);
	time_str = strdup(asctime(gmtime(&created_time)));
	time_str[24] = 0;

	printf("File created time: %d (%s)\n", created_time, time_str);

	printf("Difference is: %d\n", created_time - time_now);

	if (abs(created_time - time_now) > 30)
	{
		fprintf(stderr, "Error: time difference too big: "
			"bug in emu.h?\n");
		exit(1);
	}

	/*
	sleep(1);

	if (_unlink(testfile) != 0)
	{
		perror("Failed to delete test file");
		exit(1);
	}
	*/

	exit(0);
}
