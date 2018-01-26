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

#include "BackupStoreFilenameClear.h"
#include "StreamableMemBlock.h"
#include "BoxTime.h"

class IOStream;

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
	void Invalidate(bool invalid = true)
	{
		mInvalidated = invalid;
		for (std::vector<Entry*>::iterator i = mEntries.begin();
			i != mEntries.end(); i++)
		{
			(*i)->Invalidate(invalid);
		}
	}
#endif

	typedef enum
	{
		Option_DependencyInfoPresent = 1
	} dir_StreamFormatOptions;

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
		void Invalidate(bool invalid) { mInvalidated = invalid; }
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
			ASSERT(!mInvalidated); // Compiled out of release builds
			return mName;
		}
		box_time_t GetModificationTime() const
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return mModificationTime;
		}
		int64_t GetObjectID() const
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return mObjectID;
		}
		// SetObjectID is dangerous! It should only be used when
		// creating a snapshot.
		void SetObjectID(int64_t NewObjectID)
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			mObjectID = NewObjectID;
		}
		int64_t GetSizeInBlocks() const
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return mSizeInBlocks;
		}
		int16_t GetFlags() const
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return mFlags;
		}
		void AddFlags(int16_t Flags)
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			mFlags |= Flags;
		}
		void RemoveFlags(int16_t Flags)
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			mFlags &= ~Flags;
		}

		// Some things can be changed
		void SetName(const BackupStoreFilename &rNewName)
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			mName = rNewName;
		}
		void SetSizeInBlocks(int64_t SizeInBlocks)
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			mSizeInBlocks = SizeInBlocks;
		}

		// Attributes
		bool HasAttributes() const
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return !mAttributes.IsEmpty();
		}
		void SetAttributes(const StreamableMemBlock &rAttr, uint64_t AttributesHash)
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			mAttributes.Set(rAttr);
			mAttributesHash = AttributesHash;
		}
		const StreamableMemBlock &GetAttributes() const
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return mAttributes;
		}
		uint64_t GetAttributesHash() const
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return mAttributesHash;
		}

		// Marks
		// The lowest mark number a version of a file of this name has ever had
		uint32_t GetMinMarkNumber() const
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return mMinMarkNumber;
		}
		// The mark number on this file
		uint32_t GetMarkNumber() const
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
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
			ASSERT(!mInvalidated); // Compiled out of release builds
			return GetFlags() & Flags_Dir;
		}
		bool inline IsFile()
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return GetFlags() & Flags_File;
		}
		bool inline IsOld()
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return GetFlags() & Flags_OldVersion;
		}
		bool inline IsDeleted()
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return GetFlags() & Flags_Deleted;
		}
		bool inline MatchesFlags(int16_t FlagsMustBeSet, int16_t FlagsNotToBeSet)
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return ((FlagsMustBeSet == Flags_INCLUDE_EVERYTHING) || ((mFlags & FlagsMustBeSet) == FlagsMustBeSet))
				&& ((mFlags & FlagsNotToBeSet) == 0);
		};

		// Get dependency info
		// new version this depends on
		int64_t GetDependsNewer() const
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return mDependsNewer;
		}
		void SetDependsNewer(int64_t ObjectID)
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			mDependsNewer = ObjectID;
		}
		// older version which depends on this
		int64_t GetDependsOlder() const
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			return mDependsOlder;
		}
		void SetDependsOlder(int64_t ObjectID)
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
			mDependsOlder = ObjectID;
		}

		// Dependency info saving
		bool HasDependencies()
		{
			ASSERT(!mInvalidated); // Compiled out of release builds
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

	int64_t GetObjectID() const
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		return mObjectID;
	}

	int64_t GetContainerID() const
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		return mContainerID;
	}
	// Need to be able to update the container ID when moving objects
	void SetContainerID(int64_t ContainerID)
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		mContainerID = ContainerID;
	}

	// Purely for use of server -- not serialised into streams
	int64_t GetRevisionID() const
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		return mRevisionID;
	}
	void SetRevisionID(int64_t RevisionID)
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		mRevisionID = RevisionID;
	}

	unsigned int GetNumberOfEntries() const
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		return mEntries.size();
	}

	// User info -- not serialised into streams
	int64_t GetUserInfo1_SizeInBlocks() const
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		return mUserInfo1;
	}
	void SetUserInfo1_SizeInBlocks(int64_t UserInfo1)
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		mUserInfo1 = UserInfo1;
	}

	// Attributes
	bool HasAttributes() const
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		return !mAttributes.IsEmpty();
	}
	void SetAttributes(const StreamableMemBlock &rAttr,
		box_time_t AttributesModTime)
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		mAttributes.Set(rAttr);
		mAttributesModTime = AttributesModTime;
	}
	const StreamableMemBlock &GetAttributes() const
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		return mAttributes;
	}
	box_time_t GetAttributesModTime() const
	{
		ASSERT(!mInvalidated); // Compiled out of release builds
		return mAttributesModTime;
	}

	class Iterator
	{
	public:
		Iterator(const BackupStoreDirectory &rDir)
			: mrDir(rDir), i(rDir.mEntries.begin())
		{
			ASSERT(!mrDir.mInvalidated); // Compiled out of release builds
		}

		BackupStoreDirectory::Entry *Next(int16_t FlagsMustBeSet = Entry::Flags_INCLUDE_EVERYTHING, int16_t FlagsNotToBeSet = Entry::Flags_EXCLUDE_NOTHING)
		{
			ASSERT(!mrDir.mInvalidated); // Compiled out of release builds
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
			ASSERT(!mrDir.mInvalidated); // Compiled out of release builds
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
			ASSERT(!mrDir.mInvalidated); // Compiled out of release builds
		}

		BackupStoreDirectory::Entry *Next(int16_t FlagsMustBeSet = Entry::Flags_INCLUDE_EVERYTHING, int16_t FlagsNotToBeSet = Entry::Flags_EXCLUDE_NOTHING)
		{
			ASSERT(!mrDir.mInvalidated); // Compiled out of release builds
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

	// For testing
	// Don't use these functions in normal code!
	void TESTONLY_SetObjectID(int64_t ObjectID) {mObjectID = ObjectID;}
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
