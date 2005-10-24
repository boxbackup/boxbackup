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

