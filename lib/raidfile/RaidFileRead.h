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
//		Name:    RaidFileRead.h
//		Purpose: Read Raid like Files
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------

#ifndef RAIDFILEREAD__H
#define RAIDFILEREAD__H

#include <string>
#include <memory>
#include <vector>

#include "IOStream.h"

class RaidFileDiscSet;


// --------------------------------------------------------------------------
//
// Class
//		Name:    RaidFileRead
//		Purpose: Read RAID like files
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
class RaidFileRead : public IOStream
{
protected:
	RaidFileRead(int SetNumber, const std::string &Filename);
public:
	virtual ~RaidFileRead();
private:
	RaidFileRead(const RaidFileRead &rToCopy);
	
public:
	// Open a raid file
	static std::auto_ptr<RaidFileRead> Open(int SetNumber, const std::string &Filename, int64_t *pRevisionID = 0, int BufferSizeHint = 4096);

	// Extra info
	virtual pos_type GetFileSize() const = 0;

	// Utility functions
	static bool FileExists(int SetNumber, const std::string &rFilename, int64_t *pRevisionID = 0);
	static bool DirectoryExists(const RaidFileDiscSet &rSet, const std::string &rDirName);
	static bool DirectoryExists(int SetNumber, const std::string &rDirName);
	enum
	{
		DirReadType_FilesOnly = 0,
		DirReadType_DirsOnly = 1
	};
	static bool ReadDirectoryContents(int SetNumber, const std::string &rDirName, int DirReadType, std::vector<std::string> &rOutput);

	// Common IOStream interface implementation
	virtual void Write(const void *pBuffer, int NBytes);
	virtual bool StreamClosed();
	virtual pos_type BytesLeftToRead();

	pos_type GetDiscUsageInBlocks();

	typedef int64_t FileSizeType;

protected:
	int mSetNumber;
	std::string mFilename;
};

#endif // RAIDFILEREAD__H

