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
//		Name:    MD5Digest.cpp
//		Purpose: Simple interface for creating MD5 digests
//		Created: 8/12/03
//
// --------------------------------------------------------------------------


#include "Box.h"

#include "MD5Digest.h"

#include "MemLeakFindOn.h"


MD5Digest::MD5Digest()
{
	MD5_Init(&md5);
	for(unsigned int l = 0; l < sizeof(mDigest); ++l)
	{
		mDigest[l] = 0;
	}
}

MD5Digest::~MD5Digest()
{
}

void MD5Digest::Add(const std::string &rString)
{
	MD5_Update(&md5, rString.c_str(), rString.size());
}

void MD5Digest::Add(const void *pData, int Length)
{
	MD5_Update(&md5, pData, Length);
}

void MD5Digest::Finish()
{
	MD5_Final(mDigest, &md5);
}

std::string MD5Digest::DigestAsString()
{
	std::string r;

	static const char *hex = "0123456789abcdef";

	for(unsigned int l = 0; l < sizeof(mDigest); ++l)
	{
		r += hex[(mDigest[l] & 0xf0) >> 4];
		r += hex[(mDigest[l] & 0x0f)];
	}

	return r;
}

int MD5Digest::CopyDigestTo(uint8_t *to)
{
	for(int l = 0; l < MD5_DIGEST_LENGTH; ++l)
	{
		to[l] = mDigest[l];
	}

	return MD5_DIGEST_LENGTH;
}


bool MD5Digest::DigestMatches(uint8_t *pCompareWith) const
{
	for(int l = 0; l < MD5_DIGEST_LENGTH; ++l)
	{
		if(pCompareWith[l] != mDigest[l])
			return false;
	}

	return true;
}

