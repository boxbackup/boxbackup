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
	Archive(IOStream &Stream, int Timeout)
		: mrStream(Stream)
	{
		mTimeout = Timeout;
	}
private:
	// no copying
	Archive(const Archive &);
	Archive & operator=(const Archive &);
public:
	~Archive()
	{
	}
	//
	//
	//
	void Write(bool Item)
	{
		Write((int) Item);
	}
	void WriteExact(uint32_t Item) { Write((int)Item); }
	void Write(int Item)
	{
		int32_t privItem = htonl(Item);
		mrStream.Write(&privItem, sizeof(privItem));
	}
	void Write(int64_t Item)
	{
		int64_t privItem = box_hton64(Item);
		mrStream.Write(&privItem, sizeof(privItem));
	}
	void WriteExact(uint64_t Item) { Write(Item); }
	void Write(uint64_t Item)
	{
		uint64_t privItem = box_hton64(Item);
		mrStream.Write(&privItem, sizeof(privItem));
	}
	void Write(uint8_t Item)
	{
		int privItem = Item;
		Write(privItem);
	}
	void Write(const std::string &Item)
	{
		int size = Item.size();
		Write(size);
		mrStream.Write(Item.c_str(), size);
	}
	//
	//
	//
	void Read(bool &rItemOut)
	{
		int privItem;
		Read(privItem);

		if (privItem)
		{
			rItemOut = true;
		}
		else
		{
			rItemOut = false;
		}
	}
	void ReadExact(uint32_t &rItemOut) { Read((int&)rItemOut); }
	void Read(int &rItemOut)
	{
		int32_t privItem;
		if(!mrStream.ReadFullBuffer(&privItem, sizeof(privItem), 0 /* not interested in bytes read if this fails */))
		{
			THROW_EXCEPTION(CommonException, ArchiveBlockIncompleteRead)
		}
		rItemOut = ntohl(privItem);
	}
	void Read(int64_t &rItemOut)
	{
		int64_t privItem;
		if(!mrStream.ReadFullBuffer(&privItem, sizeof(privItem), 0 /* not interested in bytes read if this fails */))
		{
			THROW_EXCEPTION(CommonException, ArchiveBlockIncompleteRead)
		}
		rItemOut = box_ntoh64(privItem);
	}
	void ReadExact(uint64_t &rItemOut) { Read(rItemOut); }
	void Read(uint64_t &rItemOut)
	{
		uint64_t privItem;
		if(!mrStream.ReadFullBuffer(&privItem, sizeof(privItem), 0 /* not interested in bytes read if this fails */))
		{
			THROW_EXCEPTION(CommonException, ArchiveBlockIncompleteRead)
		}
		rItemOut = box_ntoh64(privItem);
	}
	void Read(uint8_t &rItemOut)
	{
		int privItem;
		Read(privItem);
		rItemOut = privItem;
	}
	void Read(std::string &rItemOut)
	{
		int size;
		Read(size);

		// Assume most strings are relatively small
		char buf[256];
		if(size < (int) sizeof(buf))
		{
			// Fetch rest of pPayload, relying on the Protocol to error on stupidly large sizes for us
			if(!mrStream.ReadFullBuffer(buf, size, 0 /* not interested in bytes read if this fails */, mTimeout))
			{
				THROW_EXCEPTION(CommonException, ArchiveBlockIncompleteRead)
			}
			// assign to this string, storing the header and the extra payload
			rItemOut.assign(buf, size);
		}
		else
		{
			// Block of memory to hold it
			MemoryBlockGuard<char*> dataB(size);
			char *ppayload = dataB;

			// Fetch rest of pPayload, relying on the Protocol to error on stupidly large sizes for us
			if(!mrStream.ReadFullBuffer(ppayload, size, 0 /* not interested in bytes read if this fails */, mTimeout))
			{
				THROW_EXCEPTION(CommonException, ArchiveBlockIncompleteRead)
			}
			// assign to this string, storing the header and the extra pPayload
			rItemOut.assign(ppayload, size);
		}
	}
private:
	IOStream &mrStream;
	int mTimeout;
};

#endif // ARCHIVE__H
