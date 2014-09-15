// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreDirectory.h
//		Purpose: Representation of a backup directory
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREDIRECTORY__H
#define BACKUPSTOREDIRECTORY__H

#include <string>
#include <vector>

#include "autogen_BackupProtocol.h"
#include "BackupStoreFilenameClear.h"
#include "StreamableMemBlock.h"
#include "BoxTime.h"

class IOStream;

#ifndef BOX_RELEASE_BUILD
	#define ASSERT_NOT_INVALIDATED ASSERT(!mInvalidated);
#else
	#define ASSERT_NOT_INVALIDATED
#endif

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreDirectory
//		Purpose: In memory representation of a directory
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
class BackupStoreDirectory
{
private:
#ifndef BOX_RELEASE_BUILD
	bool mInvalidated;
#endif

public:
#ifndef BOX_RELEASE_BUILD
	void Invalidate()
	{
		mInvalidated = true;
		for (std::vector<Entry*>::iterator i = mEntries.begin();
			i != mEntries.end(); i++)
		{
			(*i)->Invalidate();
		}
	}
#endif

	BackupStoreDirectory();
	BackupStoreDirectory(int64_t ObjectID, int64_t ContainerID);
	// Convenience constructor from a stream
	BackupStoreDirectory(IOStream& rStream,
		int Timeout = IOStream::TimeOutInfinite)
#ifndef BOX_RELEASE_BUILD
	: mInvalidated(false)
#endif
	{
		ReadFromStream(rStream, Timeout);
	}
	BackupStoreDirectory(std::auto_ptr<IOStream> apStream,
		int Timeout = IOStream::TimeOutInfinite)
#ifndef BOX_RELEASE_BUILD
	: mInvalidated(false)
#endif
	{
		ReadFromStream(*apStream, Timeout);
	}
	BackupStoreDirectory(BackupProtocolCallable& protocol,
		int64_t DirectoryID, int Timeout,
		int16_t FlagsMustBeSet = BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
		int16_t FlagsNotToBeSet = BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
		bool FetchAttributes = true)
#ifndef BOX_RELEASE_BUILD
	: mInvalidated(false)
#endif
	{
		Download(protocol, DirectoryID, Timeout, FlagsMustBeSet,
			FlagsNotToBeSet, FetchAttributes);
	}
private:
	// Copying not allowed
	BackupStoreDirectory(const BackupStoreDirectory &rToCopy);
public:
	~BackupStoreDirectory();

	class Entry
	{
	private:
#ifndef BOX_RELEASE_BUILD
		bool mInvalidated;
#endif

	public:
#ifndef BOX_RELEASE_BUILD
		void Invalidate() { mInvalidated = true; }
#endif

		friend class BackupStoreDirectory;

		Entry();
		~Entry();
		Entry(const Entry &rToCopy);
		Entry(const BackupStoreFilename &rName, box_time_t ModificationTime, int64_t ObjectID, int64_t SizeInBlocks, int16_t Flags, uint64_t AttributesHash);

		void ReadFromStream(IOStream &rStream, int Timeout);
		void WriteToStream(IOStream &rStream) const;

		const BackupStoreFilename &GetName() const
		{
			ASSERT_NOT_INVALIDATED;
			return mName;
		}
		box_time_t GetModificationTime() const
		{
			ASSERT_NOT_INVALIDATED;
			return mModificationTime;
		}
		int64_t GetObjectID() const
		{
			ASSERT_NOT_INVALIDATED;
			return mObjectID;
		}
		// SetObjectID is dangerous! It should only be used when
		// creating a snapshot.
		void SetObjectID(int64_t NewObjectID)
		{
			ASSERT_NOT_INVALIDATED;
			mObjectID = NewObjectID;
		}
		int64_t GetSizeInBlocks() const
		{
			ASSERT_NOT_INVALIDATED;
			return mSizeInBlocks;
		}
		int16_t GetFlags() const
		{
			ASSERT_NOT_INVALIDATED;
			return mFlags;
		}
		void AddFlags(int16_t Flags)
		{
			ASSERT_NOT_INVALIDATED;
			mFlags |= Flags;
		}
		void RemoveFlags(int16_t Flags)
		{
			ASSERT_NOT_INVALIDATED;
			mFlags &= ~Flags;
		}

		// Some things can be changed
		void SetName(const BackupStoreFilename &rNewName)
		{
			ASSERT_NOT_INVALIDATED;
			mName = rNewName;
		}
		void SetSizeInBlocks(int64_t SizeInBlocks)
		{
			ASSERT_NOT_INVALIDATED;
			mSizeInBlocks = SizeInBlocks;
		}

		// Attributes
		bool HasAttributes() const
		{
			ASSERT_NOT_INVALIDATED;
			return !mAttributes.IsEmpty();
		}
		void SetAttributes(const StreamableMemBlock &rAttr, uint64_t AttributesHash)
		{
			ASSERT_NOT_INVALIDATED;
			mAttributes.Set(rAttr);
			mAttributesHash = AttributesHash;
		}
		const StreamableMemBlock &GetAttributes() const
		{
			ASSERT_NOT_INVALIDATED;
			return mAttributes;
		}
		uint64_t GetAttributesHash() const
		{
			ASSERT_NOT_INVALIDATED;
			return mAttributesHash;
		}

		// Marks
		// The lowest mark number a version of a file of this name has ever had
		uint32_t GetMinMarkNumber() const
		{
			ASSERT_NOT_INVALIDATED;
			return mMinMarkNumber;
		}
		// The mark number on this file
		uint32_t GetMarkNumber() const
		{
			ASSERT_NOT_INVALIDATED;
			return mMarkNumber;
		}

		// Make sure these flags are synced with those in backupprocotol.txt
		// ListDirectory command
		enum
		{
			Flags_INCLUDE_EVERYTHING 	= -1,
			Flags_EXCLUDE_NOTHING 		= 0,
			Flags_EXCLUDE_EVERYTHING	= 31,	// make sure this is kept as sum of ones below!
			Flags_File					= 1,
			Flags_Dir					= 2,
			Flags_Deleted				= 4,
			Flags_OldVersion			= 8,
			Flags_RemoveASAP			= 16	// if this flag is set, housekeeping will remove it as it is marked Deleted or OldVersion
		};
		// characters for textual listing of files -- see bbackupquery/BackupQueries
		#define BACKUPSTOREDIRECTORY_ENTRY_FLAGS_DISPLAY_NAMES "fdXoR"

		// convenience methods
		bool inline IsDir()
		{
			ASSERT_NOT_INVALIDATED;
			return GetFlags() & Flags_Dir;
		}
		bool inline IsFile()
		{
			ASSERT_NOT_INVALIDATED;
			return GetFlags() & Flags_File;
		}
		bool inline IsOld()
		{
			ASSERT_NOT_INVALIDATED;
			return GetFlags() & Flags_OldVersion;
		}
		bool inline IsDeleted()
		{
			ASSERT_NOT_INVALIDATED;
			return GetFlags() & Flags_Deleted;
		}
		bool inline MatchesFlags(int16_t FlagsMustBeSet, int16_t FlagsNotToBeSet)
		{
			ASSERT_NOT_INVALIDATED;
			return ((FlagsMustBeSet == Flags_INCLUDE_EVERYTHING) || ((mFlags & FlagsMustBeSet) == FlagsMustBeSet))
				&& ((mFlags & FlagsNotToBeSet) == 0);
		};

		// Get dependency info
		// new version this depends on
		int64_t GetDependsNewer() const
		{
			ASSERT_NOT_INVALIDATED;
			return mDependsNewer;
		}
		void SetDependsNewer(int64_t ObjectID)
		{
			ASSERT_NOT_INVALIDATED;
			mDependsNewer = ObjectID;
		}
		// older version which depends on this
		int64_t GetDependsOlder() const
		{
			ASSERT_NOT_INVALIDATED;
			return mDependsOlder;
		}
		void SetDependsOlder(int64_t ObjectID)
		{
			ASSERT_NOT_INVALIDATED;
			mDependsOlder = ObjectID;
		}

		// Dependency info saving
		bool HasDependencies()
		{
			ASSERT_NOT_INVALIDATED;
			return mDependsNewer != 0 || mDependsOlder != 0;
		}
		void ReadFromStreamDependencyInfo(IOStream &rStream, int Timeout);
		void WriteToStreamDependencyInfo(IOStream &rStream) const;

	private:
		BackupStoreFilename mName;
		box_time_t mModificationTime;
		int64_t mObjectID;
		int64_t mSizeInBlocks;
		int16_t mFlags;
		uint64_t mAttributesHash;
		StreamableMemBlock mAttributes;
		uint32_t mMinMarkNumber;
		uint32_t mMarkNumber;

		uint64_t mDependsNewer;	// new version this depends on
		uint64_t mDependsOlder;	// older version which depends on this
	};

	void Download(BackupProtocolCallable& protocol, int64_t DirectoryID, int Timeout,
		int16_t FlagsMustBeSet = BackupProtocolListDirectory::Flags_INCLUDE_EVERYTHING,
		int16_t FlagsNotToBeSet = BackupProtocolListDirectory::Flags_EXCLUDE_NOTHING,
		bool FetchAttributes = true)
	{
		ASSERT_NOT_INVALIDATED;
		protocol.QueryListDirectory(DirectoryID, FlagsMustBeSet,
			FlagsNotToBeSet, FetchAttributes);
		// Stream
		ReadFromStream(*protocol.ReceiveStream(), Timeout);
	}

#ifndef BOX_RELEASE_BUILD
	bool IsInvalidated()
	{
		return mInvalidated;
	}
#endif // !BOX_RELEASE_BUILD

	void ReadFromStream(IOStream &rStream, int Timeout);
	void WriteToStream(IOStream &rStream,
		int16_t FlagsMustBeSet = Entry::Flags_INCLUDE_EVERYTHING,
		int16_t FlagsNotToBeSet = Entry::Flags_EXCLUDE_NOTHING,
		bool StreamAttributes = true, bool StreamDependencyInfo = true) const;

	Entry *AddEntry(const Entry &rEntryToCopy);
	Entry *AddEntry(const BackupStoreFilename &rName,
		box_time_t ModificationTime, int64_t ObjectID,
		int64_t SizeInBlocks, int16_t Flags,
		uint64_t AttributesHash);
	void DeleteEntry(int64_t ObjectID);
	Entry *FindEntryByID(int64_t ObjectID) const;
	/*
	Entry *FindEntryByName(const BackupStoreFilename& rFilename,
		int16_t FlagsMustBeSet = Entry::Flags_INCLUDE_EVERYTHING,
		int16_t FlagsNotToBeSet = Entry::Flags_EXCLUDE_NOTHING)
	{
		return Iterator(*this).FindMatchingClearName(rFilename,
			FlagsMustBeSet, FlagsNotToBeSet);
	}
	*/
	int64_t GetObjectID() const
	{
		ASSERT_NOT_INVALIDATED;
		return mObjectID;
	}
	int64_t GetContainerID() const
	{
		ASSERT_NOT_INVALIDATED;
		return mContainerID;
	}

	// Need to be able to update the container ID when moving objects
	void SetContainerID(int64_t ContainerID)
	{
		ASSERT_NOT_INVALIDATED;
		mContainerID = ContainerID;
	}

	// Purely for use of server -- not serialised into streams
	int64_t GetRevisionID() const
	{
		ASSERT_NOT_INVALIDATED;
		return mRevisionID;
	}
	void SetRevisionID(int64_t RevisionID)
	{
		ASSERT_NOT_INVALIDATED;
		mRevisionID = RevisionID;
	}

	unsigned int GetNumberOfEntries() const
	{
		ASSERT_NOT_INVALIDATED;
		return mEntries.size();
	}

	// User info -- not serialised into streams
	int64_t GetUserInfo1_SizeInBlocks() const
	{
		ASSERT_NOT_INVALIDATED;
		return mUserInfo1;
	}
	void SetUserInfo1_SizeInBlocks(int64_t UserInfo1)
	{
		ASSERT_NOT_INVALIDATED;
		mUserInfo1 = UserInfo1;
	}

	// Attributes
	bool HasAttributes() const
	{
		ASSERT_NOT_INVALIDATED;
		return !mAttributes.IsEmpty();
	}
	void SetAttributes(const StreamableMemBlock &rAttr,
		box_time_t AttributesModTime)
	{
		ASSERT_NOT_INVALIDATED;
		mAttributes.Set(rAttr);
		mAttributesModTime = AttributesModTime;
	}
	const StreamableMemBlock &GetAttributes() const
	{
		ASSERT_NOT_INVALIDATED;
		return mAttributes;
	}
	box_time_t GetAttributesModTime() const
	{
		ASSERT_NOT_INVALIDATED;
		return mAttributesModTime;
	}

	class Iterator
	{
	public:
		Iterator(const BackupStoreDirectory &rDir)
			: mrDir(rDir), i(rDir.mEntries.begin())
		{
#ifndef BOX_RELEASE_BUILD
			ASSERT(!mrDir.mInvalidated);
#endif // !BOX_RELEASE_BUILD
		}

		BackupStoreDirectory::Entry *Next(int16_t FlagsMustBeSet = Entry::Flags_INCLUDE_EVERYTHING, int16_t FlagsNotToBeSet = Entry::Flags_EXCLUDE_NOTHING)
		{
#ifndef BOX_RELEASE_BUILD
			ASSERT(!mrDir.mInvalidated);
#endif // !BOX_RELEASE_BUILD
			// Skip over things which don't match the required flags
			while(i != mrDir.mEntries.end() && !(*i)->MatchesFlags(FlagsMustBeSet, FlagsNotToBeSet))
			{
				++i;
			}
			// Not the last one?
			if(i == mrDir.mEntries.end())
			{
				return 0;
			}
			// Return entry, and increment
			return (*(i++));
		}

		// WARNING: This function is really very inefficient.
		// Only use when you want to look up ONE filename, not in a loop looking up lots.
		// In a looping situation, cache the decrypted filenames in another memory structure.
		BackupStoreDirectory::Entry *FindMatchingClearName(const BackupStoreFilenameClear &rFilename, int16_t FlagsMustBeSet = Entry::Flags_INCLUDE_EVERYTHING, int16_t FlagsNotToBeSet = Entry::Flags_EXCLUDE_NOTHING)
		{
#ifndef BOX_RELEASE_BUILD
			ASSERT(!mrDir.mInvalidated);
#endif // !BOX_RELEASE_BUILD
			// Skip over things which don't match the required flags or filename
			while( (i != mrDir.mEntries.end())
				&& ( (!(*i)->MatchesFlags(FlagsMustBeSet, FlagsNotToBeSet))
					|| (BackupStoreFilenameClear((*i)->GetName()).GetClearFilename() != rFilename.GetClearFilename()) ) )
			{
				++i;
			}
			// Not the last one?
			if(i == mrDir.mEntries.end())
			{
				return 0;
			}
			// Return entry, and increment
			return (*(i++));
		}

	private:
		const BackupStoreDirectory &mrDir;
		std::vector<Entry*>::const_iterator i;
	};

	friend class Iterator;

	class ReverseIterator
	{
	public:
		ReverseIterator(const BackupStoreDirectory &rDir)
			: mrDir(rDir), i(rDir.mEntries.rbegin())
		{
#ifndef BOX_RELEASE_BUILD
			ASSERT(!mrDir.mInvalidated);
#endif // !BOX_RELEASE_BUILD
		}

		BackupStoreDirectory::Entry *Next(int16_t FlagsMustBeSet = Entry::Flags_INCLUDE_EVERYTHING, int16_t FlagsNotToBeSet = Entry::Flags_EXCLUDE_NOTHING)
		{
#ifndef BOX_RELEASE_BUILD
			ASSERT(!mrDir.mInvalidated);
#endif // !BOX_RELEASE_BUILD
			// Skip over things which don't match the required flags
			while(i != mrDir.mEntries.rend() && !(*i)->MatchesFlags(FlagsMustBeSet, FlagsNotToBeSet))
			{
				++i;
			}
			// Not the last one?
			if(i == mrDir.mEntries.rend())
			{
				return 0;
			}
			// Return entry, and increment
			return (*(i++));
		}

	private:
		const BackupStoreDirectory &mrDir;
		std::vector<Entry*>::const_reverse_iterator i;
	};

	friend class ReverseIterator;

	// For recovery of the store
	// Implemented in BackupStoreCheck2.cpp
	bool CheckAndFix();
	void AddUnattachedObject(const BackupStoreFilename &rName,
		box_time_t ModificationTime, int64_t ObjectID,
		int64_t SizeInBlocks, int16_t Flags);
	bool NameInUse(const BackupStoreFilename &rName);

	// Be very careful with SetObjectID! It's intended for use in
	// BackupStoreContext::MakeUnique only.
	void SetObjectID(int64_t ObjectID) {mObjectID = ObjectID;}
	// Debug and diagnostics
	void Dump(void *clibFileHandle, bool ToTrace); // first arg is FILE *, but avoid including stdio.h everywhere

private:
	int64_t mRevisionID;
	int64_t mObjectID;
	int64_t mContainerID;
	std::vector<Entry*> mEntries;
	box_time_t mAttributesModTime;
	StreamableMemBlock mAttributes;
	int64_t mUserInfo1;
};

#endif // BACKUPSTOREDIRECTORY__H
