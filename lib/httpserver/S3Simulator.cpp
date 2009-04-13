// --------------------------------------------------------------------------
//
// File
//		Name:    S3Client.cpp
//		Purpose: Amazon S3 client helper implementation class
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <cstring>

// #include <cstdio>
// #include <ctime>

#include <openssl/hmac.h>

#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "autogen_HTTPException.h"
#include "IOStream.h"
#include "Logging.h"
#include "S3Simulator.h"
#include "decode.h"
#include "encode.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    HTTPServer::GetConfigVerify()
//		Purpose: Returns additional configuration options for the
//			 S3 simulator. Currently the access key, secret key
//			 and store directory can be configured.
//		Created: 09/01/09
//
// --------------------------------------------------------------------------
const ConfigurationVerify* S3Simulator::GetConfigVerify() const
{
	static ConfigurationVerifyKey verifyserverkeys[] = 
	{
		HTTPSERVER_VERIFY_SERVER_KEYS(ConfigurationVerifyKey::NoDefaultValue) // no default addresses
	};

	static ConfigurationVerify verifyserver[] = 
	{
		{
			"Server",
			0,
			verifyserverkeys,
			ConfigTest_Exists | ConfigTest_LastEntry,
			0
		}
	};
	
	static ConfigurationVerifyKey verifyrootkeys[] = 
	{
		ConfigurationVerifyKey("AccessKey", ConfigTest_Exists),
		ConfigurationVerifyKey("SecretKey", ConfigTest_Exists),
		ConfigurationVerifyKey("StoreDirectory", ConfigTest_Exists),
		HTTPSERVER_VERIFY_ROOT_KEYS
	};

	static ConfigurationVerify verify =
	{
		"root",
		verifyserver,
		verifyrootkeys,
		ConfigTest_Exists | ConfigTest_LastEntry,
		0
	};

	return &verify;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Simulator::Handle(HTTPRequest &rRequest,
//			 HTTPResponse &rResponse)
//		Purpose: Handles any incoming S3 request, by checking
//			 authorization and then dispatching to one of the
//			 private Handle* methods.
//		Created: 09/01/09
//
// --------------------------------------------------------------------------

void S3Simulator::Handle(HTTPRequest &rRequest, HTTPResponse &rResponse)
{
	// if anything goes wrong, return a 500 error
	rResponse.SetResponseCode(HTTPResponse::Code_InternalServerError);
	rResponse.SetContentType("text/plain");

	try
	{
		const Configuration& rConfig(GetConfiguration());
		std::string access_key = rConfig.GetKeyValue("AccessKey");
		std::string secret_key = rConfig.GetKeyValue("SecretKey");
		
		std::string md5, date, bucket;
		rRequest.GetHeader("content-md5", &md5);
		rRequest.GetHeader("date", &date);
		
		std::string host = rRequest.GetHostName();
		std::string s3suffix = ".s3.amazonaws.com";
		if (host.size() > s3suffix.size())
		{
			std::string suffix = host.substr(host.size() -
				s3suffix.size(), s3suffix.size());
			if (suffix == s3suffix)
			{
				bucket = host.substr(0, host.size() -
					s3suffix.size());
			}
		}
		
		std::ostringstream data;
		data << rRequest.GetVerb() << "\n";
		data << md5 << "\n";
		data << rRequest.GetContentType() << "\n";
		data << date << "\n";

		// header names are already in lower case, i.e. canonical form

		std::vector<HTTPRequest::Header> headers = rRequest.GetHeaders();
		sort(headers.begin(), headers.end());
		
		for (std::vector<HTTPRequest::Header>::iterator
			i = headers.begin(); i != headers.end(); i++)
		{
			if (i->first.substr(0, 5) == "x-amz")
			{
				data << i->first << ":" << i->second << "\n";
			}
		}		
		
		if (! bucket.empty())
		{
			data << "/" << bucket;
		}
		
		data << rRequest.GetRequestURI();
		std::string data_string = data.str();

		unsigned char digest_buffer[EVP_MAX_MD_SIZE];
		unsigned int digest_size = sizeof(digest_buffer);
		/* unsigned char* mac = */ HMAC(EVP_sha1(),
			secret_key.c_str(), secret_key.size(),
			(const unsigned char*)data_string.c_str(),
			data_string.size(), digest_buffer, &digest_size);
		std::string digest((const char *)digest_buffer, digest_size);
		
		base64::encoder encoder;
		std::string expectedAuth = "AWS " + access_key + ":" +
			encoder.encode(digest);
		
		if (expectedAuth[expectedAuth.size() - 1] == '\n')
		{
			expectedAuth = expectedAuth.substr(0,
				expectedAuth.size() - 1);
		}
		
		std::string actualAuth;
		if (!rRequest.GetHeader("authorization", &actualAuth) ||
			actualAuth != expectedAuth)
		{
			rResponse.SetResponseCode(HTTPResponse::Code_Unauthorized);
			SendInternalErrorResponse("Authentication Failed",
				rResponse);
		}	
		else if (rRequest.GetMethod() == HTTPRequest::Method_GET)
		{
			HandleGet(rRequest, rResponse);
		}
		else if (rRequest.GetMethod() == HTTPRequest::Method_PUT)
		{
			HandlePut(rRequest, rResponse);
		}
		else
		{
			rResponse.SetResponseCode(HTTPResponse::Code_MethodNotAllowed);
			SendInternalErrorResponse("Unsupported Method",
				rResponse);
		}
	}
	catch (CommonException &ce)
	{
		SendInternalErrorResponse(ce.what(), rResponse);
	}
	catch (std::exception &e)
	{
		SendInternalErrorResponse(e.what(), rResponse);
	}
	catch (...)
	{
		SendInternalErrorResponse("Unknown exception", rResponse);
	}
	
	if (rResponse.GetResponseCode() != 200 &&
		rResponse.GetSize() == 0)
	{
		// no error message written, provide a default
		std::ostringstream s;
		s << rResponse.GetResponseCode();
		SendInternalErrorResponse(s.str().c_str(), rResponse);
	}
	
	return;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Simulator::HandleGet(HTTPRequest &rRequest,
//			 HTTPResponse &rResponse)
//		Purpose: Handles an S3 GET request, i.e. downloading an
//			 existing object.
//		Created: 09/01/09
//
// --------------------------------------------------------------------------

void S3Simulator::HandleGet(HTTPRequest &rRequest, HTTPResponse &rResponse)
{
	std::string path = GetConfiguration().GetKeyValue("StoreDirectory");
	path += rRequest.GetRequestURI();
	std::auto_ptr<FileStream> apFile;

	try
	{
		apFile.reset(new FileStream(path));
	}
	catch (CommonException &ce)
	{
		if (ce.GetSubType() == CommonException::OSFileOpenError)
		{
			rResponse.SetResponseCode(HTTPResponse::Code_NotFound);
		}
		else if (ce.GetSubType() == CommonException::AccessDenied)
		{
			rResponse.SetResponseCode(HTTPResponse::Code_Forbidden);
		}
		throw;
	}

	// http://docs.amazonwebservices.com/AmazonS3/2006-03-01/UsingRESTOperations.html
	apFile->CopyStreamTo(rResponse);
	rResponse.AddHeader("x-amz-id-2", "qBmKRcEWBBhH6XAqsKU/eg24V3jf/kWKN9dJip1L/FpbYr9FDy7wWFurfdQOEMcY");
	rResponse.AddHeader("x-amz-request-id", "F2A8CCCA26B4B26D");
	rResponse.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
	rResponse.AddHeader("Last-Modified", "Sun, 1 Jan 2006 12:00:00 GMT");
	rResponse.AddHeader("ETag", "\"828ef3fdfa96f00ad9f27c383fc9ac7f\"");
	rResponse.AddHeader("Server", "AmazonS3");
	rResponse.SetResponseCode(HTTPResponse::Code_OK);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Simulator::HandlePut(HTTPRequest &rRequest,
//			 HTTPResponse &rResponse)
//		Purpose: Handles an S3 PUT request, i.e. uploading data to
//			 create or replace an object.
//		Created: 09/01/09
//
// --------------------------------------------------------------------------

void S3Simulator::HandlePut(HTTPRequest &rRequest, HTTPResponse &rResponse)
{
	std::string path = GetConfiguration().GetKeyValue("StoreDirectory");
	path += rRequest.GetRequestURI();
	std::auto_ptr<FileStream> apFile;

	try
	{
		apFile.reset(new FileStream(path, O_CREAT | O_WRONLY));
	}
	catch (CommonException &ce)
	{
		if (ce.GetSubType() == CommonException::OSFileOpenError)
		{
			rResponse.SetResponseCode(HTTPResponse::Code_NotFound);
		}
		else if (ce.GetSubType() == CommonException::AccessDenied)
		{
			rResponse.SetResponseCode(HTTPResponse::Code_Forbidden);
		}
		throw;
	}

	if (rRequest.IsExpectingContinue())
	{
		rResponse.SendContinue();
	}

	rRequest.ReadContent(*apFile);

	// http://docs.amazonwebservices.com/AmazonS3/2006-03-01/RESTObjectPUT.html
	rResponse.AddHeader("x-amz-id-2", "LriYPLdmOdAiIfgSm/F1YsViT1LW94/xUQxMsF7xiEb1a0wiIOIxl+zbwZ163pt7");
	rResponse.AddHeader("x-amz-request-id", "F2A8CCCA26B4B26D");
	rResponse.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
	rResponse.AddHeader("Last-Modified", "Sun, 1 Jan 2006 12:00:00 GMT");
	rResponse.AddHeader("ETag", "\"828ef3fdfa96f00ad9f27c383fc9ac7f\"");
	rResponse.SetContentType("");
	rResponse.AddHeader("Server", "AmazonS3");
	rResponse.SetResponseCode(HTTPResponse::Code_OK);
}
