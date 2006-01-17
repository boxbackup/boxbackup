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
//		Name:    BackupClientInodeToIDMap.h
//		Purpose: Map of inode numbers to file IDs on the store
//		Created: 11/11/03
//
// --------------------------------------------------------------------------

#ifndef BACKUPCLIENTINODETOIDMAP_H
#define BACKUPCLIENTINODETOIDMAP__H

#include <sys/types.h>

#include <map>
#include <utility>

// Use in memory implementation if there isn't access to the Berkely DB on this platform
#ifdef PLATFORM_BERKELEY_DB_NOT_SUPPORTED
	#define BACKIPCLIENTINODETOIDMAP_IN_MEMORY_IMPLEMENTATION
#endif

typedef ino_t InodeRefType;

// avoid having to include the DB files when not necessary
#ifndef BACKIPCLIENTINODETOIDMAP_IMPLEMENTATION
	class DB;
#endif

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupClientInodeToIDMap
//		Purpose: Map of inode numbers to file IDs on the store
//		Created: 11/11/03
//
// --------------------------------------------------------------------------
class BackupClientInodeToIDMap
{
public:
	BackupClientInodeToIDMap();
	~BackupClientInodeToIDMap();
private:
	BackupClientInodeToIDMap(const BackupClientInodeToIDMap &rToCopy);	// not allowed
public:

	void Open(const char *Filename, bool ReadOnly, bool CreateNew);
	void OpenEmpty();

	void AddToMap(InodeRefType InodeRef, int64_t ObjectID, int64_t InDirectory);
	bool Lookup(InodeRefType InodeRef, int64_t &rObjectIDOut, int64_t &rInDirectoryOut) const;

	void Close();

private:
#ifdef BACKIPCLIENTINODETOIDMAP_IN_MEMORY_IMPLEMENTATION
	std::map<InodeRefType, std::pair<int64_t, int64_t> > mMap;
#else
	bool mReadOnly;
	bool mEmpty;
	DB *dbp;	// C style interface, use notation from documentation
#endif
};

#endif // BACKUPCLIENTINODETOIDMAP__H


