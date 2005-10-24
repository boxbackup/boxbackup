// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFilenameClear.cpp
//		Purpose: BackupStoreFilenames in the clear
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "BackupStoreFilenameClear.h"
#include "BackupStoreException.h"
#include "CipherContext.h"
#include "CipherBlowfish.h"
#include "Guards.h"

#include "MemLeakFindOn.h"

// Hide private variables from the rest of the world
namespace
{
	int sEncodeMethod = BackupStoreFilename::Encoding_Clear;
	CipherContext sBlowfishEncrypt;
	CipherContext sBlowfishDecrypt;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::BackupStoreFilenameClear()
//		Purpose: Default constructor, creates an invalid filename
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreFilenameClear::BackupStoreFilenameClear()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::BackupStoreFilenameClear(const std::string &)
//		Purpose: Creates a filename, encoding from the given string
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreFilenameClear::BackupStoreFilenameClear(const std::string &rToEncode)
{
	SetClearFilename(rToEncode);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::BackupStoreFilenameClear(const BackupStoreFilenameClear &)
//		Purpose: Copy constructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreFilenameClear::BackupStoreFilenameClear(const BackupStoreFilenameClear &rToCopy)
	: BackupStoreFilename(rToCopy),
	  mClearFilename(rToCopy.mClearFilename)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::BackupStoreFilenameClear(const BackupStoreFilename &rToCopy)
//		Purpose: Copy from base class
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreFilenameClear::BackupStoreFilenameClear(const BackupStoreFilename &rToCopy)
	: BackupStoreFilename(rToCopy)
{
	// Will get a clear filename when it's required
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::~BackupStoreFilenameClear()
//		Purpose: Destructor
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
BackupStoreFilenameClear::~BackupStoreFilenameClear()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::GetClearFilename()
//		Purpose: Get the unencoded filename
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
#ifdef BACKUPSTOREFILEAME_MALLOC_ALLOC_BASE_TYPE
const std::string BackupStoreFilenameClear::GetClearFilename() const
{
	MakeClearAvailable();
	// When modifying, remember to change back to reference return if at all possible
	// -- returns an object rather than a reference to allow easy use with other code.
	return std::string(mClearFilename.c_str(), mClearFilename.size());
}
#else
const std::string &BackupStoreFilenameClear::GetClearFilename() const
{
	MakeClearAvailable();
	return mClearFilename;
}
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::SetClearFilename(const std::string &)
//		Purpose: Encode and make available the clear filename
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreFilenameClear::SetClearFilename(const std::string &rToEncode)
{
	// Only allow Blowfish encodings
	if(sEncodeMethod != Encoding_Blowfish)
	{
		THROW_EXCEPTION(BackupStoreException, FilenameEncryptionNotSetup)
	}

	// Make an encoded string with blowfish encryption
	EncryptClear(rToEncode, sBlowfishEncrypt, Encoding_Blowfish);
		
	// Store the clear filename
	mClearFilename.assign(rToEncode.c_str(), rToEncode.size());

	// Make sure we did the right thing
	if(!CheckValid(false))
	{
		THROW_EXCEPTION(BackupStoreException, Internal)
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::MakeClearAvailable()
//		Purpose: Private. Make sure the clear filename is available
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreFilenameClear::MakeClearAvailable() const
{
	if(!mClearFilename.empty())
		return;		// nothing to do

	// Check valid
	CheckValid();
		
	// Decode the header
	int size = BACKUPSTOREFILENAME_GET_SIZE(*this);
	int encoding = BACKUPSTOREFILENAME_GET_ENCODING(*this);
	
	// Decode based on encoding given in the header
	switch(encoding)
	{
	case Encoding_Clear:
		TRACE0("**** BackupStoreFilename encoded with Clear encoding ****\n");
		mClearFilename.assign(c_str() + 2, size - 2);
		break;
		
	case Encoding_Blowfish:
		DecryptEncoded(sBlowfishDecrypt);
		break;
	
	default:
		THROW_EXCEPTION(BackupStoreException, UnknownFilenameEncoding)
		break;	
	}
}


// Buffer for encoding and decoding -- do this all in one single buffer to
// avoid lots of string allocation, which stuffs up memory usage.
// These static memory vars are, of course, not thread safe, but we don't use threads.
#ifndef NDEBUG
static int sEncDecBufferSize = 2;	// small size for debug builds
#else
static int sEncDecBufferSize = 256;
#endif
static MemoryBlockGuard<uint8_t *> spEncDecBuffer(sEncDecBufferSize);

// fudge to stop leak reporting
#ifdef BOX_MEMORY_LEAK_TESTING
namespace
{
	class leak_off
	{
	public:
		leak_off()
		{
			MEMLEAKFINDER_NOT_A_LEAK(spEncDecBuffer);
		}
	};
	leak_off dont_report_as_leak;
}
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::EncryptClear(const std::string &, CipherContext &, int)
//		Purpose: Private. Assigns the encoded filename string, encrypting.
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
void BackupStoreFilenameClear::EncryptClear(const std::string &rToEncode, CipherContext &rCipherContext, int StoreAsEncoding)
{
	// Work out max size
	int maxOutSize = rCipherContext.MaxOutSizeForInBufferSize(rToEncode.size()) + 4;
	
	// Make sure encode/decode buffer has enough space
	if(sEncDecBufferSize < maxOutSize)
	{
		TRACE2("Reallocating filename encoding/decoding buffer from %d to %d\n", sEncDecBufferSize, maxOutSize);
		spEncDecBuffer.Resize(maxOutSize);
		sEncDecBufferSize = maxOutSize;
	}
	
	// Pointer to buffer
	uint8_t *buffer = spEncDecBuffer;
	MEMLEAKFINDER_NOT_A_LEAK(buffer);
	
	// Encode -- do entire block in one go
	int encSize = rCipherContext.TransformBlock(buffer + 2, sEncDecBufferSize - 2, rToEncode.c_str(), rToEncode.size());
	// and add in header size
	encSize += 2;
	
	// Adjust header
	BACKUPSTOREFILENAME_MAKE_HDR(buffer, encSize, StoreAsEncoding);
	
	// Store the encoded string
	assign((char*)buffer, encSize);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::DecryptEncoded(CipherContext &)
//		Purpose: Decrypt the encoded filename using the cipher context
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
void BackupStoreFilenameClear::DecryptEncoded(CipherContext &rCipherContext) const
{
	// Work out max size
	int maxOutSize = rCipherContext.MaxOutSizeForInBufferSize(size()) + 4;
	
	// Make sure encode/decode buffer has enough space
	if(sEncDecBufferSize < maxOutSize)
	{
		TRACE2("Reallocating filename encoding/decoding buffer from %d to %d\n", sEncDecBufferSize, maxOutSize);
		spEncDecBuffer.Resize(maxOutSize);
		sEncDecBufferSize = maxOutSize;
	}
	
	// Pointer to buffer
	uint8_t *buffer = spEncDecBuffer;
	MEMLEAKFINDER_NOT_A_LEAK(buffer);
	
	// Decrypt
	const char *str = c_str() + 2;
	int sizeOut = rCipherContext.TransformBlock(buffer, sEncDecBufferSize, str, size() - 2);
	
	// Assign to this
	mClearFilename.assign((char*)buffer, sizeOut);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::EncodedFilenameChanged()
//		Purpose: The encoded filename stored has changed
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void BackupStoreFilenameClear::EncodedFilenameChanged()
{
	BackupStoreFilename::EncodedFilenameChanged();

	// Delete stored filename in clear
	mClearFilename.erase();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::SetBlowfishKey(const void *, int)
//		Purpose: Set the key used for Blowfish encryption of filenames
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
void BackupStoreFilenameClear::SetBlowfishKey(const void *pKey, int KeyLength, const void *pIV, int IVLength)
{
	// Initialisation vector not used. Can't use a different vector for each filename as
	// that would stop comparisions on the server working.
	sBlowfishEncrypt.Reset();
	sBlowfishEncrypt.Init(CipherContext::Encrypt, CipherBlowfish(CipherDescription::Mode_CBC, pKey, KeyLength));
	ASSERT(sBlowfishEncrypt.GetIVLength() == IVLength);
	sBlowfishEncrypt.SetIV(pIV);
	sBlowfishDecrypt.Reset();
	sBlowfishDecrypt.Init(CipherContext::Decrypt, CipherBlowfish(CipherDescription::Mode_CBC, pKey, KeyLength));
	ASSERT(sBlowfishDecrypt.GetIVLength() == IVLength);
	sBlowfishDecrypt.SetIV(pIV);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFilenameClear::SetEncodingMethod(int)
//		Purpose: Set the encoding method used for filenames
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
void BackupStoreFilenameClear::SetEncodingMethod(int Method)
{
	sEncodeMethod = Method;
}



