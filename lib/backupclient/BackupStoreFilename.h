// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
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

