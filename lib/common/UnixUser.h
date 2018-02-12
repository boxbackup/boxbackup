// --------------------------------------------------------------------------
//
// File
//		Name:    UnixUser.h
//		Purpose: Interface for managing the UNIX user of the current process
//		Created: 21/1/04
//
// --------------------------------------------------------------------------

#ifndef UNIXUSER__H
#define UNIXUSER__H

#include <string>

class UnixUser
{
public:
	UnixUser(const std::string& Username);
	UnixUser(uid_t UID, gid_t GID);
	~UnixUser();
private:
	// no copying allowed
	UnixUser(const UnixUser &);
	UnixUser &operator=(const UnixUser &);
public:

	void ChangeProcessUser(bool Temporary = false);

	uid_t GetUID() {return mUID;}
	gid_t GetGID() {return mGID;}

private:
	uid_t mUID;
	gid_t mGID;
	bool mRevertOnDestruction;
};

#endif // UNIXUSER__H

