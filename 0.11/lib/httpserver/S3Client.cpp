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
#include "HTTPServer.h"
#include "autogen_HTTPException.h"
#include "IOStream.h"
#include "Logging.h"
#include "S3Client.h"
#include "decode.h"
#include "encode.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Client::GetObject(const std::string& rObjectURI)
//		Purpose: Retrieve the object with the specified URI (key)
//			 from your S3 bucket.
//		Created: 09/01/09
//
// --------------------------------------------------------------------------

HTTPResponse S3Client::GetObject(const std::string& rObjectURI)
{
	return FinishAndSendRequest(HTTPRequest::Method_GET, rObjectURI);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Client::PutObject(const std::string& rObjectURI,
//			 IOStream& rStreamToSend, const char* pContentType)
//		Purpose: Upload the stream to S3, creating or overwriting the
//			 object with the specified URI (key) in your S3
//			 bucket.
//		Created: 09/01/09
//
// --------------------------------------------------------------------------

HTTPResponse S3Client::PutObject(const std::string& rObjectURI,
	IOStream& rStreamToSend, const char* pContentType)
{
	return FinishAndSendRequest(HTTPRequest::Method_PUT, rObjectURI,
		&rStreamToSend, pContentType);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Client::FinishAndSendRequest(
//			 HTTPRequest::Method Method,
//			 const std::string& rRequestURI,
//			 IOStream* pStreamToSend,
//			 const char* pStreamContentType)
//		Purpose: Internal method which creates an HTTP request to S3,
//			 populates the date and authorization header fields,
//			 and sends it to S3 (or the simulator), attaching
//			 the specified stream if any to the request. Opens a
//			 connection to the server if necessary, which may
//			 throw a ConnectionException. Returns the HTTP
//			 response returned by S3, which may be a 500 error.
//		Created: 09/01/09
//
// --------------------------------------------------------------------------

HTTPResponse S3Client::FinishAndSendRequest(HTTPRequest::Method Method,
	const std::string& rRequestURI, IOStream* pStreamToSend,
	const char* pStreamContentType)
{
	HTTPRequest request(Method, rRequestURI);
	request.SetHostName(mHostName);
	
	std::ostringstream date;
	time_t tt = time(NULL);
	struct tm *tp = gmtime(&tt);
	if (!tp)
	{
		BOX_ERROR("Failed to get current time");
		THROW_EXCEPTION(HTTPException, Internal);
	}
	const char *dow[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
	date << dow[tp->tm_wday] << ", ";
	const char *month[] = {"Jan","Feb","Mar","Apr","May","Jun",
		"Jul","Aug","Sep","Oct","Nov","Dec"};
	date << std::internal << std::setfill('0') <<
		std::setw(2) << tp->tm_mday << " " <<
		month[tp->tm_mon] << " " <<
		(tp->tm_year + 1900) << " ";
	date << std::setw(2) << tp->tm_hour << ":" <<
		std::setw(2) << tp->tm_min  << ":" <<
		std::setw(2) << tp->tm_sec  << " GMT";
	request.AddHeader("Date", date.str());

	if (pStreamContentType)
	{
		request.AddHeader("Content-Type", pStreamContentType);
	}
	
	std::string s3suffix = ".s3.amazonaws.com";
	std::string bucket;
	if (mHostName.size() > s3suffix.size())
	{
		std::string suffix = mHostName.substr(mHostName.size() -
			s3suffix.size(), s3suffix.size());
		if (suffix == s3suffix)
		{
			bucket = mHostName.substr(0, mHostName.size() -
				s3suffix.size());
		}
	}
	
	std::ostringstream data;
	data << request.GetVerb() << "\n";
	data << "\n"; /* Content-MD5 */
	data << request.GetContentType() << "\n";
	data << date.str() << "\n";
		
	if (! bucket.empty())
	{
		data << "/" << bucket;
	}
	
	data << request.GetRequestURI();
	std::string data_string = data.str();

	unsigned char digest_buffer[EVP_MAX_MD_SIZE];
	unsigned int digest_size = sizeof(digest_buffer);
	/* unsigned char* mac = */ HMAC(EVP_sha1(),
		mSecretKey.c_str(), mSecretKey.size(),
		(const unsigned char*)data_string.c_str(),
		data_string.size(), digest_buffer, &digest_size);
	std::string digest((const char *)digest_buffer, digest_size);
	
	base64::encoder encoder;
	std::string auth_code = "AWS " + mAccessKey + ":" +
		encoder.encode(digest);

	if (auth_code[auth_code.size() - 1] == '\n')
	{
		auth_code = auth_code.substr(0, auth_code.size() - 1);
	}

	request.AddHeader("Authorization", auth_code);
	
	if (mpSimulator)
	{
		if (pStreamToSend)
		{
			pStreamToSend->CopyStreamTo(request);
		}

		request.SetForReading();
		CollectInBufferStream response_buffer;
		HTTPResponse response(&response_buffer);
	
		mpSimulator->Handle(request, response);
		return response;
	}
	else
	{
		try
		{
			if (!mapClientSocket.get())
			{
				mapClientSocket.reset(new SocketStream());
				mapClientSocket->Open(Socket::TypeINET,
					mHostName, mPort);
			}
			return SendRequest(request, pStreamToSend,
				pStreamContentType);
		}
		catch (ConnectionException &ce)
		{
			if (ce.GetType() == ConnectionException::SocketWriteError)
			{
				// server may have disconnected us,
				// try to reconnect, just once
				mapClientSocket->Open(Socket::TypeINET,
					mHostName, mPort);
				return SendRequest(request, pStreamToSend,
					pStreamContentType);
			}
			else
			{
				throw;
			}
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Client::SendRequest(HTTPRequest& rRequest,
//			 IOStream* pStreamToSend,
//			 const char* pStreamContentType)
//		Purpose: Internal method which sends a pre-existing HTTP 
//			 request to S3. Attaches the specified stream if any
//			 to the request. Opens a connection to the server if
//			 necessary, which may throw a ConnectionException.
//			 Returns the HTTP response returned by S3, which may
//			 be a 500 error.
//		Created: 09/01/09
//
// --------------------------------------------------------------------------

HTTPResponse S3Client::SendRequest(HTTPRequest& rRequest,
	IOStream* pStreamToSend, const char* pStreamContentType)
{		
	HTTPResponse response;
	
	if (pStreamToSend)
	{
		rRequest.SendWithStream(*mapClientSocket,
			30000 /* milliseconds */,
			pStreamToSend, response);
	}
	else
	{
		rRequest.Send(*mapClientSocket, 30000 /* milliseconds */);
		response.Receive(*mapClientSocket, 30000 /* milliseconds */);
	}
		
	return response;
}	
