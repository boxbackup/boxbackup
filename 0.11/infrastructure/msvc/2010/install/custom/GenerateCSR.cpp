#include "stdafx.h"


static BOOL CheckPrivateKey(HCRYPTPROV hCryptProv)
{
	HCRYPTKEY	hKey	= NULL;
	BOOL			rv		= FALSE;

	WcaLog(LOGMSG_STANDARD, "Checking keys");
	if (CryptGenKey(hCryptProv,AT_KEYEXCHANGE,0x08000000|CRYPT_EXPORTABLE,&hKey))
	{
		rv = CryptDestroyKey(hKey);
	}
	WcaLog(LOGMSG_STANDARD, "Checked keys");

	return rv;
}


UINT __stdcall GenerateCSR(MSIHANDLE hInstall)
{
	HRESULT hr;
	UINT er = ERROR_INSTALL_FAILURE;
	DWORD gle;
	char	ComputerName[MAX_COMPUTERNAME_LENGTH + 1];
	DWORD lenComputerName = sizeof(ComputerName);

	if(FAILED(hr = WcaInitialize(hInstall, "GenerateCSR")))
	{
		WcaLogError(hr, "Failed to initialize");
	}
	else
	{
		DWORD gle;
		HKEY hKey;

		if(ERROR_SUCCESS == (gle = RegOpenKeyEx(HKEY_LOCAL_MACHINE,"Software\\Box Backup",0,KEY_QUERY_VALUE,&hKey)))
		{
			DWORD	data;
			DWORD cb = sizeof(DWORD);

			gle = RegQueryValueEx(hKey,"GeneratedCsrOk",NULL,NULL,(BYTE*)&data,&cb);

			RegCloseKey(hKey);

			if(ERROR_SUCCESS == gle && data)
			{
				return WcaFinalize(ERROR_SUCCESS);
			}
		}
	}

	if(FALSE == GetComputerName(ComputerName,&lenComputerName))
	{
		hr = HRESULT_FROM_WIN32(GetLastError());
		WcaLogError(hr, "Look out the window - I think the world just ended!");
	}
	else
	{
		DWORD lenInstallLocation = 0,
				lenAccountNumber = 0,
				lenEmailAddress = 0;
		char	*InstallLocation,
				*AccountNumber,
				*EmailAddress,
				*next = NULL;

		InstallLocation = GetCustomActionData(hInstall);

		lenInstallLocation = (DWORD)strlen(InstallLocation = strtok_s(InstallLocation,"\n",&next));
		lenAccountNumber = (DWORD)strlen(AccountNumber = strtok_s(NULL,"\n",&next));
		lenEmailAddress = (DWORD)strlen(EmailAddress = strtok_s(NULL,"\n",&next));


		if(SUCCEEDED(hr))
		{
			HCRYPTPROV hCryptProv;

			WcaLog(LOGMSG_STANDARD, "Initialized.");
			WcaLog(LOGMSG_STANDARD, InstallLocation);
			WcaLog(LOGMSG_STANDARD, AccountNumber);
			WcaLog(LOGMSG_STANDARD, EmailAddress);
		
			// Get the crypt context - can't do anything without it
			if(NULL == (hCryptProv = GetCryptContext()))
			{
				hr = HRESULT_FROM_WIN32(GetLastError());
				WcaLogError(hr, "Failed to aquire crypt context");
			}
			// Make sure we've got a key that's valid for signing
			else if(FALSE == CheckPrivateKey(hCryptProv))
			{
				hr = HRESULT_FROM_WIN32(GetLastError());
				WcaLogError(hr, "Failed to get private key");
			}
			else
			{
				CERT_RDN_ATTR	rgNameAttr[]	= {   { szOID_ORGANIZATION_NAME,				CERT_RDN_PRINTABLE_STRING, 0, NULL  },
																{ szOID_COMMON_NAME,						CERT_RDN_PRINTABLE_STRING, 0, NULL  },
																{ szOID_RSA_emailAddr,					CERT_RDN_IA5_STRING, 		0, NULL  },
																{ szOID_ORGANIZATIONAL_UNIT_NAME,   CERT_RDN_PRINTABLE_STRING, 0, NULL  }	};
				CERT_RDN			rgRDN[]			= { 4, rgNameAttr };
				CERT_NAME_INFO	Name				= { 1, rgRDN };
				LPSTR				pbCSR				= NULL;
				char				CommonName[128];
				DWORD				lenCommonName;

				lenCommonName = sprintf_s(CommonName,"BACKUP-%s",AccountNumber);
				rgNameAttr[0].Value.cbData	= 3;
				rgNameAttr[0].Value.pbData	= (BYTE*)"n/a";
				rgNameAttr[1].Value.cbData	= lenCommonName;
				rgNameAttr[1].Value.pbData = (BYTE*)CommonName;
				rgNameAttr[2].Value.cbData	= lenEmailAddress;
				rgNameAttr[2].Value.pbData = (BYTE*)EmailAddress;
				rgNameAttr[3].Value.cbData	= lenComputerName;
				rgNameAttr[3].Value.pbData	= (BYTE*)ComputerName;

				WcaLog(LOGMSG_STANDARD, "Encoding CSR");

				DWORD	cbNameEncoded = 0;

				if(FALSE == CryptEncodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, X509_NAME, &Name, NULL, &cbNameEncoded))
				{
					hr = HRESULT_FROM_WIN32(GetLastError());
					WcaLogError(hr, "Failed to get buffer size to encode object");
				}
				else
				{
					BYTE *pbNameEncoded = new BYTE[cbNameEncoded];

					if(FALSE == CryptEncodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, X509_NAME, &Name, pbNameEncoded, &cbNameEncoded))
					{
						hr = HRESULT_FROM_WIN32(GetLastError());
						WcaLogError(hr, "Failed to encode object");
					}
					else
					{
						DWORD cbPublicKeyInfo = 0;

						WcaLog(LOGMSG_STANDARD, "Exporting public key");

						if(FALSE == CryptExportPublicKeyInfo(hCryptProv, AT_KEYEXCHANGE, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, NULL, &cbPublicKeyInfo))
						{
							hr = HRESULT_FROM_WIN32(GetLastError());
							WcaLogError(hr, "Failed to get buffer length for public key");
						}
						else
						{
							char	*PublicKeyBuffer	= new char[cbPublicKeyInfo];
							CERT_PUBLIC_KEY_INFO*	pbPublicKeyInfo	= (CERT_PUBLIC_KEY_INFO*) PublicKeyBuffer;

							if(FALSE == CryptExportPublicKeyInfo(hCryptProv, AT_KEYEXCHANGE, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, pbPublicKeyInfo, &cbPublicKeyInfo))
							{
								hr = HRESULT_FROM_WIN32(GetLastError());
								WcaLogError(hr, "Failed to export public key");
							}
							else
							{
								CRYPT_ALGORITHM_IDENTIFIER			SigAlg;
								SigAlg.pszObjId						= szOID_RSA_SHA1RSA;
								SigAlg.Parameters.cbData			= 0;

								CERT_REQUEST_INFO						CertReqInfo;
								CertReqInfo.dwVersion				= CERT_REQUEST_V1;
								CertReqInfo.Subject.cbData			= cbNameEncoded;
								CertReqInfo.Subject.pbData			= pbNameEncoded;
								CertReqInfo.SubjectPublicKeyInfo	= *pbPublicKeyInfo;
								CertReqInfo.cAttribute				= 0;
								CertReqInfo.rgAttribute				= NULL;


								DWORD cbSignedEncodedCertReqSize = 0;

								WcaLog(LOGMSG_STANDARD, "Signing certificate");

								if(FALSE == CryptSignAndEncodeCertificate(
									hCryptProv,
									AT_KEYEXCHANGE,
									X509_ASN_ENCODING,
									X509_CERT_REQUEST_TO_BE_SIGNED,
									&CertReqInfo,
									&SigAlg,
									NULL,
									NULL,
									&cbSignedEncodedCertReqSize))
								{
									hr = HRESULT_FROM_WIN32(GetLastError());
									WcaLogError(hr, "Failed to get buffer size for csr");
								}
								else
								{
									BYTE* pbSignedEncodedCertReq = new BYTE[cbSignedEncodedCertReqSize];

									if(FALSE == CryptSignAndEncodeCertificate(
										hCryptProv,
										AT_KEYEXCHANGE,
										X509_ASN_ENCODING,
										X509_CERT_REQUEST_TO_BE_SIGNED,
										&CertReqInfo,
										&SigAlg,
										NULL,
										pbSignedEncodedCertReq,
										&cbSignedEncodedCertReqSize))
									{
										hr = HRESULT_FROM_WIN32(hr);
										WcaLogError(hr, "Failed to sign and encode certificate");
									}
									else
									{
										DWORD cbCSR = 0;

										WcaLog(LOGMSG_STANDARD, "Binary to string");

										if(FALSE == CryptBinaryToString(pbSignedEncodedCertReq, cbSignedEncodedCertReqSize, CRYPT_STRING_BASE64REQUESTHEADER, NULL, &cbCSR))
										{
											hr = HRESULT_FROM_WIN32(GetLastError());
											WcaLogError(hr, "Failed to get buffer size for csr string");
										}
										else
										{
											char *pbCSR = new char[cbCSR];

											if(FALSE == CryptBinaryToString(pbSignedEncodedCertReq, cbSignedEncodedCertReqSize, CRYPT_STRING_BASE64REQUESTHEADER, pbCSR, &cbCSR))
											{
												hr = HRESULT_FROM_WIN32(GetLastError());
												WcaLogError(hr, "Failed to get csr as string");
											}
											else
											{
												HANDLE hFile;
												char filename[MAX_PATH+1];

												// Woo! We've got everything

												sprintf_s(filename,sizeof(filename),"%s%s-csr.pem",InstallLocation,AccountNumber);
												if(INVALID_HANDLE_VALUE == (hFile = CreateFile(filename,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL))
													&& ERROR_FILE_EXISTS != (gle = GetLastError()))
												{
													hr = HRESULT_FROM_WIN32(gle);
													WcaLogError(hr, "Can't create CSR file");
												}
												else
												{
													DWORD nWritten;

													if (FALSE == WriteFile(hFile,pbCSR,cbCSR,&nWritten,NULL)
														|| cbCSR != nWritten)
													{
														hr = HRESULT_FROM_WIN32(GetLastError());
														WcaLogError(hr, "Failed to write CSR");
													}
													else
													{
														DWORD gle;
														HKEY hKey;

														if(ERROR_SUCCESS != (gle = RegOpenKeyEx(HKEY_LOCAL_MACHINE,"Software\\Box Backup",0,KEY_SET_VALUE,&hKey)))
														{
															hr = HRESULT_FROM_WIN32(gle);
															WcaLogError(hr, "Failed to open registry key");
														}
														else
														{
															DWORD data = 1;

															if(ERROR_SUCCESS != (gle = RegSetValueEx(hKey,"GeneratedCsrOk",0,REG_DWORD,(BYTE*)&data,sizeof(DWORD))))
															{
																hr = HRESULT_FROM_WIN32(gle);
																WcaLogError(hr, "Failed to set registry value");
															}

															RegCloseKey(hKey);
														}
													}

													CloseHandle(hFile);
												}


												if(SUCCEEDED(hr))
												{
													er = ERROR_SUCCESS;
												}
											}

											delete[] pbCSR;
										}
									}

									delete[] pbSignedEncodedCertReq;
								}
							}

							delete[] PublicKeyBuffer;
						}
					}

					delete[] pbNameEncoded;
				}
			}

			CryptReleaseContext(hCryptProv,0);
		}

		delete[] InstallLocation;
	}

	return WcaFinalize(er);
}
