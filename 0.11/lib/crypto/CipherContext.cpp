// --------------------------------------------------------------------------
//
// File
//		Name:    CipherContext.cpp
//		Purpose: Context for symmetric encryption / descryption
//		Created: 1/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#define BOX_LIB_CRYPTO_OPENSSL_HEADERS_INCLUDED_TRUE
#include "CipherContext.h"
#include "CipherDescription.h"
#include "CipherException.h"
#include "Random.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::CipherContext()
//		Purpose: Constructor
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherContext::CipherContext()
	: mInitialised(false),
	  mWithinTransform(false),
	  mPaddingOn(true)
#ifdef HAVE_OLD_SSL
	, mFunction(Decrypt),
	  mpDescription(0)
#endif
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::~CipherContext()
//		Purpose: Destructor
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
CipherContext::~CipherContext()
{
	if(mInitialised)
	{
		// Clean up
		EVP_CIPHER_CTX_cleanup(&ctx);
		mInitialised = false;
	}
#ifdef HAVE_OLD_SSL
	if(mpDescription != 0)
	{
		delete mpDescription;
		mpDescription = 0;
	}
#endif
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::Init(CipherContext::CipherFunction, const CipherDescription &)
//		Purpose: Initialises the context, specifying the direction for the encryption, and a
//				 description of the cipher to use, it's keys, etc
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
void CipherContext::Init(CipherContext::CipherFunction Function, const CipherDescription &rDescription)
{
	// Check for bad usage
	if(mInitialised)
	{
		THROW_EXCEPTION(CipherException, AlreadyInitialised)
	}
	if(Function != Decrypt && Function != Encrypt)
	{
		THROW_EXCEPTION(CipherException, BadArguments)
	}
	
	// Initialise the cipher
#ifndef HAVE_OLD_SSL
	EVP_CIPHER_CTX_init(&ctx); // no error return code, even though the docs says it does

	if(EVP_CipherInit_ex(&ctx, rDescription.GetCipher(), NULL, NULL, NULL, Function) != 1)
#else
	// Store function for later
	mFunction = Function;

	// Use old version of init call
	if(EVP_CipherInit(&ctx, rDescription.GetCipher(), NULL, NULL, Function) != 1)
#endif
	{
		THROW_EXCEPTION(CipherException, EVPInitFailure)
	}
	
	try
	{
#ifndef HAVE_OLD_SSL
		// Let the description set up everything else
		rDescription.SetupParameters(&ctx);
#else
		// With the old version, a copy needs to be taken first.
		mpDescription = rDescription.Clone();
		// Mark it as not a leak, otherwise static cipher contexts
		// cause spurious memory leaks to be reported
		MEMLEAKFINDER_NOT_A_LEAK(mpDescription);
		mpDescription->SetupParameters(&ctx);
#endif
	}
	catch(...)
	{
		EVP_CIPHER_CTX_cleanup(&ctx);
		throw;
	}

	// mark as initialised
	mInitialised = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::Reset()
//		Purpose: Reset the context, so it can be initialised again with a different key
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
void CipherContext::Reset()
{
	if(mInitialised)
	{
		// Clean up
		EVP_CIPHER_CTX_cleanup(&ctx);
		mInitialised = false;
	}
#ifdef HAVE_OLD_SSL
	if(mpDescription != 0)
	{
		delete mpDescription;
		mpDescription = 0;
	}
#endif
	mWithinTransform = false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::Begin()
//		Purpose: Begin a transformation
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
void CipherContext::Begin()
{
	if(!mInitialised)
	{
		THROW_EXCEPTION(CipherException, NotInitialised)
	}

	// Warn if in a transformation (not an error, because a context might not have been finalised if an exception occured)
	if(mWithinTransform)
	{
		BOX_WARNING("CipherContext::Begin called when context "
			"flagged as within a transform");
	}

	// Initialise the cipher context again
	if(EVP_CipherInit(&ctx, NULL, NULL, NULL, -1) != 1)
	{
		THROW_EXCEPTION(CipherException, EVPInitFailure)
	}
	
	// Mark as being within a transform
	mWithinTransform = true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::Transform(void *, int, const void *, int)
//		Purpose: Transforms the data in the in buffer to the out buffer. If pInBuffer == 0 && InLength == 0
//				 then Final() is called instead.
//				 Returns the number of bytes placed in the out buffer.
//				 There must be room in the out buffer for all the data in the in buffer.
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
int CipherContext::Transform(void *pOutBuffer, int OutLength, const void *pInBuffer, int InLength)
{
	if(!mInitialised)
	{
		THROW_EXCEPTION(CipherException, NotInitialised)
	}
	
	if(!mWithinTransform)
	{
		THROW_EXCEPTION(CipherException, BeginNotCalled)
	}

	// Check parameters
	if(pOutBuffer == 0 || OutLength < 0 || (pInBuffer != 0 && InLength <= 0) || (pInBuffer == 0 && InLength != 0))
	{
		THROW_EXCEPTION(CipherException, BadArguments)
	}
	
	// Is this the final call?
	if(pInBuffer == 0)
	{
		return Final(pOutBuffer, OutLength);
	}
	
	// Check output buffer size
	if(OutLength < (InLength + EVP_CIPHER_CTX_block_size(&ctx)))
	{
		THROW_EXCEPTION(CipherException, OutputBufferTooSmall);
	}
	
	// Do the transform
	int outLength = OutLength;
	if(EVP_CipherUpdate(&ctx, (unsigned char*)pOutBuffer, &outLength, (unsigned char*)pInBuffer, InLength) != 1)
	{
		THROW_EXCEPTION(CipherException, EVPUpdateFailure)
	}

	return outLength;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::Final(void *, int)
//		Purpose: Transforms the data as per Transform, and returns the final data in the out buffer.
//				 Returns the number of bytes written in the out buffer.
//				 Two main causes of exceptions being thrown: 1) Data is corrupt, and so the end isn't
//				 padded properly. 2) Padding is off, and the data to be encrypted isn't a multiple
//				 of a block long.
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
int CipherContext::Final(void *pOutBuffer, int OutLength)
{
	if(!mInitialised)
	{
		THROW_EXCEPTION(CipherException, NotInitialised)
	}

	if(!mWithinTransform)
	{
		THROW_EXCEPTION(CipherException, BeginNotCalled)
	}

	// Check parameters
	if(pOutBuffer == 0 || OutLength < 0)
	{
		THROW_EXCEPTION(CipherException, BadArguments)
	}

	// Check output buffer size
	if(OutLength < (2 * EVP_CIPHER_CTX_block_size(&ctx)))
	{
		THROW_EXCEPTION(CipherException, OutputBufferTooSmall);
	}
	
	// Do the transform
	int outLength = OutLength;
#ifndef HAVE_OLD_SSL
	if(EVP_CipherFinal_ex(&ctx, (unsigned char*)pOutBuffer, &outLength) != 1)
	{
		THROW_EXCEPTION(CipherException, EVPFinalFailure)
	}
#else
	OldOpenSSLFinal((unsigned char*)pOutBuffer, outLength);
#endif
	
	mWithinTransform = false;

	return outLength;
}


#ifdef HAVE_OLD_SSL
// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::OldOpenSSLFinal(unsigned char *, int &)
//		Purpose: The old version of OpenSSL needs more work doing to finalise the cipher,
//				 and reset it so that it's ready for another go.
//		Created: 27/3/04
//
// --------------------------------------------------------------------------
void CipherContext::OldOpenSSLFinal(unsigned char *Buffer, int &rOutLengthOut)
{
	// Old version needs to use a different form, and then set up the cipher again for next time around
	int outLength = rOutLengthOut;
	// Have to emulate padding off...
	int blockSize = EVP_CIPHER_CTX_block_size(&ctx);
	if(mPaddingOn)
	{
		// Just use normal final call
		if(EVP_CipherFinal(&ctx, Buffer, &outLength) != 1)
		{
			THROW_EXCEPTION(CipherException, EVPFinalFailure)
		}
	}
	else
	{
		// Padding is off. OpenSSL < 0.9.7 doesn't support this, so it has to be
		// bodged in there. Which isn't nice.
		if(mFunction == Decrypt)
		{
			// NASTY -- fiddling around with internals like this is bad.
			// But only way to get this working on old versions of OpenSSL.
			if(!EVP_EncryptUpdate(&ctx,Buffer,&outLength,ctx.buf,0)
				|| outLength != blockSize)
			{
				THROW_EXCEPTION(CipherException, EVPFinalFailure)
			}
			// Clean up
			EVP_CIPHER_CTX_cleanup(&ctx);
		}
		else
		{
			// Check that the length is correct
			if((ctx.buf_len % blockSize) != 0)
			{
				THROW_EXCEPTION(CipherException, EVPFinalFailure)
			}
			// For encryption, assume that the last block entirely is
			// padding, and remove it.
			char temp[1024];
			outLength = sizeof(temp);
			if(EVP_CipherFinal(&ctx, Buffer, &outLength) != 1)
			{
				THROW_EXCEPTION(CipherException, EVPFinalFailure)
			}
			// Remove last block, assuming it's full of padded bytes only.
			outLength -= blockSize;
			// Copy anything to the main buffer
			// (can't just use main buffer, because it might overwrite something important)
			if(outLength > 0)
			{
				::memcpy(Buffer, temp, outLength);
			}
		}
	}
	// Reinitialise the cipher for the next time around
	if(EVP_CipherInit(&ctx, mpDescription->GetCipher(), NULL, NULL, mFunction) != 1)
	{
		THROW_EXCEPTION(CipherException, EVPInitFailure)
	}
	mpDescription->SetupParameters(&ctx);

	// Update length for caller
	rOutLengthOut = outLength;
}
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::InSizeForOutBufferSize(int)
//		Purpose: Returns the maximum amount of data that can be sent in
//				 given a output buffer size.
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
int CipherContext::InSizeForOutBufferSize(int OutLength)
{
	if(!mInitialised)
	{
		THROW_EXCEPTION(CipherException, NotInitialised)
	}

	// Strictly speaking, the *2 is unnecessary. However... 
	// Final() is paranoid, and requires two input blocks of space to work.
	return OutLength - (EVP_CIPHER_CTX_block_size(&ctx) * 2);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::MaxOutSizeForInBufferSize(int)
//		Purpose: Returns the maximum output size for an input of a given length.
//				 Will tend to over estimate, as it needs to allow space for Final() to be called.
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
int CipherContext::MaxOutSizeForInBufferSize(int InLength)
{
	if(!mInitialised)
	{
		THROW_EXCEPTION(CipherException, NotInitialised)
	}

	// Final() is paranoid, and requires two input blocks of space to work, and so we need to add
	// three blocks on to be absolutely sure.
	return InLength + (EVP_CIPHER_CTX_block_size(&ctx) * 3);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::TransformBlock(void *, int, const void *, int)
//		Purpose: Transform one block to another all in one go, no Final required.
//		Created: 1/12/03
//
// --------------------------------------------------------------------------
int CipherContext::TransformBlock(void *pOutBuffer, int OutLength, const void *pInBuffer, int InLength)
{
	if(!mInitialised)
	{
		THROW_EXCEPTION(CipherException, NotInitialised)
	}
	
	// Warn if in a transformation
	if(mWithinTransform)
	{
		BOX_WARNING("CipherContext::TransformBlock called when "
			"context flagged as within a transform");
	}

	// Check output buffer size
	if(OutLength < (InLength + EVP_CIPHER_CTX_block_size(&ctx)))
	{
		// Check if padding is off, in which case the buffer can be smaller
		if(!mPaddingOn && OutLength <= InLength)
		{
			// This is OK.
		}
		else
		{
			THROW_EXCEPTION(CipherException, OutputBufferTooSmall);
		}
	}
	
	// Initialise the cipher context again
	if(EVP_CipherInit(&ctx, NULL, NULL, NULL, -1) != 1)
	{
		THROW_EXCEPTION(CipherException, EVPInitFailure)
	}
	
	// Do the entire block
	int outLength = 0;
	try
	{
		// Update
		outLength = OutLength;
		if(EVP_CipherUpdate(&ctx, (unsigned char*)pOutBuffer, &outLength, (unsigned char*)pInBuffer, InLength) != 1)
		{
			THROW_EXCEPTION(CipherException, EVPUpdateFailure)
		}
		// Finalise
		int outLength2 = OutLength - outLength;
#ifndef HAVE_OLD_SSL
		if(EVP_CipherFinal_ex(&ctx, ((unsigned char*)pOutBuffer) + outLength, &outLength2) != 1)
		{
			THROW_EXCEPTION(CipherException, EVPFinalFailure)
		}
#else
		OldOpenSSLFinal(((unsigned char*)pOutBuffer) + outLength, outLength2);
#endif
		outLength += outLength2;
	}
	catch(...)
	{
		// Finalise the context, so definately ready for the next caller
		int outs = OutLength;
#ifndef HAVE_OLD_SSL
		EVP_CipherFinal_ex(&ctx, (unsigned char*)pOutBuffer, &outs);
#else
		OldOpenSSLFinal((unsigned char*)pOutBuffer, outs);
#endif
		throw;
	}

	return outLength;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::GetIVLength()
//		Purpose: Returns the size of the IV for this context
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
int CipherContext::GetIVLength()
{
	if(!mInitialised)
	{
		THROW_EXCEPTION(CipherException, NotInitialised)
	}
	
	return EVP_CIPHER_CTX_iv_length(&ctx);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::SetIV(const void *)
//		Purpose: Sets the IV for this context (must be correctly sized, use GetIVLength)
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
void CipherContext::SetIV(const void *pIV)
{
	if(!mInitialised)
	{
		THROW_EXCEPTION(CipherException, NotInitialised)
	}
	
	// Warn if in a transformation
	if(mWithinTransform)
	{
		BOX_WARNING("CipherContext::SetIV called when context "
			"flagged as within a transform");
	}

	// Set IV
	if(EVP_CipherInit(&ctx, NULL, NULL, (unsigned char *)pIV, -1) != 1)
	{
		THROW_EXCEPTION(CipherException, EVPInitFailure)
	}

#ifdef HAVE_OLD_SSL
	// Update description
	if(mpDescription != 0)
	{
		mpDescription->SetIV(pIV);
	}
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::SetRandomIV(int &)
//		Purpose: Set a random IV for the context, and return a pointer to the IV used,
//				 and the length of this IV in the rLengthOut arg.
//		Created: 3/12/03
//
// --------------------------------------------------------------------------
const void *CipherContext::SetRandomIV(int &rLengthOut)
{
	if(!mInitialised)
	{
		THROW_EXCEPTION(CipherException, NotInitialised)
	}
	
	// Warn if in a transformation
	if(mWithinTransform)
	{
		BOX_WARNING("CipherContext::SetRandomIV called when "
			"context flagged as within a transform");
	}

	// Get length of IV
	unsigned int ivLen = EVP_CIPHER_CTX_iv_length(&ctx);
	if(ivLen > sizeof(mGeneratedIV))
	{
		THROW_EXCEPTION(CipherException, IVSizeImplementationLimitExceeded)
	}
	
	// Generate some random data
	Random::Generate(mGeneratedIV, ivLen);

	// Set IV
	if(EVP_CipherInit(&ctx, NULL, NULL, mGeneratedIV, -1) != 1)
	{
		THROW_EXCEPTION(CipherException, EVPInitFailure)
	}	

#ifdef HAVE_OLD_SSL
	// Update description
	if(mpDescription != 0)
	{
		mpDescription->SetIV(mGeneratedIV);
	}
#endif

	// Return the IV and it's length
	rLengthOut = ivLen;
	return mGeneratedIV;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    CipherContext::UsePadding(bool)
//		Purpose: Set whether or not the context uses padding. 
//		Created: 12/12/03
//
// --------------------------------------------------------------------------
void CipherContext::UsePadding(bool Padding)
{
#ifndef HAVE_OLD_SSL
	if(EVP_CIPHER_CTX_set_padding(&ctx, Padding) != 1)
	{
		THROW_EXCEPTION(CipherException, EVPSetPaddingFailure)
	}
#endif
	mPaddingOn = Padding;
}



