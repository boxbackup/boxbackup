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
public:
	BackupStoreDirectory();
	BackupStoreDirectory(int64_t ObjectID, int64_t ContainerID);
private:
	// Copying not allowed
	BackupStoreDirectory(const BackupStoreDirectory &rToCopy);
public:
	~BackupStoreDirectory();

	class Entry
	{
	public:
		friend class BackupStoreDirectory;

		Entry();
		~Entry();
		Entry(const Entry &rToCopy);
		Entry(const BackupStoreFilename &rName, box_time_t ModificationTime, int64_t ObjectID, int64_t SizeInBlocks, int16_t Flags, uint64_t AttributesHash);
		
		void ReadFromStream(IOStream &rStream, int Timeout);
		void WriteToStream(IOStream &rStream) const;
		
		const BackupStoreFilename &GetName() const {return mName;}
		box_time_t GetModificationTime() const {return mModificationTime;}
		int64_t GetObjectID() const {return mObjectID;}
		int64_t GetSizeInBlocks() const {return mSizeInBlocks;}
		int16_t GetFlags() const {return mFlags;}
		void AddFlags(int16_t Flags) {mFlags |= Flags;}
		void RemoveFlags(int16_t Flags) {mFlags &= ~Flags;}

		// Some things can be changed
		void SetName(const BackupStoreFilename &rNewName) {mName = rNewName;}
		void SetSizeInBlocks(int64_t SizeInBlocks) {mSizeInBlocks = SizeInBlocks;}

		// Attributes
		bool HasAttributes() const {return !mAttributes.IsEmpty();}
		void SetAttributes(const StreamableMemBlock &rAttr, uint64_t AttributesHash) {mAttributes.Set(rAttr); mAttributesHash = AttributesHash;}
		const StreamableMemBlock &GetAttributes() const {return mAttributes;}
		uint64_t GetAttributesHash() const {return mAttributesHash;}
		
		// Marks
		// The lowest mark number a version of a file of this name has ever had
		uint32_t GetMinMarkNumber() const {return mMinMarkNumber;}
		// The mark number on this file
		uint32_t GetMarkNumber() const {return mMarkNumber;}

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
		
		bool inline MatchesFlags(int16_t FlagsMustBeSet, int16_t FlagsNotToBeSet)
		{
			return ((FlagsMustBeSet == Flags_INCLUDE_EVERYTHING) || ((mFlags & FlagsMustBeSet) == FlagsMustBeSet))
				&& ((mFlags & FlagsNotToBeSet) == 0);
		};

		// Get dependency info
		// new version this depends on
		int64_t GetDependsNewer() const {return mDependsNewer;}
		void SetDependsNewer(int64_t ObjectID) {mDependsNewer = ObjectID;}
		// older version which depends on this
		int64_t GetDependsOlder() const {return mDependsOlder;}
		void SetDependsOlder(int64_t ObjectID) {mDependsOlder = ObjectID;}

		// Dependency info saving
		bool HasDependencies() {return mDependsNewer != 0 || mDependsOlder != 0;}
		void ReadFromStreamDependencyInfo(IOStream &rStream, int Timeout);
		void WriteToStreamDependencyInfo(IOStream &rStream) const;

	private:
		BackupStoreFilename	mName;
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
	
	void ReadFromStream(IOStream &rStream, int Timeout);
	void WriteToStream(IOStream &rStream,
			int16_t FlagsMustBeSet = Entry::Flags_INCLUDE_EVERYTHING,
			int16_t FlagsNotToBeSet = Entry::Flags_EXCLUDE_NOTHING,
			bool StreamAttributes = true, bool StreamDependencyInfo = true) const;
			
	Entry *AddEntry(const Entry &rEntryToCopy);
	Entry *AddEntry(const BackupStoreFilename &rName, box_time_t ModificationTime, int64_t ObjectID, int64_t SizeInBlocks, int16_t Flags, box_time_t AttributesModTime);
	void DeleteEntry(int64_t ObjectID);
	Entry *FindEntryByID(int64_t ObjectID) const;
	
	int64_t GetObjectID() const {return mObjectID;}
	int64_t GetContainerID() const {return mContainerID;}
	
	// Need to be able to update the container ID when moving objects
	void SetContainerID(int64_t ContainerID) {mContainerID = ContainerID;}

	// Purely for use of server -- not serialised into streams	
	int64_t GetRevisionID() const {return mRevisionID;}
	void SetRevisionID(int64_t RevisionID) {mRevisionID = RevisionID;}
	
	unsigned int GetNumberOfEntries() const {return mEntries.size();}

	// User info -- not serialised into streams
	int64_t GetUserInfo1_SizeInBlocks() const {return mUserInfo1;}
	void SetUserInfo1_SizeInBlocks(int64_t UserInfo1) {mUserInfo1 = UserInfo1;}

	// Attributes
	bool HasAttributes() const {return !mAttributes.IsEmpty();}
	void SetAttributes(const StreamableMemBlock &rAttr, box_time_t AttributesModTime) {mAttributes.Set(rAttr); mAttributesModTime = AttributesModTime;}
	const StreamableMemBlock &GetAttributes() const {return mAttributes;}
	box_time_t GetAttributesModTime() const {return mAttributesModTime;}

	class Iterator
	{
	public:
		Iterator(const BackupStoreDirectory &rDir)
			: mrDir(rDir), i(rDir.mEntries.begin())
		{
		}
		
		BackupStoreDirectory::Entry *Next(int16_t FlagsMustBeSet = Entry::Flags_INCLUDE_EVERYTHING, int16_t FlagsNotToBeSet = Entry::Flags_EXCLUDE_NOTHING)
		{
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
		}
		
		BackupStoreDirectory::Entry *Next(int16_t FlagsMustBeSet = Entry::Flags_INCLUDE_EVERYTHING, int16_t FlagsNotToBeSet = Entry::Flags_EXCLUDE_NOTHING)
		{
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
	void AddUnattactedObject(const BackupStoreFilename &rName, box_time_t ModificationTime, int64_t ObjectID, int64_t SizeInBlocks, int16_t Flags);
	bool NameInUse(const BackupStoreFilename &rName);
	// Don't use these functions in normal code!

	// For testing
	void TESTONLY_SetObjectID(int64_t ObjectID) {mObjectID = ObjectID;}

	// Debug and diagonistics
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

