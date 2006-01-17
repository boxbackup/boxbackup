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
//		Name:    RaidFileWrite.h
//		Purpose: Writing RAID like files
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------

#ifndef RAIDFILEWRITE__H
#define RAIDFILEWRITE__H

#include <string>

#include "IOStream.h"

class RaidFileDiscSet;

// --------------------------------------------------------------------------
//
// Class
//		Name:    RaidFileWrite
//		Purpose: Writing RAID like files
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
class RaidFileWrite : public IOStream
{
public:
	RaidFileWrite(int SetNumber, const std::string &Filename);
	~RaidFileWrite();
private:
	RaidFileWrite(const RaidFileWrite &rToCopy);

public:
	// IOStream interface
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);	// will exception
	virtual void Write(const void *pBuffer, int NBytes);
	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, int SeekType);
	virtual void Close();		// will discard the file! Use commit instead.
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

	// Extra bits
	void Open(bool AllowOverwrite = false);
	void Commit(bool ConvertToRaidNow = false);
	void Discard();
	void TransformToRaidStorage();
	void Delete();
	pos_type GetFileSize();
	pos_type GetDiscUsageInBlocks();
	
	static void CreateDirectory(int SetNumber, const std::string &rDirName, bool Recursive = false, int mode = 0777);
	static void CreateDirectory(const RaidFileDiscSet &rSet, const std::string &rDirName, bool Recursive = false, int mode = 0777);
	
private:

private:
	int mSetNumber;
	std::string mFilename;
	int mOSFileHandle;
};

#endif // RAIDFILEWRITE__H

