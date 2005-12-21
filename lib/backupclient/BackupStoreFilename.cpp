// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFilename.cpp
//		Purpose: Filename for the backup store
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "BackupStoreFilename.h"
#include "Protocol.h"
#include "BackupStoreException.h"
#include "IOStream.h"
#include "Guards.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilename::BackupStoreFilename()
//		Purpose: Default constructor -- creates an invalid filename
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreFilename::BackupStoreFilename()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilename::BackupStoreFilename(const BackupStoreFilename &)
//		Purpose: Copy constructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreFilename::BackupStoreFilename(const BackupStoreFilename &rToCopy)
	: BackupStoreFilename_base(rToCopy)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilename::~BackupStoreFilename()
//		Purpose: Destructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreFilename::~BackupStoreFilename()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilename::CheckValid(bool)
//		Purpose: Checks the encoded filename for validity
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
bool BackupStoreFilename::CheckValid(bool ExceptionIfInvalid) const
{
	bool ok = true;
	
	if(size() < 2)
	{
		// Isn't long enough to have a header
		ok = false;
	}
	else
	{
		// Check size is consistent
		unsigned int dsize = BACKUPSTOREFILENAME_GET_SIZE(*this);
		if(dsize != size())
		{
			ok = false;
		}
		
		// And encoding is an accepted value
		unsigned int encoding = BACKUPSTOREFILENAME_GET_ENCODING(*this);
		if(encoding < Encoding_Min || encoding > Encoding_Max)
		{
			ok = false;
		}
	}
	
	// Exception?
	if(!ok && ExceptionIfInvalid)
	{
		THROW_EXCEPTION(BackupStoreException, InvalidBackupStoreFilename)
	}

	return ok;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilename::ReadFromProtocol(Protocol &)
//		Purpose: Reads the filename from the protocol object
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreFilename::ReadFromProtocol(Protocol &rProtocol)
{
	// Read the header
	char hdr[2];
	rProtocol.Read(hdr, 2);
	
	// How big is it?
	int dsize = BACKUPSTOREFILENAME_GET_SIZE(hdr);
	
	// Fetch rest of data, relying on the Protocol to error on stupidly large sizes for us
	std::string data;
	rProtocol.Read(data, dsize - 2);
	
	// assign to this string, storing the header and the extra data
	assign(hdr, 2);
	append(data.c_str(), data.size());
	
	// Check it
	CheckValid();
	
	// Alert derived classes
	EncodedFilenameChanged();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilename::WriteToProtocol(Protocol &)
//		Purpose: Writes the filename to the protocol object
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreFilename::WriteToProtocol(Protocol &rProtocol) const
{
	CheckValid();
	
	rProtocol.Write(c_str(), (int)size());
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilename::ReadFromStream(IOStream &)
//		Purpose: Reads the filename from a stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreFilename::ReadFromStream(IOStream &rStream, int Timeout)
{
	// Read the header
	char hdr[2];
	if(!rStream.ReadFullBuffer(hdr, 2, 0 /* not interested in bytes read if this fails */, Timeout))
	{
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}
	
	// How big is it?
	unsigned int dsize = BACKUPSTOREFILENAME_GET_SIZE(hdr);
	
	// Assume most filenames are small
	char buf[256];
	if(dsize < sizeof(buf))
	{
		// Fetch rest of data, relying on the Protocol to error on stupidly large sizes for us
		if(!rStream.ReadFullBuffer(buf + 2, dsize - 2, 0 /* not interested in bytes read if this fails */, Timeout))
		{
			THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
		}
		// Copy in header
		buf[0] = hdr[0]; buf[1] = hdr[1];

		// assign to this string, storing the header and the extra data
		assign(buf, dsize);
	}
	else
	{
		// Block of memory to hold it
		MemoryBlockGuard<char*> dataB(dsize+2);
		char *data = dataB;

		// Fetch rest of data, relying on the Protocol to error on stupidly large sizes for us
		if(!rStream.ReadFullBuffer(data + 2, dsize - 2, 0 /* not interested in bytes read if this fails */, Timeout))
		{
			THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
		}
		// Copy in header
		data[0] = hdr[0]; data[1] = hdr[1];

		// assign to this string, storing the header and the extra data
		assign(data, dsize);
	}
	
	// Check it
	CheckValid();
	
	// Alert derived classes
	EncodedFilenameChanged();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilename::WriteToStream(IOStream &)
//		Purpose: Writes the filename to a stream
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreFilename::WriteToStream(IOStream &rStream) const
{
	CheckValid();
	
	rStream.Write(c_str(), (int)size());
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilename::EncodedFilenameChanged()
//		Purpose: The encoded filename stored has changed
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreFilename::EncodedFilenameChanged()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilename::IsEncrypted()
//		Purpose: Returns true if the filename is stored using an encrypting encoding
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
bool BackupStoreFilename::IsEncrypted() const
{
	return BACKUPSTOREFILENAME_GET_ENCODING(*this) != Encoding_Clear;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilename::SetAsClearFilename(const char *)
//		Purpose: Sets this object to be a valid filename, but with a filename in the clear.
//				 Used on the server to create filenames when there's no way of encrypting it.
//		Created: 22/4/04
//
// --------------------------------------------------------------------------
void BackupStoreFilename::SetAsClearFilename(const char *Clear)
{
	// Make std::string from the clear name
	std::string toEncode(Clear);

	// Make an encoded string
	char hdr[2];
	BACKUPSTOREFILENAME_MAKE_HDR(hdr, toEncode.size()+2, Encoding_Clear);
	std::string encoded(hdr, 2);
	encoded += toEncode;
	ASSERT(encoded.size() == toEncode.size() + 2);
	
	// Store the encoded string
	assign(encoded);
	
	// Stuff which must be done
	EncodedFilenameChanged();
	CheckValid(false);
}



