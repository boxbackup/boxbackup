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
//		Name:    BackupStoreObjectMagic.h
//		Purpose: Magic values for the start of objects in the backup store
//		Created: 19/11/03
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREOBJECTMAGIC__H
#define BACKUPSTOREOBJECTMAGIC__H

// Each of these values is the first 4 bytes of the object file.
// Remember to swap from network to host byte order.

// Magic value for file streams
#define OBJECTMAGIC_FILE_MAGIC_VALUE_V1		0x66696C65
// Do not use v0 in any new code!
#define OBJECTMAGIC_FILE_MAGIC_VALUE_V0		0x46494C45

// Magic for the block index at the file stream -- used to
// ensure streams are reordered as expected
#define OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1 0x62696478
// Do not use v0 in any new code!
#define OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V0 0x46426C6B

// Magic value for directory streams
#define OBJECTMAGIC_DIR_MAGIC_VALUE 		0x4449525F

#endif // BACKUPSTOREOBJECTMAGIC__H

