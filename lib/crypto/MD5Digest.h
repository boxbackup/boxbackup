// --------------------------------------------------------------------------
//
// File
//		Name:    MD5Digest.h
//		Purpose: Simple interface for creating MD5 digests
//		Created: 8/12/03
//
// --------------------------------------------------------------------------

#ifndef MD5DIGEST_H
#define MD5DIGEST_H

#include <openssl/md5.h>
#include <string>

// --------------------------------------------------------------------------
//
// Function
//		Name:    MD5Digest
//		Purpose: Simple interface for creating MD5 digests
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
class MD5Digest  
{
public:
	MD5Digest();
	virtual ~MD5Digest();

	void Add(const std::string &rString);
	void Add(const void *pData, int Length);

	void Finish();

	std::string DigestAsString();
	uint8_t *DigestAsData(int *pLength = 0)
	{
		if(pLength) *pLength = sizeof(mDigest);
		return mDigest;
	}

	enum
	{
		DigestLength = MD5_DIGEST_LENGTH
	};

	int CopyDigestTo(uint8_t *to);

	bool DigestMatches(uint8_t *pCompareWith) const;

private:
	MD5_CTX	md5;
	uint8_t mDigest[MD5_DIGEST_LENGTH];
};

#endif // MD5DIGEST_H

