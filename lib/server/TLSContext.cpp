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
	ImportCertificatesAndPrivateKeyFromStore(CertificatesFile);
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
void TLSContext::ImportCertificatesAndPrivateKeyFromStore(const char *CertificatesFile)
{
	HCERTSTORE  hSystemStore;

	if (NULL == (hSystemStore = CertOpenStore(CERT_STORE_PROV_SYSTEM,
															0,
															NULL,
															CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG,
															L"MY")))
	{
		BOX_LOG_WIN_ERROR("Failed to open store");
	}
	else
	{
		PCCERT_CONTEXT  pDesiredCert = NULL;

		//
		// serverCA
		//
		if(NULL == (pDesiredCert = CertFindCertificateInStore(hSystemStore,
																				PKCS_7_ASN_ENCODING | X509_ASN_ENCODING,
																				0,
																				CERT_FIND_SUBJECT_STR,
																				L"Backup system server root",
																				NULL)))
		{
			BOX_LOG_WIN_ERROR("No matching root certificate");
		}
		else
		{
			const unsigned char *pbCertEncoded = pDesiredCert->pbCertEncoded;
			X509_LOOKUP *lookup	= NULL;
			X509 *x509	= NULL;

			if(NULL == (x509 = d2i_X509(NULL, &pbCertEncoded, pDesiredCert->cbCertEncoded)))
			{
				SSLLib::LogError("d2i_X509");
				THROW_EXCEPTION(ServerException, TLSLoadTrustedCAsFailed)
			}
			if(NULL == (lookup = ::X509_STORE_add_lookup(mpContext->cert_store, X509_LOOKUP_file())))
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

			CertFreeCertificateContext(pDesiredCert);
		}


		//
		// BACKUP-12345
		//
		std::string subject("BACKUP-");
		subject.append(&CertificatesFile[2]);

		if(NULL == (pDesiredCert = CertFindCertificateInStore(hSystemStore,
																				PKCS_7_ASN_ENCODING | X509_ASN_ENCODING,
																				0,
																				CERT_FIND_SUBJECT_STR,
																				Win32::multi2wide(subject).c_str(),
																				NULL)))
		{
			BOX_LOG_WIN_ERROR("No matching certificate");
		}
		else
		{
			const unsigned char *pbCertEncoded = pDesiredCert->pbCertEncoded;
			X509 *x509	= NULL;
					
			if(NULL == (x509 = d2i_X509(NULL, &pbCertEncoded, pDesiredCert->cbCertEncoded)))
			{
				SSLLib::LogError("d2i_X509");
				THROW_EXCEPTION(ServerException, TLSLoadCertificatesFailed)
			}
			if(::SSL_CTX_use_certificate(mpContext, x509) != 1)
			{
				SSLLib::LogError("SSL_CTX_use_certificate");
				THROW_EXCEPTION(ServerException, TLSLoadCertificatesFailed)
			}
			X509_free(x509);


			//
			// Check the Private Key is associated with the Certificate
			//
			DWORD pcbData = 0;
			if (CertGetCertificateContextProperty(pDesiredCert, CERT_KEY_PROV_INFO_PROP_ID, NULL, &pcbData))
			{
				BOX_TRACE("Certificate has associated Private Key");
			}
			else
			{
				CRYPT_KEY_PROV_INFO cryptKeyProvInfo = { L"BoxBackup",
																	  MS_DEF_PROV_W,
																	  PROV_RSA_FULL,
																	  CRYPT_MACHINE_KEYSET,
																	  0,
																	  NULL,
																	  AT_KEYEXCHANGE };

				if (!CertSetCertificateContextProperty(pDesiredCert,
																	CERT_KEY_PROV_INFO_PROP_ID,
																	0,
																	&cryptKeyProvInfo))
				{
					BOX_LOG_WIN_ERROR("SetCertProp failed");
				}
				else
				{
					BOX_INFO("Associated Private Key with certificate");
				}
			}


			//
			// Private Key
			//
			HCRYPTPROV hCryptProv;
			DWORD keySpec;
			BOOL mustFree;

			if (!CryptAcquireCertificatePrivateKey(pDesiredCert,
																CRYPT_ACQUIRE_SILENT_FLAG,
																NULL,
																&hCryptProv,
																&keySpec,
																&mustFree))
			{
				BOX_LOG_WIN_ERROR("CryptAcquireCertificatePrivateKey failed");
			}
			else
			{
				HCRYPTKEY	hKey	= NULL;

				// must be one of these....
				if (!CryptGetUserKey(hCryptProv, AT_KEYEXCHANGE, &hKey)
					&& !CryptGetUserKey(hCryptProv, AT_SIGNATURE, &hKey))
				{
					BOX_LOG_WIN_ERROR("CryptGetUserKey failed - no Private Key?!");
				}
				else
				{
					DWORD len = 0;

					if(!CryptExportKey(hKey, NULL, PRIVATEKEYBLOB, 0, NULL, &len))
					{
						BOX_LOG_WIN_ERROR("CryptExportKey failed (first pass)");
					}
					else
					{
						std::unique_ptr<BYTE[]> key(new BYTE[len]);

						if(!CryptExportKey(hKey, NULL, PRIVATEKEYBLOB, 0, key.get(), &len))
						{
							BOX_LOG_WIN_ERROR("CryptExportKey failed (second pass)");
						}
						else if(!CryptEncodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
															PKCS_RSA_PRIVATE_KEY,
															key.get(),
															NULL,
															&len))
						{
							BOX_LOG_WIN_ERROR("CryptEncodeObject failed (first pass)");
						}
						else
						{
							std::unique_ptr<BYTE[]> encodedKey(new BYTE[len]);

							if(!CryptEncodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
														 PKCS_RSA_PRIVATE_KEY,
														 key.get(),
														 encodedKey.get(),
														 &len))
							{
								BOX_LOG_WIN_ERROR("CryptEncodeObject failed (second pass)");
							}
							else
							{
								const unsigned char *pEncodedKey = encodedKey.get();
								EVP_PKEY *evpPK = NULL;

								if(!d2i_PrivateKey(EVP_PKEY_RSA, &evpPK, &pEncodedKey, len))
								{
									SSLLib::LogError("d2i_PrivateKey");
									THROW_EXCEPTION(ServerException, TLSLoadPrivateKeyFailed)
								}
								if(::SSL_CTX_use_PrivateKey(mpContext, evpPK) != 1)
								{
									SSLLib::LogError("SSL_CTX_use_PrivateKey");
									THROW_EXCEPTION(ServerException, TLSLoadPrivateKeyFailed)
								}
								EVP_PKEY_free(evpPK);
							}
						}
					}

					CryptDestroyKey(hKey);

					if(mustFree)
					{
						CryptReleaseContext(hCryptProv, 0);
					}
				}
			}

			CertFreeCertificateContext(pDesiredCert);
		}

		CertCloseStore(hSystemStore, CERT_CLOSE_STORE_CHECK_FLAG); // TODO: check this
	}
}
#endif
