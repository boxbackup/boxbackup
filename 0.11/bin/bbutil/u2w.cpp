#include "bbutil.h"

#include <WinCrypt.h>



static HCRYPTPROV getCryptProv() throw(Win32Exception)
{
	HCRYPTPROV	hCryptProv	= NULL;

	// first delete any existing provider that might be associated with existing certificates
	CryptAcquireContext(&hCryptProv,"BoxBackup",MS_ENHANCED_PROV,PROV_RSA_FULL,CRYPT_DELETEKEYSET|CRYPT_MACHINE_KEYSET);

	if (!CryptAcquireContext(&hCryptProv,"BoxBackup",MS_ENHANCED_PROV,PROV_RSA_FULL,CRYPT_MACHINE_KEYSET)) {
		if (!CryptAcquireContext(&hCryptProv,"BoxBackup",MS_ENHANCED_PROV,PROV_RSA_FULL,CRYPT_MACHINE_KEYSET|CRYPT_NEWKEYSET)) {
			THROW_EXCEPTION_MESSAGE(Win32Exception, API_CryptAcquireContext, "Could not aquire context")
		}
	}
	return hCryptProv;
}

static void importKeysFile(const char *KeysFile) throw(Win32Exception)
{
	ifstream	sKeysFile(KeysFile,ios_base::binary | ios_base::ate);

	if (sKeysFile.bad())
		THROW_EXCEPTION_MESSAGE(Win32Exception, Internal, "Bad KeysFile");
	ifstream::pos_type lenKeysFile = sKeysFile.tellg();
	sKeysFile.seekg(0);

	unique_ptr<char[]> bufKeysFile(new char[lenKeysFile]);
	sKeysFile.read(bufKeysFile.get(),lenKeysFile);
	sKeysFile.close();

	DWORD err;
	HKEY hKey = NULL;
	try {
		if (ERROR_SUCCESS != (err = RegOpenKeyEx(HKEY_LOCAL_MACHINE,"Software\\Box Backup",0,KEY_QUERY_VALUE|KEY_SET_VALUE,&hKey))) {
			THROW_EXCEPTION_MESSAGE(Win32Exception, API_RegOpenKeyEx, "Cannot open SOFTWARE\\Box Backup");

		} else {
			if (ERROR_SUCCESS != (err = RegSetValueEx(hKey,
																	"FileEncKeys",
																	0,
																	REG_BINARY,
																	reinterpret_cast<BYTE*>(bufKeysFile.get()),
																	static_cast<DWORD>(lenKeysFile))))
			{
				THROW_EXCEPTION_MESSAGE(Win32Exception, API_RegSetValueEx, "Cannot write FileEncKeys");
			}

			RegCloseKey(hKey);
		}
	} catch(...) {
		if (hKey)
			RegCloseKey(hKey);
		throw;
	}

	cout << "Imported KeysFile (" << KeysFile << ") OK" << endl;
}

static void importCertificateFile(HCRYPTPROV hCryptProv, const char *CertificateFile) throw(Win32Exception)
{
	ifstream sCertificateFile(CertificateFile, ios_base::ate);

	if (sCertificateFile.bad())
		THROW_EXCEPTION_MESSAGE(Win32Exception, Internal, "Bad CertificateFile");
	ifstream::pos_type lenCertificateFile = sCertificateFile.tellg();
	sCertificateFile.seekg(0);

	unique_ptr<char[]> bufCertificateFile(new char[lenCertificateFile]);
	sCertificateFile.read(bufCertificateFile.get(),lenCertificateFile);
	sCertificateFile.close();


	HCERTSTORE		hSystemStore	= NULL;
	PCCERT_CONTEXT	pCertContext	= NULL;
	HCRYPTKEY		hPubKey			= NULL;

	try {
		DWORD lenBinary = static_cast<DWORD>(lenCertificateFile * 3 / 4);
		unique_ptr<BYTE[]> binCertificateFile(new BYTE[lenBinary]);

		if (!CryptStringToBinary(bufCertificateFile.get(),
										 static_cast<DWORD>(lenCertificateFile),
										 CRYPT_STRING_BASE64HEADER,
										 binCertificateFile.get(),
										 &lenBinary,
										 NULL,
										 NULL))
		{
			THROW_EXCEPTION_MESSAGE(Win32Exception, API_CryptStringToBinary, "Failed to decode Certificate");
		}


		if (NULL == (hSystemStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, NULL, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"MY")))
			THROW_EXCEPTION_MESSAGE(Win32Exception, API_CertOpenStore, "Cannot open Local Machine certificate store");
		
		if (!CertAddEncodedCertificateToStore(hSystemStore,
														  X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
														  binCertificateFile.get(),
														  lenBinary,
														  CERT_STORE_ADD_REPLACE_EXISTING,
														  &pCertContext))
		{
			THROW_EXCEPTION_MESSAGE(Win32Exception, API_CertAddEncodedCertificateToStore, "Failed to add certificate to store");
		}

		if (!CryptImportPublicKeyInfo(hCryptProv, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, &pCertContext->pCertInfo->SubjectPublicKeyInfo, &hPubKey))
			THROW_EXCEPTION_MESSAGE(Win32Exception, API_CryptImportPublicKeyInfo, "Failed to import Public Key");
		CryptDestroyKey(hPubKey);

		CRYPT_KEY_PROV_INFO cryptKeyProvInfo = { L"BoxBackup",MS_DEF_PROV_W,PROV_RSA_FULL,CRYPT_MACHINE_KEYSET,0,NULL,AT_KEYEXCHANGE };

		if (!CertSetCertificateContextProperty(pCertContext,CERT_KEY_PROV_INFO_PROP_ID,0,&cryptKeyProvInfo))
			THROW_EXCEPTION_MESSAGE(Win32Exception, API_CertSetCertificateContextProperty, "Failed to set context property");

		CertFreeCertificateContext(pCertContext);
		
	} catch(...) {
		if (pCertContext)
			CertFreeCertificateContext(pCertContext);
		if (hSystemStore)
			CertCloseStore(hSystemStore,CERT_CLOSE_STORE_FORCE_FLAG);
		throw;
	}

	cout << "Imported CertificateFile (" << CertificateFile << ") OK" << endl;
}

void importPrivateKeyFile(HCRYPTPROV hCryptProv, const char *PrivateKeyFile) throw(Win32Exception)
{
	ifstream sPrivateKeyFile(PrivateKeyFile, ios_base::ate);

	if (sPrivateKeyFile.bad())
		THROW_EXCEPTION_MESSAGE(Win32Exception, Internal, "Bad PrivateKeyFile");
	ifstream::pos_type lenPrivateKeyFile = sPrivateKeyFile.tellg();
	sPrivateKeyFile.seekg(0);

	unique_ptr<char[]> bufPrivateKeyFile(new char[lenPrivateKeyFile]);
	sPrivateKeyFile.getline(bufPrivateKeyFile.get(),lenPrivateKeyFile); // eat the first line so decoding doesn't choke
	sPrivateKeyFile.read(bufPrivateKeyFile.get(),lenPrivateKeyFile);
	sPrivateKeyFile.close();


	try {
		DWORD lenBinaryPrivateKey = static_cast<DWORD>(lenPrivateKeyFile * 3 / 4);
		unique_ptr<BYTE[]> binPrivateKeyFile(new BYTE[lenBinaryPrivateKey]);

		if (!CryptStringToBinary(bufPrivateKeyFile.get(),
										 static_cast<DWORD>(lenPrivateKeyFile),
										 CRYPT_STRING_BASE64,
										 binPrivateKeyFile.get(),
										 &lenBinaryPrivateKey,
										 NULL,
										 NULL))
		{
			THROW_EXCEPTION_MESSAGE(Win32Exception, API_CryptStringToBinary, "Cannot decode Private Key");
		}

		DWORD lenDecodedPrivateKey = 0;
		if (!CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
									  PKCS_RSA_PRIVATE_KEY,
									  binPrivateKeyFile.get(),
									  lenBinaryPrivateKey,
									  0,
									  NULL,
									  &lenDecodedPrivateKey) && ERROR_MORE_DATA != GetLastError())
		{
			THROW_EXCEPTION_MESSAGE(Win32Exception, API_CryptDecodeObject, "Failed to get buffer length to decode Private Key")
		}

		unique_ptr<BYTE[]> decodedPrivateKey(new BYTE[lenDecodedPrivateKey]);
		if (!CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
									  PKCS_RSA_PRIVATE_KEY,
									  binPrivateKeyFile.get(),
									  lenBinaryPrivateKey,
									  0,
									  decodedPrivateKey.get(),
									  &lenDecodedPrivateKey))
		{
			THROW_EXCEPTION_MESSAGE(Win32Exception, API_CryptDecodeObject, "Failed to decode Private Key")
		}

		HCRYPTKEY hPrivKey;
		if (!CryptImportKey(hCryptProv,
								  decodedPrivateKey.get(),
								  lenDecodedPrivateKey,
								  NULL,
								  CRYPT_EXPORTABLE,
								  &hPrivKey))
		{
			THROW_EXCEPTION_MESSAGE(Win32Exception, API_CryptImportKey, "Failed to import Private Key")
		}
		CryptDestroyKey(hPrivKey);

	} catch(...) {
		throw;
	}

	cout << "Imported PrivateKeyFile (" << PrivateKeyFile << ") OK" << endl;
}


/*
 * This is a utility function that can only be called by an administrator.
 * I'm assuming things are as they ought to be - there are far better
 * and easier ways to do damage to a system than by abusing this code.
 */
void do_u2w(const char *KeysFile, const char *CertificateFile, const char *PrivateKeyFile) throw()
{
	HCRYPTPROV	hCryptProv = NULL;

	try {
		hCryptProv	= getCryptProv();

		importKeysFile(KeysFile);
		importCertificateFile(hCryptProv, CertificateFile);
		importPrivateKeyFile(hCryptProv, PrivateKeyFile);

	} catch(Win32Exception &e) {
		cout << e.what() << ": " << e.GetMessage() << endl;
	} catch(std::exception &e) {
		cout << e.what() << endl;
	}

	if (hCryptProv)
		CryptReleaseContext(hCryptProv,0);
}
