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
//		Name:    Archive.h
//		Purpose: Backup daemon state archive
//		Created: 2005/04/11
//
// --------------------------------------------------------------------------

#ifndef ARCHIVE__H
#define ARCHIVE__H

#include <vector>
#include <string>
#include <memory>

#include "IOStream.h"
#include "Guards.h"

#define ARCHIVE_GET_SIZE(hdr)		(( ((uint8_t)((hdr)[0])) | ( ((uint8_t)((hdr)[1])) << 8)) >> 2)

#define ARCHIVE_MAGIC_VALUE_RECURSE 0x4449525F
#define ARCHIVE_MAGIC_VALUE_NOOP 0x5449525F

class Archive
{
public:
	Archive()
	{
	}
	virtual ~Archive()
	{
	}
	//
	// primitive insertion operations
	//
	virtual void Add(bool bItem) = 0;
	virtual void Add(int iItem) = 0;
	virtual void Add(int64_t  iItem) = 0;
	virtual void Add(uint64_t iItem) = 0;
	virtual void Add(uint8_t iItem) = 0;
	virtual void Add(const std::string & strItem) = 0;
	//
	// chaining support
	//
	Archive & operator<<(bool bItem) { Add(bItem); return *this; }
	Archive & operator<<(int iItem) { Add(iItem); return *this; }
	Archive & operator<<(int64_t iItem) { Add(iItem); return *this; }
	Archive & operator<<(uint64_t iItem) { Add(iItem); return *this; }
	Archive & operator<<(uint8_t iItem) { Add(iItem); return *this; }
	Archive & operator<<(const std::string & strItem) { Add(strItem); return *this; }
	//
	// primitive extraction oprations
	//
	virtual void Get(bool & bItem) = 0;
	virtual void Get(int & iItem) = 0;
	virtual void Get(int64_t  & iItem) = 0;
	virtual void Get(uint64_t & iItem) = 0;
	virtual void Get(uint8_t & iItem) = 0;
	virtual void Get(std::string & strItem) = 0;
	//
	// chaining support
	//
	Archive & operator>>(bool & bItem) { Get(bItem); return *this; }
	Archive & operator>>(int & iItem) { Get(iItem); return *this; }
	Archive & operator>>(int64_t & iItem) { Get(iItem); return *this; }
	Archive & operator>>(uint64_t & iItem) { Get(iItem); return *this; }
	Archive & operator>>(uint8_t & iItem) { Get(iItem); return *this; }
	Archive & operator>>(std::string & strItem) { Get(strItem); return *this; }
private:
	Archive(const Archive &);
	Archive & operator=(const Archive &);
};

class IOStreamArchive : public Archive
{
public:
	IOStreamArchive(IOStream & mStream, int Timeout) : mStream(mStream)
	{
		mTimeout = Timeout;
	}
	virtual ~IOStreamArchive()
	{
	}
	//
	//
	//
	virtual void Add(bool bItem)
	{
		Add((int) bItem);
	}
	virtual void Add(int iItem)
	{
		int32_t privItem = htonl(iItem);
		mStream.Write(&privItem, sizeof(privItem));
	}
	virtual void Add(int64_t  iItem)
	{
		int64_t privItem = hton64(iItem);
		mStream.Write(&privItem, sizeof(privItem));
	}
	virtual void Add(uint64_t iItem)
	{
		uint64_t privItem = hton64(iItem);
		mStream.Write(&privItem, sizeof(privItem));
	}
	virtual void Add(uint8_t iItem)
	{
		int privItem = iItem;
		Add(privItem);
	}
	virtual void Add(const std::string & strItem)
	{
		int iSize = strItem.size();
		Add(iSize);
		mStream.Write(strItem.c_str(), iSize);
	}
	//
	//
	//
	virtual void Get(bool & bItem)
	{
		int privItem;
		Get(privItem);

		if (privItem)
			bItem = true;
		else
			bItem = false;
	}
	virtual void Get(int & iItem)
	{
		int32_t privItem;
		if(!mStream.ReadFullBuffer(&privItem, sizeof(privItem), 0 /* not interested in bytes read if this fails */))
		{
			THROW_EXCEPTION(CommonException, StreamableMemBlockIncompleteRead)
		}
		iItem = ntohl(privItem);
	}
	virtual void Get(int64_t  & iItem)
	{
		int64_t privItem;
		if(!mStream.ReadFullBuffer(&privItem, sizeof(privItem), 0 /* not interested in bytes read if this fails */))
		{
			THROW_EXCEPTION(CommonException, StreamableMemBlockIncompleteRead)
		}
		iItem = ntoh64(privItem);
	}
	virtual void Get(uint64_t & iItem)
	{
		uint64_t privItem;
		if(!mStream.ReadFullBuffer(&privItem, sizeof(privItem), 0 /* not interested in bytes read if this fails */))
		{
			THROW_EXCEPTION(CommonException, StreamableMemBlockIncompleteRead)
		}
		iItem = ntoh64(privItem);
	}
	virtual void Get(uint8_t & iItem)
	{
		int privItem;
		Get(privItem);
		iItem = privItem;
	}
	virtual void Get(std::string & strItem)
	{
		int iSize;
		Get(iSize);

		// Assume most strings are relatively small
		char buf[256];
		if(iSize < (int) sizeof(buf))
		{
			// Fetch rest of pPayload, relying on the Protocol to error on stupidly large sizes for us
			if(!mStream.ReadFullBuffer(buf, iSize, 0 /* not interested in bytes read if this fails */, mTimeout))
			{
				THROW_EXCEPTION(CommonException, StreamableMemBlockIncompleteRead)
			}
			// assign to this string, storing the header and the extra pPayload
			strItem.assign(buf, iSize);
		}
		else
		{
			// Block of memory to hold it
			MemoryBlockGuard<char*> dataB(iSize);
			char *pPayload = dataB;

			// Fetch rest of pPayload, relying on the Protocol to error on stupidly large sizes for us
			if(!mStream.ReadFullBuffer(pPayload, iSize, 0 /* not interested in bytes read if this fails */, mTimeout))
			{
				THROW_EXCEPTION(CommonException, StreamableMemBlockIncompleteRead)
			}
			// assign to this string, storing the header and the extra pPayload
			strItem.assign(pPayload, iSize);
		}
	}
protected:
	IOStream & mStream;
	int mTimeout;
private:
	IOStreamArchive(const IOStreamArchive &);
	IOStreamArchive & operator=(const IOStreamArchive &);
};

#endif // ARCHIVE__H
