// --------------------------------------------------------------------------
//
// File
//		Name:    TLSContext.h
//		Purpose: TLS (SSL) context for connections
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------

#include "Box.h"

#define TLS_CLASS_IMPLEMENTATION_CPP
#include <openssl/ssl.h>
#ifdef WIN32
#include <openssl/evp.h>
#endif

#include "TLSContext.h"
#include "ServerException.h"
#include "SSLLib.h"
#include "TLSContext.h"

#include "MemLeakFindOn.h"

#ifdef WIN32
#include <WinCrypt.h>
#endif

#define MAX_VERIFICATION_DEPTH		2
#define CIPHER_LIST					"ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH"

// --------------------------------------------------------------------------
//
// Function
//		Name:    TLSContext::TLSContext()
//		Purpose: Constructor
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
TLSContext::TLSContext()
	: mpContext(0)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    TLSContext::~TLSContext()
//		Purpose: Destructor
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
TLSContext::~TLSContext()
{
	if(mpContext != 0)
	{
		::SSL_CTX_free(mpContext);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    TLSContext::Initialise(bool, const char *, const char *, const char *)
//		Purpose: Initialise the context, loading in the specified certificate and private key files
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
void TLSContext::Initialise(bool AsServer, const char *CertificatesFile, const char *PrivateKeyFile, const char *TrustedCAsFile)
{
	if(mpContext != 0)
	{
		::SSL_CTX_free(mpContext);
	}

	mpContext = ::SSL_CTX_new(AsServer?TLSv1_server_method():TLSv1_client_method());
	if(mpContext == NULL)
	{
		THROW_EXCEPTION(ServerException, TLSAllocationFailed)
	}
	
	// Setup our identity
#ifdef WIN32
	{
		X509 *x509 = GetTrustedCertificate();
		X509_LOOKUP *lookup = ::X509_STORE_add_lookup(mpContext->cert_store, X509_LOOKUP_file());
		if(lookup == NULL)
		{
			SSLLib::LogError("X509_STORE_add_lookup");
			THROW_EXCEPTION(ServerException, TLSLoadTrustedCAsFailed)
		}
		if(::X509_STORE_add_cert(lookup->store_ctx, x509) != 1)
		{
			SSLLib::LogError("X509_STORE_add_cert");
			THROW_EXCEPTION(ServerException, TLSLoadTrustedCAsFailed)
		}
		// don't free lookup
		X509_free(x509);
	}

	{
		X509 *x509 = GetCertificate();
		if(::SSL_CTX_use_certificate(mpContext, x509) != 1)
		{
			SSLLib::LogError("SSL_CTX_use_certificate");
			THROW_EXCEPTION(ServerException, TLSLoadCertificatesFailed)
		}
		X509_free(x509);
	}

	{
		EVP_PKEY *evpPK = GetPrivateKey();
		if(::SSL_CTX_use_PrivateKey(mpContext, evpPK) != 1)
		{
			SSLLib::LogError("SSL_CTX_use_PrivateKey");
			THROW_EXCEPTION(ServerException, TLSLoadPrivateKeyFailed)
		}
		EVP_PKEY_free(evpPK);
	}
#else
	if(::SSL_CTX_use_certificate_chain_file(mpContext, CertificatesFile) != 1)
	{
		std::string msg = "loading certificates from ";
		msg += CertificatesFile;
		SSLLib::LogError(msg);
		THROW_EXCEPTION(ServerException, TLSLoadCertificatesFailed)
	}
	if(::SSL_CTX_use_PrivateKey_file(mpContext, PrivateKeyFile, SSL_FILETYPE_PEM) != 1)
	{
		std::string msg = "loading private key from ";
		msg += PrivateKeyFile;
		SSLLib::LogError(msg);
		THROW_EXCEPTION(ServerException, TLSLoadPrivateKeyFailed)
	}

	// Setup the identify of CAs we trust
	if(::SSL_CTX_load_verify_locations(mpContext, TrustedCAsFile, NULL) != 1)
	{
		std::string msg = "loading CA cert from ";
		msg += TrustedCAsFile;
		SSLLib::LogError(msg);
		THROW_EXCEPTION(ServerException, TLSLoadTrustedCAsFailed)
	}
#endif

	// Setup options to require these certificates
	::SSL_CTX_set_verify(mpContext, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
	// and a sensible maximum depth
	::SSL_CTX_set_verify_depth(mpContext, MAX_VERIFICATION_DEPTH);
	
	// Setup allowed ciphers
	if(::SSL_CTX_set_cipher_list(mpContext, CIPHER_LIST) != 1)
	{
		SSLLib::LogError("setting cipher list to " CIPHER_LIST);
		THROW_EXCEPTION(ServerException, TLSSetCiphersFailed)
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    TLSContext::GetRawContext()
//		Purpose: Get the raw context for OpenSSL API
//		Created: 2003/08/06
//
// --------------------------------------------------------------------------
SSL_CTX *TLSContext::GetRawContext() const
{
	if(mpContext == 0)
	{
		THROW_EXCEPTION(ServerException, TLSContextNotInitialised)
	}
	return mpContext;
}


#ifdef WIN32
// --------------------------------------------------------------------------
//
// Function
//		Name:    TLSContext::GetCertificate()
//		Purpose: Get the certificate from the store
//		Created: 2010/09/03
//
// --------------------------------------------------------------------------
X509* TLSContext::GetCertificate()
{
	HCRYPTPROV	hCryptProv	= NULL;
	X509			*x509			= NULL;

	if (FALSE == CryptAcquireContext(&hCryptProv,"BoxBackup",NULL,PROV_RSA_FULL,CRYPT_MACHINE_KEYSET))
	{
		BOX_ERROR("CryptAcquireContext failed");
	}
	else
	{
		DWORD cbPublicKeyInfo = 0;

		if(FALSE == CryptExportPublicKeyInfo(hCryptProv, AT_SIGNATURE, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, NULL, &cbPublicKeyInfo))
		{
			BOX_ERROR("Failed to get buffer length for public key");
		}
		else
		{
			char	*PublicKeyBuffer	= new char[cbPublicKeyInfo];
			CERT_PUBLIC_KEY_INFO*	pbPublicKeyInfo	= (CERT_PUBLIC_KEY_INFO*) PublicKeyBuffer;

			if(FALSE == CryptExportPublicKeyInfo(hCryptProv, AT_SIGNATURE, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, pbPublicKeyInfo, &cbPublicKeyInfo))
			{
				BOX_ERROR("Failed to export public key");
			}
			else
			{
				HCERTSTORE  hSystemStore;

				if (NULL == (hSystemStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, NULL, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"MY")))
				{
					BOX_ERROR("Failed to open store");
				}
				else
				{
					PCCERT_CONTEXT  pDesiredCert = NULL;

					if(NULL == (pDesiredCert = CertFindCertificateInStore(hSystemStore, PKCS_7_ASN_ENCODING | X509_ASN_ENCODING, 0, CERT_FIND_PUBLIC_KEY, pbPublicKeyInfo, NULL)))
					{
						BOX_ERROR("No matching certificate");
					}
					else
					{
						const unsigned char *pbCertEncoded = pDesiredCert->pbCertEncoded;

						x509 = d2i_X509(NULL, &pbCertEncoded, pDesiredCert->cbCertEncoded);

						DWORD pcbData = 0;
						if (CertGetCertificateContextProperty(pDesiredCert,CERT_KEY_PROV_INFO_PROP_ID,NULL,&pcbData))
						{
							BOX_TRACE("Certificate has associated Private Key");
						}
						else
						{
							CRYPT_KEY_PROV_INFO cryptKeyProvInfo = { L"BoxBackup",MS_DEF_PROV_W,PROV_RSA_FULL,CRYPT_MACHINE_KEYSET,0,NULL,AT_SIGNATURE };

							if (!CertSetCertificateContextProperty(pDesiredCert,CERT_KEY_PROV_INFO_PROP_ID,0,&cryptKeyProvInfo))
							{
								BOX_ERROR("SetCertProp failed");
							}
							else
							{
								BOX_INFO("Associated Private Key with certificate");
							}
						}

						CertFreeCertificateContext(pDesiredCert);
					}

					CertCloseStore(hSystemStore,CERT_CLOSE_STORE_CHECK_FLAG); // TODO: check this
				}
			}

			delete[] PublicKeyBuffer;
		}

		CryptReleaseContext(hCryptProv,0);
	}

	if(NULL == x509)
	{
		THROW_EXCEPTION(ServerException, TLSLoadCertificatesFailed)
	}

	return x509;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    TLSContext::GetTrustedCertificate()
//		Purpose: Get the certificate from the store
//		Created: 2010/10/09
//
// --------------------------------------------------------------------------
X509* TLSContext::GetTrustedCertificate()
{
	X509			*x509			= NULL;
	HCERTSTORE  hSystemStore;

	if (NULL == (hSystemStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, NULL, CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG, L"MY")))
	{
		BOX_ERROR("Failed to open store");
	}
	else
	{
		PCCERT_CONTEXT  pDesiredCert = NULL;

		if(NULL == (pDesiredCert = CertFindCertificateInStore(hSystemStore, PKCS_7_ASN_ENCODING | X509_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR, L"Backup system server root", NULL)))
		{
			BOX_ERROR("No matching root certificate");
		}
		else
		{
			const unsigned char *pbCertEncoded = pDesiredCert->pbCertEncoded;

			x509 = d2i_X509(NULL, &pbCertEncoded, pDesiredCert->cbCertEncoded);

			CertFreeCertificateContext(pDesiredCert);
		}

		CertCloseStore(hSystemStore,CERT_CLOSE_STORE_CHECK_FLAG); // TODO: check this
	}

	if(NULL == x509)
	{
		THROW_EXCEPTION(ServerException, TLSLoadTrustedCAsFailed)
	}

	return x509;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    TLSContext::GetPrivateKey()
//		Purpose: Get the private key from the container
//		Created: 2010/09/03
//
// --------------------------------------------------------------------------
EVP_PKEY* TLSContext::GetPrivateKey()
{
	HCRYPTPROV		hCryptProv		= NULL;
	EVP_PKEY			*evpPK			= NULL;

	if (FALSE == CryptAcquireContext(&hCryptProv,"BoxBackup",NULL,PROV_RSA_FULL,CRYPT_MACHINE_KEYSET))
	{
		BOX_ERROR("CryptAcquireContext failed");
	}
	else
	{
		HCRYPTKEY	hKey	= NULL;

		if (FALSE == CryptGetUserKey(hCryptProv,AT_SIGNATURE,&hKey))
		{
			BOX_ERROR("CryptGetUserKey failed");
		}
		else
		{
			DWORD len = 0;

			if(FALSE == CryptExportKey(hKey,NULL,PRIVATEKEYBLOB,0,NULL,&len))
			{
				BOX_ERROR("CryptExportKey failed (first pass)");
			}
			else
			{
				BYTE *key = new BYTE[len];

				if(FALSE == CryptExportKey(hKey,NULL,PRIVATEKEYBLOB,0,key,&len))
				{
					BOX_ERROR("CryptExportKey failed (second pass)");
				}
				else if(FALSE == CryptEncodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY, key, NULL, &len))
				{
					BOX_ERROR("CryptEncodeObject failed (first pass)");
				}
				else
				{
					BYTE *encodedKey = new BYTE[len];

					if(FALSE == CryptEncodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY, key, encodedKey, &len))
					{
						BOX_ERROR("CryptEncodeObject failed (second pass)");
					}
					else
					{
						const unsigned char *pEncodedKey = encodedKey;

						d2i_PrivateKey(EVP_PKEY_RSA, &evpPK, &pEncodedKey, len);
					}

					delete[] encodedKey;
				}

				delete[] key;
			}

			CryptDestroyKey(hKey);
		}
		CryptReleaseContext(hCryptProv,0);
	}

	if(NULL == evpPK)
	{
		THROW_EXCEPTION(ServerException, TLSLoadPrivateKeyFailed)
	}

	return evpPK;
}

#endif