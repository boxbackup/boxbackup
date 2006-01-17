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
//		Name:    bbackupobjdump.cpp
//		Purpose: Dump contents of backup objects
//		Created: 3/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include "MainHelper.h"
#include "FileStream.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreFile.h"
#include "BackupStoreObjectMagic.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    int main(int, const char *[])
//		Purpose: Main fn for bbackupobjdump
//		Created: 3/5/04
//
// --------------------------------------------------------------------------
int main(int argc, const char *argv[])
{
	MAINHELPER_START

	if(argc != 2)
	{
		::printf("Input file not specified.\nUsage: bbackupobjdump <input file>\n");
		return 1;
	}

	// Open file
	FileStream file(argv[1]);
	
	// Read magic number
	uint32_t signature;
	if(file.Read(&signature, sizeof(signature)) != sizeof(signature))
	{
		// Too short, can't read signature from it
		return false;
	}
	// Seek back to beginning
	file.Seek(0, IOStream::SeekType_Absolute);
	
	// Then... check depending on the type
	switch(ntohl(signature))
	{
	case OBJECTMAGIC_FILE_MAGIC_VALUE_V1:
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
	case OBJECTMAGIC_FILE_MAGIC_VALUE_V0:
#endif
		BackupStoreFile::DumpFile(stdout, false, file);
		break;

	case OBJECTMAGIC_DIR_MAGIC_VALUE:
		{
			BackupStoreDirectory dir;
			dir.ReadFromStream(file, IOStream::TimeOutInfinite);
			dir.Dump(stdout, false);
			if(dir.CheckAndFix())
			{
				::printf("Directory didn't pass checking\n");
			}
		}
		break;

	default:
		::printf("File does not appear to be a valid box backup object.\n");
		break;
	}

	MAINHELPER_END
}

