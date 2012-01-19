#include "Box.h"


struct passwd * getpwnam(const char * name) throw()
{
	//for the mo pretend to be root
	gTempPasswd.pw_uid = 0;
	gTempPasswd.pw_gid = 0;

	return &gTempPasswd;
}
