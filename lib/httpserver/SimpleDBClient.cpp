// --------------------------------------------------------------------------
//
// File
//		Name:    SimpleDBClient.cpp
//		Purpose: Amazon SimpleDB client class
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <openssl/x509.h>
#include <openssl/hmac.h>

#include "HTTPQueryDecoder.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "autogen_HTTPException.h"
#include "SimpleDBClient.h"
#include "decode.h"
#include "encode.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    SimpleDBClient::GenerateQueryString(
//			 const HTTPRequest& request)
//		Purpose: Generates and returns an HTTP query string for the
//			 parameters of the supplied HTTPRequest, using the
//			 specific format required for SimpleDB request
//			 authentication signatures.
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

// http://docs.aws.amazon.com/AmazonSimpleDB/latest/DeveloperGuide/HMACAuth.html
std::string SimpleDBClient::GenerateQueryString(const HTTPRequest& request)
{
	std::vector<std::string> param_names;
	std::map<std::string, std::string> param_values;

	const HTTPRequest::Query_t& params(request.GetQuery());
	for(HTTPRequest::Query_t::const_iterator i = params.begin();
		i != params.end(); i++)
	{
		// We don't want to include the Signature parameter in the sorted query
		// string, because the client didn't either when computing the signature!
		if(i->first != "Signature")
		{
			param_names.push_back(i->first);
			// This algorithm only supports non-repeated parameters, so
			// assert that we don't already have a parameter with this name.
			ASSERT(param_values.find(i->first) == param_values.end());
			param_values[i->first] = i->second;
		}
	}

	std::sort(param_names.begin(), param_names.end());
	std::ostringstream out;

	for(std::vector<std::string>::iterator i = param_names.begin();
		i != param_names.end(); i++)
	{
		if(i != param_names.begin())
		{
			out << "&";
		}
		out << HTTPQueryDecoder::URLEncode(*i) << "=" <<
			HTTPQueryDecoder::URLEncode(param_values[*i]);
	}

	return out.str();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    SimpleDBClient::CalculateSimpleDBSignature(
//			 const HTTPRequest& request)
//		Purpose: Calculates and returns an Amazon SimpleDB auth
//			 signature for the supplied HTTPRequest, based on its
//			 parameters and the access and secret keys that this
//			 SimpleDBClient was initialised with.
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

// http://docs.aws.amazon.com/AmazonSimpleDB/latest/DeveloperGuide/HMACAuth.html
std::string SimpleDBClient::CalculateSimpleDBSignature(const HTTPRequest& request)
{
	// This code is very similar to that in S3Client::FinishAndSendRequest,
	// but using EVP_sha256 instead of EVP_sha1. TODO FIXME: factor out the
	// common parts.
	std::string query_string = GenerateQueryString(request);
	if(query_string.empty())
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, Internal,
			"Failed to get query string for request");
	}

	std::ostringstream buffer_to_sign;
	buffer_to_sign << request.GetMethodName() << "\n" <<
		request.GetHeaders().GetHostNameWithPort() << "\n" <<
		// The HTTPRequestURI component is the HTTP absolute path component
		// of the URI up to, but not including, the query string. If the 
		// HTTPRequestURI is empty, use a forward slash ( / ).
		request.GetRequestURI() << "\n" <<
		query_string;

	// Thanks to https://gist.github.com/tsupo/112188:
	unsigned int digest_size;
	unsigned char digest_buffer[EVP_MAX_MD_SIZE];
	std::string string_to_sign = buffer_to_sign.str();

	HMAC(EVP_sha256(),
		mSecretKey.c_str(), mSecretKey.size(),
		(const unsigned char *)string_to_sign.c_str(), string_to_sign.size(),
		digest_buffer, &digest_size);

	base64::encoder encoder;
	std::string digest((const char *)digest_buffer, digest_size);
	std::string auth_code = encoder.encode(digest);

	if (auth_code[auth_code.size() - 1] == '\n')
	{
		auth_code = auth_code.substr(0, auth_code.size() - 1);
	}

	return auth_code;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:	 SimpleDBClient::SendAndReceive(HTTPRequest& request,
//			 HTTPResponse& response, int expected_status_code)
//		Purpose: Private method. Sends the supplied HTTPRequest to
//			 Amazon SimpleDB, and stores the response in the
//			 supplied HTTPResponse. Since SimpleDB responses are
//			 usually XML formatted, you may prefer to use
//			 SendAndReceiveXML() instead, which parses the reply
//			 XML for you.
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

void SimpleDBClient::SendAndReceive(HTTPRequest& request, HTTPResponse& response,
	int expected_status_code)
{
	SocketStream sock;
	sock.Open(Socket::TypeINET, mHostName, mPort ? mPort : 80);

	// Send() throws exceptions if anything goes wrong.
	request.Send(sock, mTimeout);

	// Reset the response in case it has been used before.
	response.Reset();
	response.Receive(sock, mTimeout);
	std::string response_data((const char *)response.GetBuffer(),
		response.GetSize());
	if(response.GetResponseCode() != expected_status_code)
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, RequestFailedUnexpectedly,
			"Expected a " << expected_status_code << " response but received "
			"a " << response.GetResponseCode() << " instead: " <<
			response_data);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:	 SimpleDBClient::SendAndReceive(HTTPRequest& request,
//			 ptree& response_tree,
//			 const std::string& expected_root_element)
//		Purpose: Private method. Sends the supplied HTTPRequest to
//			 Amazon SimpleDB, parses the response as XML into
//			 the supplied Boost property_tree, and checks that
//			 the root element is what you expected. The root
//			 element is part of the command specification, so
//			 this ensures that the command was processed
//			 correctly by SimpleDB (or the simulator).
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

void SimpleDBClient::SendAndReceiveXML(HTTPRequest& request, ptree& response_tree,
	const std::string& expected_root_element)
{
	HTTPResponse response;
	SendAndReceive(request, response);

	std::string response_data((const char *)response.GetBuffer(),
		response.GetSize());
	std::auto_ptr<std::istringstream> ap_response_stream(
		new std::istringstream(response_data));
	read_xml(*ap_response_stream, response_tree,
		boost::property_tree::xml_parser::trim_whitespace);

	if(response_tree.begin()->first != expected_root_element)
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, UnexpectedResponseData,
			"Expected response to start with <" << expected_root_element <<
			"> but found <" << response_tree.begin()->first << "> instead");
	}

	ASSERT(++(response_tree.begin()) == response_tree.end());
}

// --------------------------------------------------------------------------
//
// Function
//		Name:	 SimpleDBClient::StartRequest(
//			 HTTPRequest::Method method,
//			 const std::string& action)
//		Purpose: Private method. Initialises an HTTPRequest for the
//			 specified method (usually HTTP GET) and action
//			 (e.g. CreateDomain, PutAttributes). You will need to
//			 add any additional parameters specific to that
//			 action to the resulting request, and sign it, before
//			 calling SendAndReceiveXML() to execute it. The
//			 request contains a timestamp, and must be executed
//			 within some time of its creation (15 minutes?) or
//			 SimpleDB will treat it as expired, and refuse to
//			 honour it, to prevent replay attacks.
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

HTTPRequest SimpleDBClient::StartRequest(HTTPRequest::Method method,
	const std::string& action)
{
	HTTPRequest request(method, "/");
	if(!mEndpoint.empty())
	{
		request.SetHostName(mEndpoint);
	}
	else
	{
		request.SetHostName(mHostName);
	}
	request.AddParameter("Action", action);
	request.AddParameter("AWSAccessKeyId", mAccessKey);
	request.AddParameter("SignatureVersion", "2");
	request.AddParameter("SignatureMethod", "HmacSHA256");

	box_time_t timestamp = mFixedTimestamp;
	if(timestamp == 0)
	{
		timestamp = GetCurrentBoxTime();
	}

	// Generate a timestamp of the format: "2010-01-25T15:01:28-07:00"
	// Ideally we'd use GMT as recommended, and end with "Z", but the signed example
	// uses -07:00 instead, so we need to support timezones to replicate it (bah!)
	//
	// http://docs.aws.amazon.com/AmazonSimpleDB/latest/DeveloperGuide/HMACAuth.html#AboutTimestamp
	// http://www.w3.org/TR/xmlschema-2/#dateTime
	//
	// If the timestamp is in timezone -07:00, then it's 7 hours later in GMT,
	// so we add the offset of -7 * 60 minutes and then use gmtime() to get the time
	// components in that timezone.
	std::ostringstream buf;
	time_t seconds = BoxTimeToSeconds(timestamp) + (mOffsetMinutes * 60);
	struct tm tm_now, *tm_ptr = &tm_now;

#ifdef WIN32
	if((tm_ptr = gmtime(&seconds)) == NULL)
#else
	if(gmtime_r(&seconds, &tm_now) == NULL)
#endif
	{
		THROW_SYS_ERROR("Failed to convert timestamp to components",
			CommonException, Internal);
	}

	buf << std::setfill('0');
	buf << std::setw(4) << (tm_ptr->tm_year + 1900) << "-" <<
	       std::setw(2) << (tm_ptr->tm_mon  + 1) << "-" <<
	       std::setw(2) << (tm_ptr->tm_mday) << "T";
	buf << std::setw(2) << tm_ptr->tm_hour << ":" <<
	       std::setw(2) << tm_ptr->tm_min  << ":" <<
	       std::setw(2) << tm_ptr->tm_sec;

	if(mOffsetMinutes)
	{
		std::div_t rem_quo = std::div(mOffsetMinutes, 60);
		int hours = rem_quo.quot;
		int mins = rem_quo.rem;
		buf << std::showpos << std::internal << std::setw(3) << hours << ":" <<
			std::noshowpos << std::setw(2) << mins;
	}
	else
	{
		buf << "Z";
	}

	request.AddParameter("Timestamp", buf.str());
	request.AddParameter("Version", "2009-04-15");
	return request;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:	 SimpleDBClient::ListDomains()
//		Purpose: Returns a list of domains (a vector of strings)
//			 which are defined in SimpleDB for the account that
//			 this client is configured with. This list is global
//			 to the account, so this function takes no
//			 parameters.
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

SimpleDBClient::list_t SimpleDBClient::ListDomains()
{
	HTTPRequest request = StartRequest(HTTPRequest::Method_GET, "ListDomains");
	request.AddParameter("Signature", CalculateSimpleDBSignature(request));

	// Send directly to in-process simulator, useful for debugging.
	// CollectInBufferStream response_buffer;
	// HTTPResponse response(&response_buffer);
	HTTPResponse response;
	ptree response_tree;
	SendAndReceiveXML(request, response_tree, "ListDomainsResponse");

	list_t domains;
	BOOST_FOREACH(ptree::value_type &v,
		response_tree.get_child("ListDomainsResponse.ListDomainsResult"))
	{
		domains.push_back(v.second.data());
	}

	return domains;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:	 SimpleDBClient::CreateDomain()
//		Purpose: Creates a new domain in the SimpleDB domain
//			 namespace for this client's account.
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

void SimpleDBClient::CreateDomain(const std::string& domain_name)
{
	HTTPRequest request = StartRequest(HTTPRequest::Method_GET, "CreateDomain");
	request.AddParameter("DomainName", domain_name);
	request.AddParameter("Signature", CalculateSimpleDBSignature(request));

	HTTPResponse response;
	ptree response_tree;
	SendAndReceiveXML(request, response_tree, "CreateDomainResponse");
}

// --------------------------------------------------------------------------
//
// Function
//		Name:	 SimpleDBClient::GetAttributes(
//			 const std::string& domain_name,
//			 const std::string& item_name)
//		Purpose: Get the attributes of the specified item in the
//			 specified domain (previously created with
//			 CreateDomain).
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

SimpleDBClient::str_map_t SimpleDBClient::GetAttributes(const std::string& domain_name,
	const std::string& item_name)
{
	HTTPRequest request = StartRequest(HTTPRequest::Method_GET, "GetAttributes");
	request.AddParameter("DomainName", domain_name);
	request.AddParameter("ItemName", item_name);
	request.AddParameter("Signature", CalculateSimpleDBSignature(request));

	ptree response_tree;
	SendAndReceiveXML(request, response_tree, "GetAttributesResponse");

	str_map_t attributes;
	BOOST_FOREACH(ptree::value_type &v,
		response_tree.get_child(
			"GetAttributesResponse.GetAttributesResult"))
	{
		std::string name = v.second.get<std::string>("Name");
		std::string value = v.second.get<std::string>("Value");
		attributes[name] = value;
	}

	return attributes;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:	 SimpleDBClient::PutAttributes(
//			 const std::string& domain_name,
//			 const std::string& item_name,
//			 const SimpleDBClient::str_map_t& attributes,
//			 const SimpleDBClient::str_map_t& expected)
//		Purpose: Create or update an item with the specified name,
//			 giving it the specified attributes. If the item
//			 already exists, a new value for any attribute will
//			 always replace any old value, but existing
//			 attributes which are not modified are preserved.
//			 If any expected values are provided, the PUT
//			 operation is conditional on those values, and will
//			 throw an exception if the item has different values
//			 for those attributes.
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

void SimpleDBClient::PutAttributes(const std::string& domain_name,
	const std::string& item_name, const SimpleDBClient::str_map_t& attributes,
		const SimpleDBClient::str_map_t& expected)
{
	HTTPRequest request = StartRequest(HTTPRequest::Method_GET, "PutAttributes");
	request.AddParameter("DomainName", domain_name);
	request.AddParameter("ItemName", item_name);

	int counter = 1;
	for(str_map_t::const_iterator i = attributes.begin(); i != attributes.end(); i++)
	{
		std::ostringstream oss;
		oss << "Attribute.";
		oss << counter++;
		request.AddParameter(oss.str() + ".Name", i->first);
		request.AddParameter(oss.str() + ".Value", i->second);
		request.AddParameter(oss.str() + ".Replace", "true");
	}

	counter = 1;
	for(str_map_t::const_iterator i = expected.begin(); i != expected.end(); i++)
	{
		std::ostringstream oss;
		oss << "Expected.";
		oss << counter++;
		request.AddParameter(oss.str() + ".Name", i->first);
		request.AddParameter(oss.str() + ".Value", i->second);
	}

	request.AddParameter("Signature", CalculateSimpleDBSignature(request));

	ptree response_tree;
	SendAndReceiveXML(request, response_tree, "PutAttributesResponse");
}


