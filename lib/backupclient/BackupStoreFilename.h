// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFilename.h
//		Purpose: Filename for the backup store
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREFILENAME__H
#define BACKUPSTOREFILENAME__H

#include <string>

class Protocol;
class IOStream;

// #define BACKUPSTOREFILEAME_MALLOC_ALLOC_BASE_TYPE
// don't define this -- the problem of memory usage still appears without this.
// It's just that this class really showed up the problem. Instead, malloc allocation
// is globally defined in BoxPlatform.h, for troublesome libraries.

#ifdef BACKUPSTOREFILEAME_MALLOC_ALLOC_BASE_TYPE
	// Use a malloc_allocated string, because the STL default allocators really screw up with
	// memory allocation, particularly with this class.
	// Makes a few things a bit messy and inefficient with conversions.
	// Given up using this, and use global malloc allocation instead, but thought it
	// worth leaving this code in just in case it's useful for the future.
	typedef std::basic_string<char, std::string_char_traits<char>, std::malloc_alloc> BackupStoreFilename_base;
	// If this is changed, change GetClearFilename() back to returning a reference.
#else
	typedef std::string BackupStoreFilename_base;
#endif // PLATFORM_HAVE_STL_MALLOC_ALLOC

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreFilename
//		Purpose: Filename for the backup store
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
class BackupStoreFilename : public BackupStoreFilename_base
{
public:
	BackupStoreFilename();
	BackupStoreFilename(const BackupStoreFilename &rToCopy);
	virtual ~BackupStoreFilename();

	bool CheckValid(bool ExceptionIfInvalid = true) const;
	
	void ReadFromProtocol(Protocol &rProtocol);
	void WriteToProtocol(Protocol &rProtocol) const;
	
	void ReadFromStream(IOStream &rStream, int Timeout);
	void WriteToStream(IOStream &rStream) const;

	void SetAsClearFilename(const char *Clear);

	// Check that it's encrypted
	bool IsEncrypted() const;
	
	// These enumerated types belong in the base class so 
	// the CheckValid() function can make sure that the encoding
	// is a valid encoding
	enum
	{
		Encoding_Min = 1,
		Encoding_Clear = 1,
		Encoding_Blowfish = 2,
		Encoding_Max = 2
	};

protected:
	virtual void EncodedFilenameChanged();
};

// On the wire utilities for class and derived class
#define BACKUPSTOREFILENAME_GET_SIZE(hdr)		(( ((uint8_t)((hdr)[0])) | ( ((uint8_t)((hdr)[1])) << 8)) >> 2)
#define BACKUPSTOREFILENAME_GET_ENCODING(hdr)	(((hdr)[0]) & 0x3)

#define BACKUPSTOREFILENAME_MAKE_HDR(hdr, size, encoding)		{uint16_t h = (((uint16_t)size) << 2) | (encoding); ((hdr)[0]) = h & 0xff; ((hdr)[1]) = h >> 8;}

#endif // BACKUPSTOREFILENAME__H

