// --------------------------------------------------------------------------
//
// File
//		Name:    testcrypto.cpp
//		Purpose: test lib/crypto
//		Created: 1/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>
#include <openssl/rand.h>

#include "CipherContext.h"
#include "CipherBlowfish.h"
#include "CipherAES.h"
#include "CipherException.h"
#include "CollectInBufferStream.h"
#include "Guards.h"
#include "RollingChecksum.h"
#include "Random.h"
#include "Test.h"

#include "MemLeakFindOn.h"

#define STRING1	"Mary had a little lamb"
#define STRING2 "Skjdf sdjf sjksd fjkhsdfjk hsdfuiohcverfg sdfnj sdfgkljh sdfjb jlhdfvghsdip vjsdfv bsdfhjvg yuiosdvgpvj kvbn m,sdvb sdfuiovg sdfuivhsdfjkv"

#define KEY "0123456701234567012345670123456"
#define KEY2 "1234567012345670123456A"

#define CHECKSUM_DATA_SIZE			(128*1024)
#define CHECKSUM_BLOCK_SIZE_BASE	(65*1024)
#define CHECKSUM_BLOCK_SIZE_LAST	(CHECKSUM_BLOCK_SIZE_BASE + 64)
#define CHECKSUM_ROLLS				16

// Copied from BackupClientCryptoKeys.h
#define BACKUPCRYPTOKEYS_FILENAME_KEY_START				0
#define BACKUPCRYPTOKEYS_FILENAME_KEY_LENGTH			56
#define BACKUPCRYPTOKEYS_FILENAME_IV_START				(0 + BACKUPCRYPTOKEYS_FILENAME_KEY_LENGTH)
#define BACKUPCRYPTOKEYS_FILENAME_IV_LENGTH				8

void check_random_int(uint32_t max)
{
	for(int c = 0; c < 1024; ++c)
	{
		uint32_t v = Random::RandomInt(max);
		TEST_THAT(v >= 0 && v <= max);
	}
}

#define ZERO_BUFFER(x) ::memset(x, 0, sizeof(x));

template<typename CipherType, int BLOCKSIZE>
void test_cipher()
{
	{
		// Make a couple of cipher contexts
		CipherContext encrypt1;
		encrypt1.Reset();
		encrypt1.Init(CipherContext::Encrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY)));
		TEST_CHECK_THROWS(encrypt1.Init(CipherContext::Encrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY))),
			CipherException, AlreadyInitialised);

		// We must always set the IV before each call to TransformBlock:
		char iv[16] = {1,2,3,4,5,6,7,8};
		ASSERT((size_t)encrypt1.GetIVLength() <= sizeof(iv));
		encrypt1.SetIV(iv);

		// Encrypt something
		char buf1[256];
		unsigned int buf1_used = encrypt1.TransformBlock(buf1, sizeof(buf1), STRING1, sizeof(STRING1));
		TEST_THAT(buf1_used >= sizeof(STRING1));

		// Decrypt it
		CipherContext decrypt1;
		decrypt1.Init(CipherContext::Decrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY)));
		// We must always set the IV before each call to TransformBlock:
		decrypt1.SetIV(iv);
		char buf1_de[256];
		unsigned int buf1_de_used = decrypt1.TransformBlock(buf1_de, sizeof(buf1_de), buf1, buf1_used);
		TEST_THAT(buf1_de_used == sizeof(STRING1));
		TEST_THAT(memcmp(STRING1, buf1_de, sizeof(STRING1)) == 0);
		
		// Use them again...
		char buf1_de2[256];
		// We must always set the IV before each call to TransformBlock:
		decrypt1.SetIV(iv);
		unsigned int buf1_de2_used = decrypt1.TransformBlock(buf1_de2, sizeof(buf1_de2), buf1, buf1_used);
		TEST_THAT(buf1_de2_used == sizeof(STRING1));
		TEST_THAT(memcmp(STRING1, buf1_de2, sizeof(STRING1)) == 0);
		
		// Test the Transform() interface:
		char buf2[256];
		TEST_CHECK_THROWS(encrypt1.Transform(buf2, sizeof(buf2), STRING1, sizeof(STRING1)),
			CipherException, BeginNotCalled);
		TEST_CHECK_THROWS(encrypt1.Final(buf2, sizeof(buf2)),
			CipherException, BeginNotCalled);
		encrypt1.Begin();
		int e = 0;
		e = encrypt1.Transform(buf2, sizeof(buf2), STRING2, sizeof(STRING2) - 16);
		e += encrypt1.Transform(buf2 + e, sizeof(buf2) - e, STRING2 + sizeof(STRING2) - 16, 16);
		e += encrypt1.Final(buf2 + e, sizeof(buf2) - e);
		TEST_THAT(e >= (int)sizeof(STRING2));
		
		// Then decrypt
		char buf2_de[256];
		decrypt1.Begin();
		TEST_CHECK_THROWS(decrypt1.Transform(buf2_de, 2, buf2, e), CipherException, OutputBufferTooSmall);
		TEST_CHECK_THROWS(decrypt1.Final(buf2_de, 2), CipherException, OutputBufferTooSmall);
		int d = decrypt1.Transform(buf2_de, sizeof(buf2_de), buf2, e - 48);
		d += decrypt1.Transform(buf2_de + d, sizeof(buf2_de) - d, buf2 + e - 48, 48);
		d += decrypt1.Final(buf2_de + d, sizeof(buf2_de) - d);
		TEST_THAT(d == sizeof(STRING2));
		TEST_THAT(memcmp(STRING2, buf2_de, sizeof(STRING2)) == 0);
		
		// Try a reset and rekey
		encrypt1.Reset();
		encrypt1.Init(CipherContext::Encrypt, CipherType(CipherDescription::Mode_CBC, KEY2, sizeof(KEY2)));
		// We must always set the IV before each call to TransformBlock:
		encrypt1.SetIV(iv);
		buf1_used = encrypt1.TransformBlock(buf1, sizeof(buf1), STRING1, sizeof(STRING1));

		// Decrypt it
		decrypt1.Reset();
		decrypt1.Init(CipherContext::Decrypt, CipherType(CipherDescription::Mode_CBC, KEY2, sizeof(KEY2)));
		// We must always set the IV before each call to TransformBlock:
		decrypt1.SetIV(iv);
		memset(buf1_de, 0, sizeof(buf1_de));
		buf1_de_used = decrypt1.TransformBlock(buf1_de, sizeof(buf1_de), buf1, buf1_used);
		TEST_THAT(buf1_de_used == sizeof(STRING1));
		TEST_THAT(memcmp(STRING1, buf1_de, sizeof(STRING1)) == 0);
	}
	
	// Test initialisation vectors
	{
		// Init with random IV
		CipherContext encrypt2;
		encrypt2.Init(CipherContext::Encrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY)));
		int ivLen;
		char iv2[BLOCKSIZE];
		const void *ivGen = encrypt2.SetRandomIV(ivLen);
		TEST_THAT(ivLen == BLOCKSIZE);	// block size
		TEST_THAT(ivGen != 0);
		memcpy(iv2, ivGen, ivLen);
		
		char buf3[256];
		unsigned int buf3_used = encrypt2.TransformBlock(buf3, sizeof(buf3), STRING2, sizeof(STRING2));
		
		// Encrypt again with different IV
		char iv3[BLOCKSIZE];
		int ivLen3;
		const void *ivGen3 = encrypt2.SetRandomIV(ivLen3);
		TEST_THAT(ivLen3 == BLOCKSIZE);	// block size
		TEST_THAT(ivGen3 != 0);
		memcpy(iv3, ivGen3, ivLen3);
		// Check the two generated IVs are different
		TEST_THAT(memcmp(iv2, iv3, BLOCKSIZE) != 0);

		char buf4[256];
		unsigned int buf4_used = encrypt2.TransformBlock(buf4, sizeof(buf4), STRING2, sizeof(STRING2));

		// check encryptions are different
		TEST_THAT(buf3_used == buf4_used);
		TEST_THAT(memcmp(buf3, buf4, buf3_used) != 0);
		
		// Test that decryption with the right IV works
		CipherContext decrypt2;
		decrypt2.Init(CipherContext::Decrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY), iv2));
		char buf3_de[256];
		unsigned int buf3_de_used = decrypt2.TransformBlock(buf3_de, sizeof(buf3_de), buf3, buf3_used);
		TEST_THAT(buf3_de_used == sizeof(STRING2));
		TEST_THAT(memcmp(STRING2, buf3_de, sizeof(STRING2)) == 0);
		
		// And that using the wrong one doesn't
		decrypt2.SetIV(iv3);
		buf3_de_used = decrypt2.TransformBlock(buf3_de, sizeof(buf3_de), buf3, buf3_used);
		TEST_THAT(buf3_de_used == sizeof(STRING2));
		TEST_THAT(memcmp(STRING2, buf3_de, sizeof(STRING2)) != 0);		
	}
	
	// Test with padding off.
	{
		CipherContext encrypt3;
		encrypt3.Init(CipherContext::Encrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY)));
		encrypt3.UsePadding(false);
		
		// Should fail because the encrypted size is not a multiple of the block size
		char buf4[256];
		encrypt3.Begin();
		ZERO_BUFFER(buf4);
		int buf4_used = encrypt3.Transform(buf4, sizeof(buf4), STRING2, 6);
		TEST_CHECK_THROWS(encrypt3.Final(buf4, sizeof(buf4)), CipherException, EVPFinalFailure);
		
		// Check a nice encryption with the correct block size
		CipherContext encrypt4;
		encrypt4.Init(CipherContext::Encrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY)));
		encrypt4.UsePadding(false);
		encrypt4.Begin();
		ZERO_BUFFER(buf4);
		buf4_used = encrypt4.Transform(buf4, sizeof(buf4), STRING2, 16);
		buf4_used += encrypt4.Final(buf4+buf4_used, sizeof(buf4));
		TEST_THAT(buf4_used == 16);

		// Check it's encrypted to the same thing as when there's padding on
		CipherContext encrypt4b;
		encrypt4b.Init(CipherContext::Encrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY)));
		encrypt4b.Begin();
		char buf4b[256];
		ZERO_BUFFER(buf4b);
		int buf4b_used = encrypt4b.Transform(buf4b, sizeof(buf4b), STRING2, 16);
		buf4b_used += encrypt4b.Final(buf4b + buf4b_used, sizeof(buf4b));
		TEST_THAT(buf4b_used == 16+BLOCKSIZE);
		TEST_THAT(::memcmp(buf4, buf4b, 16) == 0);

		// Decrypt
		char buf4_de[256];
		CipherContext decrypt4;
		decrypt4.Init(CipherContext::Decrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY)));
		decrypt4.UsePadding(false);
		decrypt4.Begin();
		ZERO_BUFFER(buf4_de);
		int buf4_de_used = decrypt4.Transform(buf4_de, sizeof(buf4_de), buf4, 16);
		buf4_de_used += decrypt4.Final(buf4_de+buf4_de_used, sizeof(buf4_de));
		TEST_THAT(buf4_de_used == 16);
		TEST_THAT(::memcmp(buf4_de, STRING2, 16) == 0);
		
		// Test that the TransformBlock thing works as expected too with blocks the same size as the input
		TEST_THAT(encrypt4.TransformBlock(buf4, 16, STRING2, 16) == 16);
		// But that it exceptions if we try the trick with padding on
		encrypt4.UsePadding(true);
		TEST_CHECK_THROWS(encrypt4.TransformBlock(buf4, 16, STRING2, 16), CipherException, OutputBufferTooSmall);
	}

	// And again, but with different string size
	{
		char buf4[256];
		int buf4_used;

		// Check a nice encryption with the correct block size
		CipherContext encrypt4;
		encrypt4.Init(CipherContext::Encrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY)));
		encrypt4.UsePadding(false);
		encrypt4.Begin();
		ZERO_BUFFER(buf4);
		buf4_used = encrypt4.Transform(buf4, sizeof(buf4), STRING2, (BLOCKSIZE*3)); // do three blocks worth
		buf4_used += encrypt4.Final(buf4+buf4_used, sizeof(buf4));
		TEST_THAT(buf4_used == (BLOCKSIZE*3));

		// Check it's encrypted to the same thing as when there's padding on
		CipherContext encrypt4b;
		encrypt4b.Init(CipherContext::Encrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY)));
		encrypt4b.Begin();
		char buf4b[256];
		ZERO_BUFFER(buf4b);
		int buf4b_used = encrypt4b.Transform(buf4b, sizeof(buf4b), STRING2, (BLOCKSIZE*3));
		buf4b_used += encrypt4b.Final(buf4b + buf4b_used, sizeof(buf4b));
		TEST_THAT(buf4b_used == (BLOCKSIZE*4));
		TEST_THAT(::memcmp(buf4, buf4b, (BLOCKSIZE*3)) == 0);

		// Decrypt
		char buf4_de[256];
		CipherContext decrypt4;
		decrypt4.Init(CipherContext::Decrypt, CipherType(CipherDescription::Mode_CBC, KEY, sizeof(KEY)));
		decrypt4.UsePadding(false);
		decrypt4.Begin();
		ZERO_BUFFER(buf4_de);
		int buf4_de_used = decrypt4.Transform(buf4_de, sizeof(buf4_de), buf4, (BLOCKSIZE*3));
		buf4_de_used += decrypt4.Final(buf4_de+buf4_de_used, sizeof(buf4_de));
		TEST_THAT(buf4_de_used == (BLOCKSIZE*3));
		TEST_THAT(::memcmp(buf4_de, STRING2, (BLOCKSIZE*3)) == 0);
		
		// Test that the TransformBlock thing works as expected too with blocks the same size as the input
		TEST_THAT(encrypt4.TransformBlock(buf4, (BLOCKSIZE*3), STRING2, (BLOCKSIZE*3)) == (BLOCKSIZE*3));
		// But that it exceptions if we try the trick with padding on
		encrypt4.UsePadding(true);
		TEST_CHECK_THROWS(encrypt4.TransformBlock(buf4, (BLOCKSIZE*3), STRING2, (BLOCKSIZE*3)), CipherException, OutputBufferTooSmall);
	}
}

int test(int argc, const char *argv[])
{
	Random::Initialise();

	// Cipher type
	::printf("Blowfish...\n");
	test_cipher<CipherBlowfish, 8>();
#ifndef HAVE_OLD_SSL
	::printf("AES...\n");
	test_cipher<CipherAES, 16>();
#else
	::printf("Skipping AES -- not supported by version of OpenSSL in use.\n");
#endif
	
	// Test with known plaintext and ciphertext (correct algorithm used, etc)
	{
		FileStream keyfile("testfiles/bbackupd.keys");
		// Ideally we would use a 448 bit (56 byte) key here, since that's what we do in
		// real life, but unfortunately the OpenSSL command-line tool only supports 128-bit
		// Blowfish keys, so it's hard to generate a reference ciphertext unless we restrict
		// ourselves to what OpenSSL can support too.
		// https://security.stackexchange.com/questions/25393/openssl-blowfish-key-limited-to-256-bits
		char key[16], iv[BACKUPCRYPTOKEYS_FILENAME_IV_LENGTH];

		if(!keyfile.ReadFullBuffer(key, sizeof(key), 0))
		{
			TEST_FAIL_WITH_MESSAGE("Failed to read full key length from file");
		}

		keyfile.Seek(BACKUPCRYPTOKEYS_FILENAME_KEY_LENGTH, IOStream::SeekType_Absolute);
		if(!keyfile.ReadFullBuffer(iv, sizeof(iv), 0))
		{
			TEST_FAIL_WITH_MESSAGE("Failed to read full IV length from file");
		}

		CipherContext encryptor;
		CipherContext decryptor;

		encryptor.Reset();
		encryptor.Init(CipherContext::Encrypt, CipherBlowfish(CipherDescription::Mode_CBC, key, sizeof(key)));
		ASSERT(encryptor.GetIVLength() == sizeof(iv));
		encryptor.SetIV(iv);

		decryptor.Reset();
		decryptor.Init(CipherContext::Decrypt, CipherBlowfish(CipherDescription::Mode_CBC, key, sizeof(key)));
		ASSERT(decryptor.GetIVLength() == sizeof(iv));
		decryptor.SetIV(iv);

		// The encrypted file bfdlink.h.enc was generated with the following command:
		// key=`dd if=bbackupd.keys bs=1 count=16 | hexdump -e '/1 "%02x"'`
		// iv=`dd if=bbackupd.keys bs=1 skip=56 count=8 | hexdump -e '/1 "%02x"'`
		// openssl enc -bf -in bfdlink.h -K $key -iv $iv
		// And has MD5 checksum 586b65fdd07474bc139c0795d344d8ad
		FileStream plaintext_file("testfiles/bfdlink.h", O_RDONLY);
		FileStream ciphertext_file("testfiles/bfdlink.h.enc", O_RDONLY);

		CollectInBufferStream plaintext, ciphertext;
		plaintext_file.CopyStreamTo(plaintext);
		ciphertext_file.CopyStreamTo(ciphertext);
		plaintext.SetForReading();
		ciphertext.SetForReading();

		MemoryBlockGuard<void *> encrypted(
			encryptor.MaxOutSizeForInBufferSize(ciphertext.GetSize()));

		int encrypted_size = encryptor.TransformBlock(encrypted.GetPtr(),
			encrypted.GetSize(), plaintext.GetBuffer(), plaintext.GetSize());
		TEST_EQUAL(ciphertext.GetSize(), encrypted_size);
		TEST_EQUAL(0, memcmp(encrypted.GetPtr(), ciphertext.GetBuffer(), encrypted_size));

		MemoryBlockGuard<void *> decrypted(ciphertext.GetSize() + 16);

		int decrypted_size = decryptor.TransformBlock(decrypted.GetPtr(),
			decrypted.GetSize(), encrypted.GetPtr(), encrypted_size);
		TEST_EQUAL(plaintext.GetSize(), decrypted_size);
		TEST_EQUAL(0, memcmp(decrypted.GetPtr(), plaintext.GetBuffer(), decrypted_size));
	}

	::printf("Misc...\n");
	// Check rolling checksums
	uint8_t *checkdata_blk = (uint8_t *)malloc(CHECKSUM_DATA_SIZE);
	uint8_t *checkdata = checkdata_blk;
	RAND_bytes(checkdata, CHECKSUM_DATA_SIZE);
	for(int size = CHECKSUM_BLOCK_SIZE_BASE; size <= CHECKSUM_BLOCK_SIZE_LAST; ++size)
	{
		// Test skip-roll code
		RollingChecksum rollFast(checkdata, size);
		rollFast.RollForwardSeveral(checkdata, checkdata+size, size, CHECKSUM_ROLLS/2);
		RollingChecksum calc(checkdata + (CHECKSUM_ROLLS/2), size);
		TEST_THAT(calc.GetChecksum() == rollFast.GetChecksum());

		//printf("size = %d\n", size);
		// Checksum to roll
		RollingChecksum roll(checkdata, size);
		
		// Roll forward
		for(int l = 0; l < CHECKSUM_ROLLS; ++l)
		{			
			// Calculate new one
			RollingChecksum calc(checkdata, size);

			//printf("%08X %08X %d %d\n", roll.GetChecksum(), calc.GetChecksum(), checkdata[0], checkdata[size]);

			// Compare them!
			TEST_THAT(calc.GetChecksum() == roll.GetChecksum());
			
			// Roll it onwards
			roll.RollForward(checkdata[0], checkdata[size], size);
	
			// increment
			++checkdata;
		}
	}
	::free(checkdata_blk);

	// Random integers
	check_random_int(0);
	check_random_int(1);
	check_random_int(5);
	check_random_int(15);	// all 1's
	check_random_int(1022);

	return 0;
}



