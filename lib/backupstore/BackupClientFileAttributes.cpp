// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientFileAttributes.cpp
//		Purpose: Storage of file attributes
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

#ifdef HAVE_SYS_XATTR_H
#include <cerrno>
#include <sys/xattr.h>
#endif

#include <cstring>

#include "BackupClientFileAttributes.h"
#include "CommonException.h"
#include "FileModificationTime.h"
#include "BoxTimeToUnix.h"
#include "BackupStoreException.h"
#include "CipherContext.h"
#include "CipherBlowfish.h"
#include "MD5Digest.h"

#include "MemLeakFindOn.h"

// set packing to one byte
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "BeginStructPackForWire.h"
#else
BEGIN_STRUCTURE_PACKING_FOR_WIRE
#endif

#define ATTRIBUTETYPE_GENERIC_UNIX	1

#define ATTRIBUTE_ENCODING_BLOWFISH	2

typedef struct 
{
	int32_t		AttributeType;
	uint32_t	UID;
	uint32_t	GID;
	uint64_t	ModificationTime;
	uint64_t	AttrModificationTime;
	uint32_t	UserDefinedFlags;
	uint32_t	FileGenerationNumber;
	uint16_t	Mode;
	// Symbolic link filename may follow
	// Extended attribute (xattr) information may follow, format is:
	//   uint32_t     Size of extended attribute block (excluding this word)
	// For each of NumberOfAttributes (sorted by AttributeName):
	//   uint16_t     AttributeNameLength
	//   char          AttributeName[AttributeNameLength]
	//   uint32_t     AttributeValueLength
	//   unsigned char AttributeValue[AttributeValueLength]
	// AttributeName is 0 terminated, AttributeValue is not (and may be binary data)
} attr_StreamFormat;

// This has wire packing so it's compatible across platforms
// Use wider than necessary sizes, just to be careful.
typedef struct
{
	int32_t uid, gid, mode;
	#ifdef WIN32
	int64_t fileCreationTime;
	#endif
} attributeHashData;

// Use default packing
#ifdef STRUCTURE_PACKING_FOR_WIRE_USE_HEADERS
#include "EndStructPackForWire.h"
#else
END_STRUCTURE_PACKING_FOR_WIRE
#endif


#define MAX_ATTRIBUTE_HASH_SECRET_LENGTH	256

// Hide private static variables from the rest of the world
// -- don't put them as static class variables to avoid openssl/evp.h being
// included all over the project.
namespace
{
	CipherContext sBlowfishEncrypt;
	CipherContext sBlowfishDecrypt;
	uint8_t sAttributeHashSecret[MAX_ATTRIBUTE_HASH_SECRET_LENGTH];
	int sAttributeHashSecretLength = 0;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::BackupClientFileAttributes()
//		Purpose: Default constructor
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
BackupClientFileAttributes::BackupClientFileAttributes()
	: mpClearAttributes(0)
{
	ASSERT(sizeof(uint64_t) == sizeof(box_time_t));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::BackupClientFileAttributes()
//		Purpose: Artifical constructor
//		Created: 2011/12/06
//
// --------------------------------------------------------------------------
BackupClientFileAttributes::BackupClientFileAttributes(const EMU_STRUCT_STAT &st)
: mpClearAttributes(0)
{
	ASSERT(sizeof(uint64_t) == sizeof(box_time_t));
	StreamableMemBlock *pnewAttr = new StreamableMemBlock;
	FillAttributes(*pnewAttr, (const char *)NULL, st, true);

	// Attributes ready. Encrypt into this block
	EncryptAttr(*pnewAttr);
	
	// Store the new attributes
	RemoveClear();
	mpClearAttributes = pnewAttr;
	pnewAttr = 0;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::BackupClientFileAttributes(const BackupClientFileAttributes &)
//		Purpose: Copy constructor
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
BackupClientFileAttributes::BackupClientFileAttributes(const BackupClientFileAttributes &rToCopy)
	: StreamableMemBlock(rToCopy), // base class does the hard work
	  mpClearAttributes(0)
{
}
BackupClientFileAttributes::BackupClientFileAttributes(const StreamableMemBlock &rToCopy)
	: StreamableMemBlock(rToCopy), // base class does the hard work
	  mpClearAttributes(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::~BackupClientFileAttributes()
//		Purpose: Destructor
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
BackupClientFileAttributes::~BackupClientFileAttributes()
{
	if(mpClearAttributes)
	{
		delete mpClearAttributes;
		mpClearAttributes = 0;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes &operator=(const BackupClientFileAttributes &)
//		Purpose: Assignment operator
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
BackupClientFileAttributes &BackupClientFileAttributes::operator=(const BackupClientFileAttributes &rAttr)
{
	StreamableMemBlock::Set(rAttr);
	RemoveClear();	// make sure no decrypted version held
	return *this;
}
// Assume users play nice
BackupClientFileAttributes &BackupClientFileAttributes::operator=(const StreamableMemBlock &rAttr)
{
	StreamableMemBlock::Set(rAttr);
	RemoveClear();	// make sure no decrypted version held
	return *this;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::operator==(const BackupClientFileAttributes &)
//		Purpose: Comparison operator
//		Created: 2003/10/09
//
// --------------------------------------------------------------------------
bool BackupClientFileAttributes::operator==(const BackupClientFileAttributes &rAttr) const
{
	EnsureClearAvailable();
	rAttr.EnsureClearAvailable();

	return mpClearAttributes->operator==(*rAttr.mpClearAttributes);
}
// Too dangerous to allow -- put the two names the wrong way round, and it compares encrypted data.
/*bool BackupClientFileAttributes::operator==(const StreamableMemBlock &rAttr) const
{
	StreamableMemBlock *pDecoded = 0;

	try
	{
		EnsureClearAvailable();
		StreamableMemBlock *pDecoded = MakeClear(rAttr);

		// Compare using clear version
		bool compared = mpClearAttributes->operator==(rAttr);
		
		// Delete temporary
		delete pDecoded;
	
		return compared;
	}
	catch(...)
	{
		delete pDecoded;
		throw;
	}
}*/


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::Compare(const BackupClientFileAttributes &, bool)
//		Purpose: Compare, optionally ignoring the attribute
//			 modification time and/or modification time, and some
//			 data which is irrelevant in practise (eg file
//			 generation number)
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
bool BackupClientFileAttributes::Compare(const BackupClientFileAttributes &rAttr,
	bool IgnoreAttrModTime, bool IgnoreModTime) const
{
	EnsureClearAvailable();
	rAttr.EnsureClearAvailable();

	// Check sizes are the same, as a first check
	if(mpClearAttributes->GetSize() != rAttr.mpClearAttributes->GetSize())
	{
		BOX_TRACE("Attribute Compare: Attributes objects are "
			"different sizes, cannot compare them: local " <<
			mpClearAttributes->GetSize() << " bytes, remote " <<
			rAttr.mpClearAttributes->GetSize() << " bytes");
		return false;
	}
	
	// Then check the elements of the two things
	// Bytes are checked in network order, but this doesn't matter as we're only checking for equality.
	attr_StreamFormat *a1 = (attr_StreamFormat*)mpClearAttributes->GetBuffer();
	attr_StreamFormat *a2 = (attr_StreamFormat*)rAttr.mpClearAttributes->GetBuffer();

	#define COMPARE(attribute, message) \
	if (a1->attribute != a2->attribute) \
	{ \
		BOX_TRACE("Attribute Compare: " << message << " differ: " \
			"local "  << ntoh(a1->attribute) << ", " \
			"remote " << ntoh(a2->attribute)); \
		return false; \
	}
	COMPARE(AttributeType, "Attribute types");
	COMPARE(UID, "UIDs");
	COMPARE(GID, "GIDs");
	COMPARE(UserDefinedFlags, "User-defined flags");
	COMPARE(Mode, "Modes");

	if(!IgnoreModTime)
	{
		uint64_t t1 = box_ntoh64(a1->ModificationTime);
		uint64_t t2 = box_ntoh64(a2->ModificationTime);
		time_t s1 = BoxTimeToSeconds(t1);
		time_t s2 = BoxTimeToSeconds(t2);
		if(s1 != s2)
		{
			BOX_TRACE("Attribute Compare: File modification "
				"times differ: local " <<
				FormatTime(t1, true) << " (" << s1 << "), "
				"remote " <<
				FormatTime(t2, true) << " (" << s2 << ")");
			return false;
		}
	}
	
	if(!IgnoreAttrModTime)
	{
		uint64_t t1 = box_ntoh64(a1->AttrModificationTime);
		uint64_t t2 = box_ntoh64(a2->AttrModificationTime);
		time_t s1 = BoxTimeToSeconds(t1);
		time_t s2 = BoxTimeToSeconds(t2);
		if(s1 != s2)
		{
			BOX_TRACE("Attribute Compare: Attribute modification "
				"times differ: local " <<
				FormatTime(t1, true) << " (" << s1 << "), "
				"remote " <<
				FormatTime(t2, true) << " (" << s2 << ")");
			return false;
		}
	}
	
	// Check symlink string?
	unsigned int size = mpClearAttributes->GetSize();
	if(size > sizeof(attr_StreamFormat))
	{
		// Symlink strings don't match. This also compares xattrs
		int datalen = size - sizeof(attr_StreamFormat);

		if(::memcmp(a1 + 1, a2 + 1, datalen) != 0)
		{
			std::string s1((char *)(a1 + 1), datalen);
			std::string s2((char *)(a2 + 1), datalen);
			BOX_TRACE("Attribute Compare: Symbolic link target "
				"or extended attributes differ: "
				"local "  << PrintEscapedBinaryData(s1) << ", "
				"remote " << PrintEscapedBinaryData(s2));
			return false;
		}
	}
	
	// Passes all test, must be OK
	return true;
}




// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::ReadAttributes(
//			 const char *Filename, bool ZeroModificationTimes,
//			 box_time_t *pModTime, box_time_t *pAttrModTime,
//			 int64_t *pFileSize, InodeRefType *pInodeNumber,
//			 bool *pHasMultipleLinks)
//		Purpose: Read the attributes of the file, and store them
//			 ready for streaming. Optionally retrieve the
//			 modification time and attribute modification time.
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::ReadAttributes(const std::string& Filename,
	bool ZeroModificationTimes, box_time_t *pModTime,
	box_time_t *pAttrModTime, int64_t *pFileSize,
	InodeRefType *pInodeNumber, bool *pHasMultipleLinks)
{
	StreamableMemBlock *pnewAttr = 0;
	try
	{
		EMU_STRUCT_STAT st;
		if(EMU_LSTAT(Filename.c_str(), &st) != 0)
		{
			THROW_SYS_FILE_ERROR("Failed to stat file",
				Filename, CommonException, OSFileError)
		}
		
		// Modification times etc
		if(pModTime) {*pModTime = FileModificationTime(st);}
		if(pAttrModTime) {*pAttrModTime = FileAttrModificationTime(st);}
		if(pFileSize) {*pFileSize = st.st_size;}
		if(pInodeNumber) {*pInodeNumber = st.st_ino;}
		if(pHasMultipleLinks) {*pHasMultipleLinks = (st.st_nlink > 1);}

		pnewAttr = new StreamableMemBlock;

		FillAttributes(*pnewAttr, Filename, st, ZeroModificationTimes);

#ifndef WIN32
		// Is it a link?
		if((st.st_mode & S_IFMT) == S_IFLNK)
		{
			FillAttributesLink(*pnewAttr, Filename, st);
		}
#endif

		FillExtendedAttr(*pnewAttr, Filename);

#ifdef WIN32
		//this is to catch those problems with invalid time stamps stored...
		//need to find out the reason why - but also a catch as well.

		attr_StreamFormat *pattr = 
			(attr_StreamFormat*)pnewAttr->GetBuffer();
		ASSERT(pattr != 0);
		
		// __time64_t winTime = BoxTimeToSeconds(
		// pnewAttr->ModificationTime);

		uint64_t  modTime = box_ntoh64(pattr->ModificationTime);
		box_time_t modSecs = BoxTimeToSeconds(modTime);
		__time64_t winTime = modSecs;

		// _MAX__TIME64_T doesn't seem to be defined, but the code below
		// will throw an assertion failure if we exceed it :-)
		// Microsoft says dates up to the year 3000 are valid, which
		// is a bit more than 15 * 2^32. Even that doesn't seem
		// to be true (still aborts), but it can at least hold 2^32.
		if (winTime >= 0x100000000LL || _gmtime64(&winTime) == 0)
		{
			BOX_ERROR("Invalid Modification Time caught for "
				"file: '" << Filename << "'");
			pattr->ModificationTime = 0;
		}

		modTime = box_ntoh64(pattr->AttrModificationTime);
		modSecs = BoxTimeToSeconds(modTime);
		winTime = modSecs;

		if (winTime > 0x100000000LL || _gmtime64(&winTime) == 0)
		{
			BOX_ERROR("Invalid Attribute Modification Time " 
				"caught for file: '" << Filename << "'");
			pattr->AttrModificationTime = 0;
		}
#endif

		// Attributes ready. Encrypt into this block
		EncryptAttr(*pnewAttr);
		
		// Store the new attributes
		RemoveClear();
		mpClearAttributes = pnewAttr;
		pnewAttr = 0;
	}
	catch(...)
	{
		// clean up
		delete pnewAttr;
		pnewAttr = 0;
		throw;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::FillAttributes()
//		Purpose: Private function, handles standard attributes for all objects
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::FillAttributes(
	StreamableMemBlock &outputBlock, const std::string& rFilename,
	const EMU_STRUCT_STAT &st, bool ZeroModificationTimes
)
{
	outputBlock.ResizeBlock(sizeof(attr_StreamFormat));
	attr_StreamFormat *pattr = (attr_StreamFormat*)outputBlock.GetBuffer();
	ASSERT(pattr != 0);

	// Fill in the entries
	pattr->AttributeType = htonl(ATTRIBUTETYPE_GENERIC_UNIX);
	pattr->UID = htonl(st.st_uid);
	pattr->GID = htonl(st.st_gid);
	if(ZeroModificationTimes)
	{
		pattr->ModificationTime = 0;
		pattr->AttrModificationTime = 0;
	}
	else
	{
		pattr->ModificationTime = box_hton64(FileModificationTime(st));
		pattr->AttrModificationTime = box_hton64(FileAttrModificationTime(st));
	}
	pattr->Mode = htons(st.st_mode);

#ifndef HAVE_STRUCT_STAT_ST_FLAGS
	pattr->UserDefinedFlags = 0;
	pattr->FileGenerationNumber = 0;
#else
	pattr->UserDefinedFlags = htonl(st.st_flags);
	pattr->FileGenerationNumber = htonl(st.st_gen);
#endif
}
#ifndef WIN32
// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::FillAttributesLink(
//			 StreamableMemBlock &outputBlock,
//			 const char *Filename, struct stat &st)
//		Purpose: Private function, handles the case where a symbolic link is needed
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::FillAttributesLink(
	StreamableMemBlock &outputBlock, const std::string& Filename,
	struct stat &st)
{
	// Make sure we're only called for symbolic links
	ASSERT((st.st_mode & S_IFMT) == S_IFLNK);

	// Get the filename the link is linked to
	char linkedTo[PATH_MAX+4];
	int linkedToSize = ::readlink(Filename.c_str(), linkedTo, PATH_MAX);
	if(linkedToSize == -1)
	{
		BOX_LOG_SYS_ERROR("Failed to readlink '" << Filename << "'");
		THROW_EXCEPTION(CommonException, OSFileError);
	}

	int oldSize = outputBlock.GetSize();
	outputBlock.ResizeBlock(oldSize+linkedToSize+1);
	char* buffer = static_cast<char*>(outputBlock.GetBuffer());

	// Add the path name for the symbolic link, and add 0 termination
	std::memcpy(buffer+oldSize, linkedTo, linkedToSize);
	buffer[oldSize+linkedToSize] = '\0';
}
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::FillExtendedAttr(const char *, unsigned char**)
//		Purpose: Private function, read the extended attributes of the file into the block
//		Created: 2005/06/12
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::FillExtendedAttr(StreamableMemBlock &outputBlock,
	const std::string& Filename)
{
#if defined HAVE_LLISTXATTR && defined HAVE_LGETXATTR
	int listBufferSize = 10000;
	char* list = new char[listBufferSize];

	try
	{
		// This returns an unordered list of attribute names, each 0 terminated,
		// concatenated together
		int listSize = ::llistxattr(Filename.c_str(), list, listBufferSize);

		if(listSize>listBufferSize)
		{
			delete[] list, list = NULL;
			list = new char[listSize];
			listSize = ::llistxattr(Filename.c_str(), list, listSize);
		}

		if(listSize>0)
		{
			// Extract list of attribute names so we can sort them
			std::vector<std::string> attrKeys;
			for(int i = 0; i<listSize; ++i)
			{
				std::string attrKey(list+i);
				i += attrKey.size();
				attrKeys.push_back(attrKey);
			}
			sort(attrKeys.begin(), attrKeys.end());

			// Make initial space in block
			int xattrSize = outputBlock.GetSize();
			int xattrBufferSize = (xattrSize+listSize)>500 ? (xattrSize+listSize)*2 : 1000;
			outputBlock.ResizeBlock(xattrBufferSize);
			unsigned char* buffer = static_cast<unsigned char*>(outputBlock.GetBuffer());

			// Leave space for attr block size later
			int xattrBlockSizeOffset = xattrSize;
			xattrSize += sizeof(uint32_t);

			// Loop for each attribute
			for(std::vector<std::string>::const_iterator attrKeyI = attrKeys.begin(); attrKeyI!=attrKeys.end(); ++attrKeyI)
			{
				std::string attrKey(*attrKeyI);

				if(xattrSize+sizeof(uint16_t)+attrKey.size()+1+sizeof(uint32_t)>static_cast<unsigned int>(xattrBufferSize))
				{
					xattrBufferSize = (xattrBufferSize+sizeof(uint16_t)+attrKey.size()+1+sizeof(uint32_t))*2;
					outputBlock.ResizeBlock(xattrBufferSize);
					buffer = static_cast<unsigned char*>(outputBlock.GetBuffer());
				}

				// Store length and text for attibute name
				uint16_t keyLength = htons(attrKey.size()+1);
				std::memcpy(buffer+xattrSize, &keyLength, sizeof(uint16_t));
				xattrSize += sizeof(uint16_t);
				std::memcpy(buffer+xattrSize, attrKey.c_str(), attrKey.size()+1);
				xattrSize += attrKey.size()+1;

				// Leave space for value size
				int valueSizeOffset = xattrSize;
				xattrSize += sizeof(uint32_t);

				// Find size of attribute (must call with buffer and length 0 on some platforms,
				// as -1 is returned if the data doesn't fit.)
				int valueSize = ::lgetxattr(Filename.c_str(), attrKey.c_str(), 0, 0);
				if(valueSize<0)
				{
					BOX_LOG_SYS_ERROR("Failed to get "
						"extended attribute size of "
						"'" << Filename << "': " <<
						attrKey);
					THROW_EXCEPTION(CommonException, OSFileError);
				}

				// Resize block, if needed
				if(xattrSize+valueSize>xattrBufferSize)
				{
					xattrBufferSize = (xattrBufferSize+valueSize)*2;
					outputBlock.ResizeBlock(xattrBufferSize);
					buffer = static_cast<unsigned char*>(outputBlock.GetBuffer());
				}

				// This gets the attribute value (may be text or binary), no termination
				valueSize = ::lgetxattr(Filename.c_str(),
					attrKey.c_str(), buffer+xattrSize,
					xattrBufferSize-xattrSize);
				if(valueSize<0)
				{
					BOX_LOG_SYS_ERROR("Failed to get "
						"extended attribute of " 
						"'" << Filename << "': " <<
						attrKey);
					THROW_EXCEPTION(CommonException, OSFileError);
				}
				xattrSize += valueSize;

				// Fill in value size
				uint32_t valueLength = htonl(valueSize);
				std::memcpy(buffer+valueSizeOffset, &valueLength, sizeof(uint32_t));
			}

			// Fill in attribute block size
			uint32_t xattrBlockLength = htonl(xattrSize-xattrBlockSizeOffset-sizeof(uint32_t));
			std::memcpy(buffer+xattrBlockSizeOffset, &xattrBlockLength, sizeof(uint32_t));

			outputBlock.ResizeBlock(xattrSize);
		}
		else if(listSize<0)
		{
			if(errno == EOPNOTSUPP || errno == EACCES
#if HAVE_DECL_ENOTSUP
				// NetBSD uses ENOTSUP instead
				// https://mail-index.netbsd.org/tech-kern/2011/12/13/msg012185.html
				|| errno == ENOTSUP
#endif
			)
			{
				// Not supported by OS, or not on this filesystem
				BOX_TRACE(BOX_SYS_ERRNO_MESSAGE(errno,
					BOX_FILE_MESSAGE(Filename, "Failed to "
						"list extended attributes")));
			}
			else if(errno == ERANGE)
			{
				BOX_ERROR("Failed to list extended "
					"attributes of '" << Filename << "': "
					"buffer too small, not backed up");
			}
			else if(errno == ENOENT)
			{
				BOX_ERROR("Failed to list extended "
					"attributes of '" << Filename << "': "
					"file no longer exists");
			}
			else
			{
				THROW_SYS_FILE_ERROR("Failed to list extended "
					"attributes for unknown reason", Filename,
					CommonException, OSFileError);
			}
		}
	}
	catch(...)
	{
		delete[] list;
		throw;
	}
	delete[] list;
#endif // defined HAVE_LLISTXATTR && defined HAVE_LGETXATTR
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::GetModificationTimes()
//		Purpose: Returns the modification time embedded in the
//			 attributes.
//		Created: 2010/02/24
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::GetModificationTimes(
	box_time_t *pModificationTime,
	box_time_t *pAttrModificationTime) const
{
	// Got something loaded
	if(GetSize() <= 0)
	{
		THROW_EXCEPTION(BackupStoreException, AttributesNotLoaded);
	}
	
	// Make sure there are clear attributes to use
	EnsureClearAvailable();
	ASSERT(mpClearAttributes != 0);

	// Check if the decrypted attributes are small enough, and the type of attributes stored
	if(mpClearAttributes->GetSize() < (int)sizeof(int32_t))
	{
		THROW_EXCEPTION(BackupStoreException, AttributesNotUnderstood);
	}
	int32_t *type = (int32_t*)mpClearAttributes->GetBuffer();
	ASSERT(type != 0);
	if(ntohl(*type) != ATTRIBUTETYPE_GENERIC_UNIX)
	{
		// Don't know what to do with these
		THROW_EXCEPTION(BackupStoreException, AttributesNotUnderstood);
	}
	
	// Check there is enough space for an attributes block
	if(mpClearAttributes->GetSize() < (int)sizeof(attr_StreamFormat))
	{
		// Too small
		THROW_EXCEPTION(BackupStoreException, AttributesNotLoaded);
	}

	// Get pointer to structure
	attr_StreamFormat *pattr = (attr_StreamFormat*)mpClearAttributes->GetBuffer();

	if(pModificationTime)
	{
		*pModificationTime = box_ntoh64(pattr->ModificationTime);
	}
	
	if(pAttrModificationTime)
	{
		*pAttrModificationTime = box_ntoh64(pattr->AttrModificationTime);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::WriteAttributes(const char *)
//		Purpose: Apply the stored attributes to the file
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::WriteAttributes(const std::string& Filename,
	bool MakeUserWritable) const
{
	// Got something loaded
	if(GetSize() <= 0)
	{
		THROW_EXCEPTION(BackupStoreException, AttributesNotLoaded);
	}
	
	// Make sure there are clear attributes to use
	EnsureClearAvailable();
	ASSERT(mpClearAttributes != 0);

	// Check if the decrypted attributes are small enough, and the type of attributes stored
	if(mpClearAttributes->GetSize() < (int)sizeof(int32_t))
	{
		THROW_EXCEPTION(BackupStoreException, AttributesNotUnderstood);
	}
	int32_t *type = (int32_t*)mpClearAttributes->GetBuffer();
	ASSERT(type != 0);
	if(ntohl(*type) != ATTRIBUTETYPE_GENERIC_UNIX)
	{
		// Don't know what to do with these
		THROW_EXCEPTION(BackupStoreException, AttributesNotUnderstood);
	}
	
	// Check there is enough space for an attributes block
	if(mpClearAttributes->GetSize() < (int)sizeof(attr_StreamFormat))
	{
		// Too small
		THROW_EXCEPTION(BackupStoreException, AttributesNotLoaded);
	}

	// Get pointer to structure
	attr_StreamFormat *pattr = (attr_StreamFormat*)mpClearAttributes->GetBuffer();
	int xattrOffset = sizeof(attr_StreamFormat);

	// is it a symlink?
	int16_t mode = ntohs(pattr->Mode);
	if((mode & S_IFMT) == S_IFLNK)
	{
		// Check things are sensible
		if(mpClearAttributes->GetSize() < (int)sizeof(attr_StreamFormat) + 1)
		{
			// Too small
			THROW_EXCEPTION(BackupStoreException, AttributesNotLoaded);
		}
	
#ifdef WIN32
		BOX_WARNING("Cannot create symbolic links on Windows: '" <<
			Filename << "'");
#else
		// Make a symlink, first deleting anything in the way
		EMU_UNLINK(Filename.c_str());
		if(::symlink((char*)(pattr + 1), Filename.c_str()) != 0)
		{
			BOX_LOG_SYS_ERROR("Failed to symlink '" << Filename <<
				"' to '" << (char*)(pattr + 1) << "'");
			THROW_EXCEPTION(CommonException, OSFileError)
		}
#endif

		xattrOffset += std::strlen(reinterpret_cast<char*>(pattr+1))+1;
	}
	
	// If working as root, set user IDs
	if(::geteuid() == 0)
	{
		#ifndef HAVE_LCHOWN
			// only if not a link, can't set their owner on this platform
			if((mode & S_IFMT) != S_IFLNK)
			{
				// Not a link, use normal chown
				if(::chown(Filename.c_str(), ntohl(pattr->UID), ntohl(pattr->GID)) != 0)
				{
					BOX_LOG_SYS_ERROR("Failed to change "
						"owner of file "
						"'" << Filename << "'");
					THROW_EXCEPTION(CommonException, OSFileError)
				}
			}
		#else
			// use the version which sets things on symlinks
			if(::lchown(Filename.c_str(), ntohl(pattr->UID), ntohl(pattr->GID)) != 0)
			{
				BOX_LOG_SYS_ERROR("Failed to change owner of "
					"symbolic link '" << Filename << "'");
				THROW_EXCEPTION(CommonException, OSFileError)
			}
		#endif
	}

	if(static_cast<int>(xattrOffset+sizeof(uint32_t))<=mpClearAttributes->GetSize())
	{
		WriteExtendedAttr(Filename, xattrOffset);
	}

	// Stop now if symlink, because otherwise it'll just be applied to the target
	if((mode & S_IFMT) == S_IFLNK)
	{
		return;
	}

	// Set modification time?
	box_time_t modtime = box_ntoh64(pattr->ModificationTime);
	if(modtime != 0)
	{
		// Work out times as timevals
		struct timeval times[2];

		#ifdef WIN32
		BoxTimeToTimeval(box_ntoh64(pattr->ModificationTime), 
			times[1]);
		BoxTimeToTimeval(box_ntoh64(pattr->AttrModificationTime), 
			times[0]);
		// Because stat() returns the creation time in the ctime
		// field under Windows, and this gets saved in the 
		// AttrModificationTime field of the serialised attributes,
		// we subvert the first parameter of emu_utimes() to allow
		// it to be reset to the right value on the restored file.
		#else
		BoxTimeToTimeval(modtime, times[1]);
		// Copy access time as well, why not, got to set it to something
		times[0] = times[1];
		// Attr modification time will be changed anyway, 
		// nothing that can be done about it
		#endif
		
		// Try to apply
		if(::utimes(Filename.c_str(), times) != 0)
		{
			BOX_LOG_SYS_WARNING("Failed to change times of "
				"file '" << Filename << "' to ctime=" <<
				BOX_FORMAT_TIMESPEC(times[0]) << ", mtime=" << 
				BOX_FORMAT_TIMESPEC(times[1]));
		}
	}

	if (MakeUserWritable)
	{
		mode |= S_IRWXU;
	}

	// Apply everything else... (allowable mode flags only)
	// Mode must be done last (think setuid)
	if(::chmod(Filename.c_str(), mode & (S_IRWXU | S_IRWXG | S_IRWXO |
		S_ISUID | S_ISGID | S_ISVTX)) != 0)
	{
		BOX_LOG_SYS_ERROR("Failed to change permissions of file "
			"'" << Filename << "'");
		THROW_EXCEPTION(CommonException, OSFileError)
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::IsSymLink()
//		Purpose: Do these attributes represent a symbolic link?
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------
bool BackupClientFileAttributes::IsSymLink() const
{
	EnsureClearAvailable();

	// Got the right kind of thing?
	if(mpClearAttributes->GetSize() < (int)sizeof(int32_t))
	{
		THROW_EXCEPTION(BackupStoreException, AttributesNotLoaded);
	}
	
	// Get the type of attributes stored
	int32_t *type = (int32_t*)mpClearAttributes->GetBuffer();
	ASSERT(type != 0);
	if(ntohl(*type) == ATTRIBUTETYPE_GENERIC_UNIX && mpClearAttributes->GetSize() > (int)sizeof(attr_StreamFormat))
	{
		// Check link
		attr_StreamFormat *pattr = (attr_StreamFormat*)mpClearAttributes->GetBuffer();
		return ((ntohs(pattr->Mode)) & S_IFMT) == S_IFLNK;
	}
	
	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::RemoveClear()
//		Purpose: Private. Deletes any clear version of the attributes that may be held
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::RemoveClear() const
{
	if(mpClearAttributes)
	{
		delete mpClearAttributes;
	}
	mpClearAttributes = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::EnsureClearAvailable()
//		Purpose: Private. Makes sure the clear version is available
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::EnsureClearAvailable() const
{
	if(mpClearAttributes == 0)
	{
		mpClearAttributes = MakeClear(*this);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::WriteExtendedAttr(const char *Filename, int xattrOffset)
//		Purpose: Private function, apply the stored extended attributes to the file
//		Created: 2005/06/13
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::WriteExtendedAttr(const std::string& Filename, int xattrOffset) const
{
#if defined HAVE_LSETXATTR
	const char* buffer = static_cast<char*>(mpClearAttributes->GetBuffer());

	uint32_t xattrBlockLength = 0;
	std::memcpy(&xattrBlockLength, buffer+xattrOffset, sizeof(uint32_t));
	int xattrBlockSize = ntohl(xattrBlockLength);
	xattrOffset += sizeof(uint32_t);

	int xattrEnd = xattrOffset+xattrBlockSize;
	if(xattrEnd>mpClearAttributes->GetSize())
	{
		// Too small
		THROW_EXCEPTION(BackupStoreException, AttributesNotLoaded);
	}

	while(xattrOffset<xattrEnd)
	{
		uint16_t keyLength = 0;
		std::memcpy(&keyLength, buffer+xattrOffset, sizeof(uint16_t));
		int keySize = ntohs(keyLength);
		xattrOffset += sizeof(uint16_t);

		const char* key = buffer+xattrOffset;
		xattrOffset += keySize;

		uint32_t valueLength = 0;
		std::memcpy(&valueLength, buffer+xattrOffset, sizeof(uint32_t));
		int valueSize = ntohl(valueLength);
		xattrOffset += sizeof(uint32_t);

		// FIXME: Warn on EOPNOTSUPP
		if(::lsetxattr(Filename.c_str(), key, buffer+xattrOffset,
			valueSize, 0)!=0 && errno!=EOPNOTSUPP)
		{
			BOX_LOG_SYS_ERROR("Failed to set extended attributes "
				"on file '" << Filename << "'");
			THROW_EXCEPTION(CommonException, OSFileError);
		}

		xattrOffset += valueSize;
	}

	ASSERT(xattrOffset==xattrEnd);
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::MakeClear(const StreamableMemBlock &)
//		Purpose: Static. Decrypts stored attributes.
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
StreamableMemBlock *BackupClientFileAttributes::MakeClear(const StreamableMemBlock &rEncrypted)
{
	// New block
	StreamableMemBlock *pdecrypted = 0;

	try
	{
		// Check the block is big enough for IV and header
		int ivSize = sBlowfishEncrypt.GetIVLength();
		if(rEncrypted.GetSize() <= (ivSize + 1))
		{
			THROW_EXCEPTION(BackupStoreException, BadEncryptedAttributes);
		}
		
		// How much space is needed for the output?
		int maxDecryptedSize = sBlowfishDecrypt.MaxOutSizeForInBufferSize(rEncrypted.GetSize() - ivSize);
		
		// Allocate it
		pdecrypted = new StreamableMemBlock(maxDecryptedSize);
	
		// ptr to block	
		uint8_t *encBlock = (uint8_t*)rEncrypted.GetBuffer();

		// Check that the header has right type
		if(encBlock[0] != ATTRIBUTE_ENCODING_BLOWFISH)
		{
			THROW_EXCEPTION(BackupStoreException, EncryptedAttributesHaveUnknownEncoding);
		}

		// Set IV
		sBlowfishDecrypt.SetIV(encBlock + 1);
		
		// Decrypt
		int decryptedSize = sBlowfishDecrypt.TransformBlock(pdecrypted->GetBuffer(), maxDecryptedSize, encBlock + 1 + ivSize, rEncrypted.GetSize() - (ivSize + 1));

		// Resize block to fit
		pdecrypted->ResizeBlock(decryptedSize);
	}
	catch(...)
	{
		delete pdecrypted;
		pdecrypted = 0;
	}

	return pdecrypted;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::SetBlowfishKey(const void *, int)
//		Purpose: Static. Sets the key to use for encryption and decryption.
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::SetBlowfishKey(const void *pKey, int KeyLength)
{
	// IVs set later
	sBlowfishEncrypt.Reset();
	sBlowfishEncrypt.Init(CipherContext::Encrypt, CipherBlowfish(CipherDescription::Mode_CBC, pKey, KeyLength));
	sBlowfishDecrypt.Reset();
	sBlowfishDecrypt.Init(CipherContext::Decrypt, CipherBlowfish(CipherDescription::Mode_CBC, pKey, KeyLength));
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::EncryptAttr(const StreamableMemBlock &)
//		Purpose: Private. Encrypt the given attributes into this block. 
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::EncryptAttr(const StreamableMemBlock &rToEncrypt)
{
	// Free any existing block
	FreeBlock();
	
	// Work out the maximum amount of space we need
	int maxEncryptedSize = sBlowfishEncrypt.MaxOutSizeForInBufferSize(rToEncrypt.GetSize());
	// And the size of the IV
	int ivSize = sBlowfishEncrypt.GetIVLength();
	
	// Allocate this space
	AllocateBlock(maxEncryptedSize + ivSize + 1);
	
	// Store the encoding byte
	uint8_t *block = (uint8_t*)GetBuffer();
	block[0] = ATTRIBUTE_ENCODING_BLOWFISH;
	
	// Generate and store an IV for this attribute block
	int ivSize2 = 0;
	const void *iv = sBlowfishEncrypt.SetRandomIV(ivSize2);
	ASSERT(ivSize == ivSize2);
	
	// Copy into the encrypted block
	::memcpy(block + 1, iv, ivSize);
	
	// Do the transform
	int encrytedSize = sBlowfishEncrypt.TransformBlock(block + 1 + ivSize, maxEncryptedSize, rToEncrypt.GetBuffer(), rToEncrypt.GetSize());

	// Resize this block
	ResizeBlock(encrytedSize + ivSize + 1);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::SetAttributeHashSecret(const void *, int)
//		Purpose: Set the secret for the filename attribute hash
//		Created: 25/4/04
//
// --------------------------------------------------------------------------
void BackupClientFileAttributes::SetAttributeHashSecret(const void *pSecret, int SecretLength)
{
	if(SecretLength > (int)sizeof(sAttributeHashSecret))
	{
		SecretLength = sizeof(sAttributeHashSecret);
	}
	if(SecretLength < 0)
	{
		THROW_EXCEPTION(BackupStoreException, Internal)
	}
	
	// Copy
	::memcpy(sAttributeHashSecret, pSecret, SecretLength);
	sAttributeHashSecretLength = SecretLength;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientFileAttributes::GenerateAttributeHash(
//			 struct stat &, const std::string &,
//			 const std::string &)
//		Purpose: Generate a 64 bit hash from the attributes, used to
//			 detect changes. Include filename in the hash, so
//			 that it changes from one file to another, so don't
//			 reveal identical attributes.
//		Created: 25/4/04
//
// --------------------------------------------------------------------------
uint64_t BackupClientFileAttributes::GenerateAttributeHash(EMU_STRUCT_STAT &st,
	const std::string &filename, const std::string &leafname)
{
	if(sAttributeHashSecretLength == 0)
	{
		THROW_EXCEPTION(BackupStoreException, AttributeHashSecretNotSet)
	}
	
	// Assemble stuff we're interested in
	attributeHashData hashData;
	memset(&hashData, 0, sizeof(hashData));
	// Use network byte order and large sizes to be cross platform
	hashData.uid = htonl(st.st_uid);
	hashData.gid = htonl(st.st_gid);
	hashData.mode = htonl(st.st_mode);

	#ifdef WIN32
	// On Windows, the "file attribute modification time" is the
	// file creation time, and we want to back this up, restore
	// it and compare it.
	//
	// On other platforms, it's not very important and can't
	// reliably be set to anything other than the current time.
	hashData.fileCreationTime = box_hton64(st.st_ctime);
	#endif

	StreamableMemBlock xattr;
	FillExtendedAttr(xattr, filename.c_str());

	// Create a MD5 hash of the data, filename, and secret
	MD5Digest digest;
	digest.Add(&hashData, sizeof(hashData));
	digest.Add(xattr.GetBuffer(), xattr.GetSize());
	digest.Add(leafname.c_str(), leafname.size());
	digest.Add(sAttributeHashSecret, sAttributeHashSecretLength);	
	digest.Finish();
	
	// Return the first 64 bits of the hash
	uint64_t result;
	memcpy(&result, digest.DigestAsData(), sizeof(result));
	return result;
}
