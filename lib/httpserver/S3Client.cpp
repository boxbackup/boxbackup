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

#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
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

using boost::property_tree::ptree;

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Client::ListBucket(const std::string& prefix,
//			 const std::string& delimiter,
//			 std::vector<S3Client::BucketEntry>* p_contents_out,
//			 std::vector<std::string>* p_common_prefixes_out,
//			 bool* p_truncated_out, int max_keys,
//			 const std::string& marker)
//		Purpose: Retrieve a list of objects in a bucket, with a
//			 common prefix, optionally starting from a specified
//			 marker, up to some limit. The entries, and common
//			 prefixes of entries containing the specified
//			 delimiter, will be appended to p_contents_out and
//			 p_common_prefixes_out. Returns the number of items
//			 appended (p_contents_out + p_common_prefixes_out),
//			 which may be 0 if there is nothing left to iterate
//			 over, or no matching files in the bucket.
//		Created: 18/03/2016
//
// --------------------------------------------------------------------------

int S3Client::ListBucket(std::vector<S3Client::BucketEntry>* p_contents_out,
	std::vector<std::string>* p_common_prefixes_out,
	const std::string& prefix, const std::string& delimiter,
	bool* p_truncated_out, int max_keys, const std::string& marker)
{
	HTTPRequest request(HTTPRequest::Method_GET, "/");
	request.SetParameter("delimiter", delimiter);
	request.SetParameter("prefix", prefix);
	request.SetParameter("marker", marker);
	if(max_keys != -1)
	{
		std::ostringstream max_keys_stream;
		max_keys_stream << max_keys;
		request.SetParameter("max-keys", max_keys_stream.str());
	}

	HTTPResponse response = FinishAndSendRequest(request);
	CheckResponse(response, "Failed to list files in bucket");
	ASSERT(response.GetResponseCode() == HTTPResponse::Code_OK);

	std::string response_data((const char *)response.GetBuffer(),
		response.GetSize());
	std::auto_ptr<std::istringstream> ap_response_stream(
		new std::istringstream(response_data));

	ptree response_tree;
	read_xml(*ap_response_stream, response_tree,
		boost::property_tree::xml_parser::trim_whitespace);

	if(response_tree.begin()->first != "ListBucketResult")
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, BadResponse,
			"Failed to list files in bucket: unexpected root element in "
			"response: " << response_tree.begin()->first);
	}

	if(++(response_tree.begin()) != response_tree.end())
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, BadResponse,
			"Failed to list files in bucket: multiple root elements in "
			"response: " << (++(response_tree.begin()))->first);
	}

	ptree result = response_tree.get_child("ListBucketResult");
	ASSERT(result.get<std::string>("Delimiter") == delimiter);
	ASSERT(result.get<std::string>("Prefix") == prefix);
	ASSERT(result.get<std::string>("Marker") == marker);

	std::string truncated = result.get<std::string>("IsTruncated");
	ASSERT(truncated == "true" || truncated == "false");
	if(p_truncated_out)
	{
		*p_truncated_out = (truncated == "true");
	}

	int num_results = 0;

	// Iterate over all the children of the ListBucketResult, looking for
	// nodes called "Contents", and examine them.
	BOOST_FOREACH(ptree::value_type &v, result)
	{
		if(v.first == "Contents")
		{
			std::string name = v.second.get<std::string>("Key");
			std::string etag = v.second.get<std::string>("ETag");
			std::string size = v.second.get<std::string>("Size");
			char* size_end_ptr;
			int64_t size_int = strtoull(size.c_str(), &size_end_ptr, 10);
			if(*size_end_ptr != 0)
			{
				THROW_EXCEPTION_MESSAGE(HTTPException, BadResponse,
					"Failed to list files in bucket: bad size in "
					"contents: " << size);
			}

			p_contents_out->push_back(BucketEntry(name, etag, size_int));
			num_results++;
		}
	}

	ptree common_prefixes = result.get_child("CommonPrefixes");
	BOOST_FOREACH(ptree::value_type &v, common_prefixes)
	{
		if(v.first == "Prefix")
		{
			p_common_prefixes_out->push_back(v.second.data());
			num_results++;
		}
	}

	return num_results;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Client::GetObject(const std::string& rObjectURI,
//			 const std::string& MD5Checksum)
//		Purpose: Retrieve the object with the specified URI (key)
//			 from your S3 bucket. If you supply an MD5 checksum,
//			 then it is assumed that you already have the file
//			 data with that checksum, and if the file version on
//			 the server is the same, then you will get a 304
//			 Not Modified response instead of a 200 OK, and no
//			 file data.
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------

HTTPResponse S3Client::GetObject(const std::string& rObjectURI,
	const std::string& MD5Checksum)
{
	return FinishAndSendRequest(HTTPRequest::Method_GET, rObjectURI,
		NULL, // pStreamToSend
		NULL, // pStreamContentType
		MD5Checksum);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Client::HeadObject(const std::string& rObjectURI)
//		Purpose: Retrieve the metadata for the object with the
//			 specified URI (key) from your S3 bucket.
//		Created: 03/08/2015
//
// --------------------------------------------------------------------------

HTTPResponse S3Client::HeadObject(const std::string& rObjectURI)
{
	return FinishAndSendRequest(HTTPRequest::Method_HEAD, rObjectURI);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Client::DeleteObject(const std::string& rObjectURI)
//		Purpose: Delete the object with the specified URI (key) from
//			 your S3 bucket.
//		Created: 27/01/2016
//
// --------------------------------------------------------------------------

HTTPResponse S3Client::DeleteObject(const std::string& rObjectURI)
{
	return FinishAndSendRequest(HTTPRequest::Method_DELETE, rObjectURI);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Client::PutObject(const std::string& rObjectURI,
//			 IOStream& rStreamToSend, const char* pContentType)
//		Purpose: Upload the stream to S3, creating or overwriting the
//			 object with the specified URI (key) in your S3
//			 bucket.
//		Created: 09/01/2009
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
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------

HTTPResponse S3Client::FinishAndSendRequest(HTTPRequest::Method Method,
	const std::string& rRequestURI, IOStream* pStreamToSend,
	const char* pStreamContentType, const std::string& MD5Checksum)
{
	// It's very unlikely that you want to request a URI from Amazon S3 servers
	// that doesn't start with a /. Very very unlikely.
	ASSERT(rRequestURI[0] == '/');
	HTTPRequest request(Method, rRequestURI);
	return FinishAndSendRequest(request, pStreamToSend, pStreamContentType, MD5Checksum);
}

HTTPResponse S3Client::FinishAndSendRequest(HTTPRequest request, IOStream* pStreamToSend,
	const char* pStreamContentType, const std::string& MD5Checksum)
{
	std::string virtual_host_name;

	if(!mVirtualHostName.empty())
	{
		virtual_host_name = mVirtualHostName;
	}
	else
	{
		virtual_host_name = mHostName;
	}

	bool with_parameters_for_get_request = (
		request.GetMethod() == HTTPRequest::Method_GET ||
		request.GetMethod() == HTTPRequest::Method_HEAD);
	BOX_TRACE("S3Client: " << mHostName << " > " << request.GetMethodName() <<
	" " << request.GetRequestURI(with_parameters_for_get_request));

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

	if (!MD5Checksum.empty())
	{
		request.AddHeader("If-None-Match",
			std::string("\"") + MD5Checksum + "\"");
	}

	request.SetHostName(virtual_host_name);
	std::string s3suffix = ".s3.amazonaws.com";
	std::string bucket;
	if (virtual_host_name.size() > s3suffix.size())
	{
		std::string suffix = virtual_host_name.substr(virtual_host_name.size() -
			s3suffix.size(), s3suffix.size());
		if (suffix == s3suffix)
		{
			bucket = virtual_host_name.substr(0, virtual_host_name.size() -
				s3suffix.size());
		}
	}

	std::ostringstream data;
	data << request.GetMethodName() << "\n";
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
	HTTPResponse response;

	if (mpSimulator)
	{
		if (pStreamToSend)
		{
			request.SetDataStream(pStreamToSend);
		}

		request.SetForReading();
		mpSimulator->Handle(request, response);

		// TODO FIXME: HTTPServer::Connection does some post-processing on every
		// response to determine whether Connection: keep-alive is possible.
		// We should do that here too, but currently our HTTP implementation
		// doesn't support chunked encoding, so it's disabled there, so we don't
		// do it here either.
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
			response = SendRequest(request, pStreamToSend,
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
				response = SendRequest(request, pStreamToSend,
					pStreamContentType);
			}
			else
			{
				BOX_TRACE("S3Client: " << mHostName << " ! " << ce.what());
				throw;
			}
		}
	}

	// It's not valid to have a keep-alive response if the length isn't known.
	// S3Simulator should really check this, but depending on how it's called above,
	// it might be possible to bypass that check, so this is a double-check.
	ASSERT(response.GetContentLength() >= 0 || !response.IsKeepAlive());

	BOX_TRACE("S3Client: " << mHostName << " < " << response.GetResponseCode() <<
		": " << response.GetContentLength() << " bytes")
	response.SetForReading();

	return response;
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
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------

HTTPResponse S3Client::SendRequest(HTTPRequest& rRequest,
	IOStream* pStreamToSend, const char* pStreamContentType)
{
	HTTPResponse response;

	if (pStreamToSend)
	{
		rRequest.SendWithStream(*mapClientSocket, mNetworkTimeout,
			pStreamToSend, response);
	}
	else
	{
		// No stream, so it's always safe to enable keep-alive
		rRequest.SetClientKeepAliveRequested(true);
		rRequest.Send(*mapClientSocket, mNetworkTimeout);
		response.Receive(*mapClientSocket, mNetworkTimeout);
	}

	if(!response.IsKeepAlive())
	{
		BOX_TRACE("Server will close the connection, closing our end too.");
		mapClientSocket.reset();
	}
	else
	{
		BOX_TRACE("Server will keep the connection open for more requests.");
	}

	return response;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Client::CheckResponse(HTTPResponse&,
//			 std::string& message)
//		Purpose: Check the status code of an Amazon S3 response, and
//			 throw an exception with a useful message (including
//			 the supplied message) if it's not a 200 OK response.
//		Created: 26/07/2015
//
// --------------------------------------------------------------------------

void S3Client::CheckResponse(const HTTPResponse& response, const std::string& message,
	bool ExpectNoContent) const
{
	if(response.GetResponseCode() == HTTPResponse::Code_NotFound)
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, FileNotFound,
			message << ": " << response.ResponseCodeString());
	}
	else if(response.GetResponseCode() !=
		(ExpectNoContent ? HTTPResponse::Code_NoContent : HTTPResponse::Code_OK))
	{
		std::string response_data((const char *)response.GetBuffer(),
			response.GetSize());
		THROW_EXCEPTION_MESSAGE(HTTPException, RequestFailedUnexpectedly,
			message << ": " << response.ResponseCodeString() << ":\n" <<
			response_data);
	}
}

