// --------------------------------------------------------------------------
//
// File
//		Name:    testhttpserver.cpp
//		Purpose: Test code for HTTP server class
//		Created: 26/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef HAVE_SIGNAL_H
	#include <signal.h>
#endif

#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <openssl/x509.h>
#include <openssl/hmac.h>

#include "autogen_HTTPException.h"
#include "HTTPQueryDecoder.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "HTTPServer.h"
#include "HTTPTest.h"
#include "IOStreamGetLine.h"
#include "MD5Digest.h"
#include "S3Client.h"
#include "S3Simulator.h"
#include "ServerControl.h"
#include "SimpleDBClient.h"
#include "Test.h"
#include "ZeroStream.h"
#include "decode.h"
#include "encode.h"

#include "MemLeakFindOn.h"

#define SHORT_TIMEOUT 5000
#define LONG_TIMEOUT 300000

using boost::property_tree::ptree;

class TestWebServer : public HTTPServer
{
public:
	TestWebServer()
	: HTTPServer(LONG_TIMEOUT)
	{ }
	~TestWebServer() { }

	virtual void Handle(HTTPRequest &rRequest, HTTPResponse &rResponse);
};

// Build a nice HTML response, so this can also be tested neatly in a browser
void TestWebServer::Handle(HTTPRequest &rRequest, HTTPResponse &rResponse)
{
	// Test redirection mechanism
	if(rRequest.GetRequestURI() == "/redirect")
	{
		rResponse.SetAsRedirect("/redirected");
		return;
	}

	// Set a cookie?
	if(rRequest.GetRequestURI() == "/set-cookie")
	{
		rResponse.SetCookie("SetByServer", "Value1");
	}

	#define DEFAULT_RESPONSE_1 "<html>\n<head><title>TEST SERVER RESPONSE</title></head>\n<body><h1>Test response</h1>\n<p><b>URI:</b> "
	#define DEFAULT_RESPONSE_3 "</p>\n<p><b>Query string:</b> "
	#define DEFAULT_RESPONSE_4 "</p>\n<p><b>Method:</b> "
	#define DEFAULT_RESPONSE_5 "</p>\n<p><b>Decoded query:</b><br>"
	#define DEFAULT_RESPONSE_6 "</p>\n<p><b>Content type:</b> "
	#define DEFAULT_RESPONSE_7 "</p>\n<p><b>Content length:</b> "
	#define DEFAULT_RESPONSE_8 "</p>\n<p><b>Cookies:</b><br>\n"
	#define DEFAULT_RESPONSE_2 "</p>\n</body>\n</html>\n"

	rResponse.SetResponseCode(HTTPResponse::Code_OK);
	rResponse.SetContentType("text/html");
	rResponse.Write(DEFAULT_RESPONSE_1, sizeof(DEFAULT_RESPONSE_1) - 1);
	const std::string &ruri(rRequest.GetRequestURI());
	rResponse.Write(ruri.c_str(), ruri.size());
	rResponse.Write(DEFAULT_RESPONSE_3, sizeof(DEFAULT_RESPONSE_3) - 1);
	const std::string &rquery(rRequest.GetQueryString());
	rResponse.Write(rquery.c_str(), rquery.size());
	rResponse.Write(DEFAULT_RESPONSE_4, sizeof(DEFAULT_RESPONSE_4) - 1);
	{
		const char *m = "????";
		switch(rRequest.GetMethod())
		{
		case HTTPRequest::Method_GET: m = "GET "; break;
		case HTTPRequest::Method_HEAD: m = "HEAD"; break;
		case HTTPRequest::Method_POST: m = "POST"; break;
		default: m = "UNKNOWN";
		}
		rResponse.Write(m, 4);
	}
	rResponse.Write(DEFAULT_RESPONSE_5, sizeof(DEFAULT_RESPONSE_5) - 1);
	{
		const HTTPRequest::Query_t &rquery(rRequest.GetQuery());
		for(HTTPRequest::Query_t::const_iterator i(rquery.begin()); i != rquery.end(); ++i)
		{
			rResponse.Write("\nPARAM:", 7);
			rResponse.Write(i->first.c_str(), i->first.size());
			rResponse.Write("=", 1);
			rResponse.Write(i->second.c_str(), i->second.size());
			rResponse.Write("<br>\n", 4);
		}
	}
	rResponse.Write(DEFAULT_RESPONSE_6, sizeof(DEFAULT_RESPONSE_6) - 1);
	const std::string &rctype(rRequest.GetContentType());
	rResponse.Write(rctype.c_str(), rctype.size());
	rResponse.Write(DEFAULT_RESPONSE_7, sizeof(DEFAULT_RESPONSE_7) - 1);
	{
		char l[32];
		rResponse.Write(l, ::sprintf(l, "%d", rRequest.GetContentLength()));
	}
	rResponse.Write(DEFAULT_RESPONSE_8, sizeof(DEFAULT_RESPONSE_8) - 1);
	const HTTPRequest::CookieJar_t *pcookies = rRequest.GetCookies();
	if(pcookies != 0)
	{
		HTTPRequest::CookieJar_t::const_iterator i(pcookies->begin());
		for(; i != pcookies->end(); ++i)
		{
			char t[512];
			rResponse.Write(t, ::sprintf(t, "COOKIE:%s=%s<br>\n", i->first.c_str(), i->second.c_str()));
		}
	}
	rResponse.Write(DEFAULT_RESPONSE_2, sizeof(DEFAULT_RESPONSE_2) - 1);
}

std::vector<std::string> get_entry_names(const std::vector<S3Client::BucketEntry> entries)
{
	std::vector<std::string> entry_names;
	for(std::vector<S3Client::BucketEntry>::const_iterator i = entries.begin();
		i != entries.end(); i++)
	{
		entry_names.push_back(i->name());

	}
	return entry_names;
}

bool exercise_s3client(S3Client& client)
{
	int num_failures_initial = num_failures;

	HTTPResponse response = client.GetObject("/photos/puppy.jpg");
	TEST_EQUAL(200, response.GetResponseCode());
	std::string response_data((const char *)response.GetBuffer(),
		response.GetSize());
	TEST_EQUAL("omgpuppies!\n", response_data);
	TEST_THAT(!response.IsKeepAlive());

	// make sure that assigning to HTTPResponse does clear stream
	response = client.GetObject("/photos/puppy.jpg");
	TEST_EQUAL(200, response.GetResponseCode());
	response_data = std::string((const char *)response.GetBuffer(),
		response.GetSize());
	TEST_EQUAL("omgpuppies!\n", response_data);
	TEST_THAT(!response.IsKeepAlive());

	response = client.GetObject("/nonexist");
	TEST_EQUAL(404, response.GetResponseCode());
	TEST_THAT(!response.IsKeepAlive());

	FileStream fs("testfiles/dsfdsfs98.fd");
	std::string digest;

	{
		MD5DigestStream digester;
		fs.CopyStreamTo(digester);
		fs.Seek(0, IOStream::SeekType_Absolute);
		digester.Close();
		digest = digester.DigestAsString();
		TEST_EQUAL("dc3b8c5e57e71d31a0a9d7cbeee2e011", digest);
	}

	// The destination file should not exist before we upload it:
	TEST_THAT(!FileExists("testfiles/store/newfile"));
	response = client.PutObject("/newfile", fs);
	TEST_EQUAL(200, response.GetResponseCode());
	TEST_THAT(!response.IsKeepAlive());
	TEST_EQUAL("\"" + digest + "\"", response.GetHeaders().GetHeaderValue("etag"));

	// This will fail if the file was created in the wrong place:
	TEST_THAT(FileExists("testfiles/store/newfile"));

	response = client.GetObject("/newfile");
	TEST_EQUAL(200, response.GetResponseCode());
	TEST_EQUAL(4269, response.GetSize());

	fs.Seek(0, IOStream::SeekType_Absolute);
	TEST_THAT(fs.CompareWith(response));
	TEST_EQUAL("\"" + digest + "\"", response.GetHeaders().GetHeaderValue("etag"));

	// Test that GET requests set the Content-Length header correctly.
	int actual_size = TestGetFileSize("testfiles/dsfdsfs98.fd");
	TEST_THAT(actual_size > 0);
	TEST_EQUAL(actual_size, response.GetContentLength());

	// Try to get it again, with the etag of the existing copy, and check that we get
	// a 304 Not Modified response.
	response = client.GetObject("/newfile", digest);
	TEST_EQUAL(HTTPResponse::Code_NotModified, response.GetResponseCode());

	// There are no examples for 304 Not Modified responses to requests with
	// If-None-Match (ETag match) so clients should not depend on this, so the
	// S3Simulator should return 0 instead of the object size and no ETag, to ensure
	// that any code which tries to use the Content-Length or ETag of such a response
	// will fail.
	TEST_EQUAL(0, response.GetContentLength());
	TEST_EQUAL("", response.GetHeaders().GetHeaderValue("etag", false)); // !required

	// Test that HEAD requests set the Content-Length header correctly. We need the
	// actual object size, not 0, despite there being no content in the response.
	// RFC 2616 section 4.4 says "1.Any response message which "MUST NOT" include a
	// message-body (such as ... any response to a HEAD request) is always terminated
	// by the first empty line after the header fields, regardless of the
	// entity-header fields present in the message... If a Content-Length header field
	// (section 14.13) is present, its decimal value in OCTETs represents both the
	// entity-length and the transfer-length."
	//
	// Also the Amazon Simple Storage Service API Reference, section "HEAD Object"
	// examples show the Content-Length being returned as non-zero for a HEAD request,
	// and ETag being returned too.

	response = client.HeadObject("/newfile");
	TEST_EQUAL(actual_size, response.GetContentLength());
	TEST_EQUAL(200, response.GetResponseCode());
	// We really need the ETag header in response to HEAD requests!
	TEST_EQUAL("\"" + digest + "\"",
		response.GetHeaders().GetHeaderValue("etag", false)); // !required
	// Check that there is NO body. The request should not have been treated as a
	// GET request!
	ZeroStream empty(0);
	TEST_THAT(fs.CompareWith(response));

	// Replace the file contents with a smaller file, check that it works and that
	// the file is truncated at the end of the new data.
	CollectInBufferStream test_data;
	test_data.Write(std::string("hello"));
	test_data.SetForReading();
	response = client.PutObject("/newfile", test_data);
	TEST_EQUAL(200, response.GetResponseCode());
	TEST_EQUAL("\"5d41402abc4b2a76b9719d911017c592\"",
		response.GetHeaders().GetHeaderValue("etag", false)); // !required
	TEST_EQUAL(5, TestGetFileSize("testfiles/store/newfile"));

	// This will fail if the file was created in the wrong place:
	TEST_THAT(FileExists("testfiles/store/newfile"));
	response = client.DeleteObject("/newfile");
	TEST_EQUAL(HTTPResponse::Code_NoContent, response.GetResponseCode());
	TEST_THAT(!FileExists("testfiles/store/newfile"));

	// Try uploading a file in a subdirectory, which should create it implicitly
	// and automatically.
	TEST_EQUAL(0, ObjectExists("testfiles/store/sub"));
	TEST_THAT(!FileExists("testfiles/store/sub/newfile"));
	response = client.PutObject("/sub/newfile", fs);
	TEST_EQUAL(200, response.GetResponseCode());
	response = client.GetObject("/sub/newfile");
	TEST_EQUAL(200, response.GetResponseCode());
	TEST_THAT(fs.CompareWith(response));
	response = client.DeleteObject("/sub/newfile");
	TEST_EQUAL(HTTPResponse::Code_NoContent, response.GetResponseCode());
	TEST_THAT(!FileExists("testfiles/store/sub/newfile"));

	// There is no way to explicitly delete a directory either, so we must do that
	// ourselves
	TEST_THAT(rmdir("testfiles/store/sub") == 0);

	// Test the ListBucket command.
	std::vector<S3Client::BucketEntry> actual_contents;
	std::vector<std::string> actual_common_prefixes;
	TEST_EQUAL(3, client.ListBucket(&actual_contents, &actual_common_prefixes));
	std::vector<std::string> actual_entry_names =
		get_entry_names(actual_contents);

	std::vector<std::string> expected_contents;
	expected_contents.push_back("dsfdsfs98.fd");
	TEST_THAT(test_equal_lists(expected_contents, actual_entry_names));

	std::vector<std::string> expected_common_prefixes;
	expected_common_prefixes.push_back("photos/");
	expected_common_prefixes.push_back("subdir/");
	TEST_THAT(test_equal_lists(expected_common_prefixes, actual_common_prefixes));

	// Test that max_keys works.
	actual_contents.clear();
	actual_common_prefixes.clear();

	bool is_truncated;
	TEST_EQUAL(2,
		client.ListBucket(
			&actual_contents, &actual_common_prefixes,
			"", // prefix
			"/", // delimiter
			&is_truncated,
			2)); // max_keys

	TEST_THAT(is_truncated);
	expected_contents.clear();
	expected_contents.push_back("dsfdsfs98.fd");
	actual_entry_names = get_entry_names(actual_contents);
	TEST_THAT(test_equal_lists(expected_contents, actual_entry_names));

	expected_common_prefixes.clear();
	expected_common_prefixes.push_back("photos/");
	TEST_THAT(test_equal_lists(expected_common_prefixes, actual_common_prefixes));

	// Test that marker works.
	actual_contents.clear();
	actual_common_prefixes.clear();

	TEST_EQUAL(2,
		client.ListBucket(
			&actual_contents, &actual_common_prefixes,
			"", // prefix
			"/", // delimiter
			&is_truncated,
			2, // max_keys
			"photos")); // marker

	TEST_THAT(!is_truncated);
	expected_contents.clear();
	actual_entry_names = get_entry_names(actual_contents);
	TEST_THAT(test_equal_lists(expected_contents, actual_entry_names));

	expected_common_prefixes.push_back("subdir/");
	TEST_THAT(test_equal_lists(expected_common_prefixes, actual_common_prefixes));

	// Test is successful if the number of failures has not increased.
	return (num_failures == num_failures_initial);
}


std::string generate_query_string(const HTTPRequest& request)
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
			TEST_LINE_OR(param_values.find(i->first) == param_values.end(),
				"Multiple values for parameter '" << i->first << "'",
				return "");
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


std::string calculate_s3_signature(const HTTPRequest& request,
	const std::string& aws_secret_access_key)
{
	// This code is very similar to that in S3Client::FinishAndSendRequest.
	// TODO FIXME: factor out the common parts.

	std::ostringstream buffer_to_sign;
	buffer_to_sign << request.GetMethodName() << "\n" <<
		request.GetHeaders().GetHeaderValue("Content-MD5",
			false) << "\n" << // !required
		request.GetContentType() << "\n" <<
		request.GetHeaders().GetHeaderValue("Date",
			true) << "\n"; // required

	// TODO FIXME: add support for X-Amz headers (S3 DG page 38)

	std::string bucket;
	std::string host_header = request.GetHeaders().GetHeaderValue("Host",
		true); // required
	std::string s3suffix = ".s3.amazonaws.com";
	if(host_header.size() > s3suffix.size())
	{
		std::string suffix = host_header.substr(host_header.size() -
			s3suffix.size(), s3suffix.size());
		if (suffix == s3suffix)
		{
			bucket = "/" + host_header.substr(0, host_header.size() -
				s3suffix.size());
		}
	}

	buffer_to_sign << bucket << request.GetRequestURI();

	// TODO FIXME: add support for sub-resources. S3 DG page 36.

	// Thanks to https://gist.github.com/tsupo/112188:
	unsigned int digest_size;
	unsigned char digest_buffer[EVP_MAX_MD_SIZE];
	std::string string_to_sign = buffer_to_sign.str();

	HMAC(EVP_sha1(),
		aws_secret_access_key.c_str(), aws_secret_access_key.size(),
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


// http://docs.aws.amazon.com/AmazonSimpleDB/latest/DeveloperGuide/HMACAuth.html
std::string calculate_simpledb_signature(const HTTPRequest& request,
	const std::string& aws_secret_access_key)
{
	// This code is very similar to that in S3Client::FinishAndSendRequest,
	// but using EVP_sha256 instead of EVP_sha1. TODO FIXME: factor out the
	// common parts.
	std::string query_string = generate_query_string(request);
	TEST_THAT_OR(query_string != "", return "");

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
		aws_secret_access_key.c_str(), aws_secret_access_key.size(),
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

bool add_simpledb_signature(HTTPRequest& request, const std::string& aws_secret_access_key)
{
	std::string signature = calculate_simpledb_signature(request,
		aws_secret_access_key);
	request.SetParameter("Signature", signature);
	return !signature.empty();
}

bool send_and_receive(HTTPRequest& request, HTTPResponse& response,
	int expected_status_code = 200)
{
	SocketStream sock;
	sock.Open(Socket::TypeINET, "localhost", 1080);
	request.Send(sock, LONG_TIMEOUT);

	response.Reset();
	response.Receive(sock, LONG_TIMEOUT);
	std::string response_data((const char *)response.GetBuffer(),
		response.GetSize());
	TEST_EQUAL_LINE(expected_status_code, response.GetResponseCode(),
		response_data);
	return (response.GetResponseCode() == expected_status_code);
}

bool parse_xml_response(HTTPResponse& response, ptree& response_tree,
	const std::string& expected_root_element)
{
	std::string response_data((const char *)response.GetBuffer(),
		response.GetSize());
	std::auto_ptr<std::istringstream> ap_response_stream(
		new std::istringstream(response_data));
	read_xml(*ap_response_stream, response_tree,
		boost::property_tree::xml_parser::trim_whitespace);

	TEST_EQUAL_OR(expected_root_element, response_tree.begin()->first, return false);
	TEST_LINE(++(response_tree.begin()) == response_tree.end(),
		"There should only be one item in the response tree root");

	return true;
}

bool send_and_receive_xml(HTTPRequest& request, ptree& response_tree,
	const std::string& expected_root_element)
{
	HTTPResponse response;
	TEST_THAT_OR(send_and_receive(request, response), return false);
	return parse_xml_response(response, response_tree, expected_root_element);
}

typedef std::multimap<std::string, std::string> multimap_t;
typedef multimap_t::value_type attr_t;

std::vector<std::string> simpledb_list_domains(const std::string& access_key,
	const std::string& secret_key)
{
	HTTPRequest request(HTTPRequest::Method_GET, "/");
	request.SetHostName(SIMPLEDB_SIMULATOR_HOST);
	request.AddParameter("Action", "ListDomains");
	request.AddParameter("AWSAccessKeyId", access_key);
	request.AddParameter("SignatureVersion", "2");
	request.AddParameter("SignatureMethod", "HmacSHA256");
	request.AddParameter("Timestamp", "2010-01-25T15:01:28-07:00");
	request.AddParameter("Version", "2009-04-15");

	TEST_THAT_OR(add_simpledb_signature(request, secret_key),
		return std::vector<std::string>());

	ptree response_tree;
	TEST_THAT(send_and_receive_xml(request, response_tree, "ListDomainsResponse"));

	std::vector<std::string> domains;
	BOOST_FOREACH(ptree::value_type &v,
		response_tree.get_child("ListDomainsResponse.ListDomainsResult"))
	{
		domains.push_back(v.second.data());
	}

	return domains;
}

HTTPRequest simpledb_get_attributes_request(const std::string& access_key,
	const std::string& secret_key)
{
	HTTPRequest request(HTTPRequest::Method_GET, "/");
	request.SetHostName(SIMPLEDB_SIMULATOR_HOST);
	request.AddParameter("Action", "GetAttributes");
	request.AddParameter("DomainName", "MyDomain");
	request.AddParameter("ItemName", "JumboFez");
	request.AddParameter("AWSAccessKeyId", access_key);
	request.AddParameter("SignatureVersion", "2");
	request.AddParameter("SignatureMethod", "HmacSHA256");
	request.AddParameter("Timestamp", "2010-01-25T15:01:28-07:00");
	request.AddParameter("Version", "2009-04-15");
	TEST_THAT(add_simpledb_signature(request, secret_key));
	return request;
}

bool simpledb_get_attributes_error(const std::string& access_key,
	const std::string& secret_key, int expected_status_code)
{
	HTTPRequest request = simpledb_get_attributes_request(access_key, secret_key);
	HTTPResponse response;
	TEST_THAT_OR(send_and_receive(request, response, expected_status_code),
		return false);
	// Nothing else to check: there is no XML
	return true;
}

bool simpledb_get_attributes(const std::string& access_key, const std::string& secret_key,
	const multimap_t& const_attributes)
{
	HTTPRequest request = simpledb_get_attributes_request(access_key, secret_key);

	ptree response_tree;
	TEST_THAT_OR(send_and_receive_xml(request, response_tree,
		"GetAttributesResponse"), return false);

	// Check that all attributes were written correctly
	TEST_EQUAL_LINE(const_attributes.size(),
		response_tree.get_child("GetAttributesResponse.GetAttributesResult").size(),
		"Wrong number of attributes in response");

	bool all_match = (const_attributes.size() ==
		response_tree.get_child("GetAttributesResponse.GetAttributesResult").size());

	multimap_t attributes = const_attributes;
	multimap_t::iterator i = attributes.begin();
	BOOST_FOREACH(ptree::value_type &v,
		response_tree.get_child(
			"GetAttributesResponse.GetAttributesResult"))
	{
		std::string name = v.second.get<std::string>("Name");
		std::string value = v.second.get<std::string>("Value");
		if(i == attributes.end())
		{
			TEST_EQUAL_LINE("", name, "Unexpected attribute name");
			TEST_EQUAL_LINE("", value, "Unexpected attribute value");
			all_match = false;
		}
		else
		{
			TEST_EQUAL_LINE(i->first, name, "Wrong attribute name");
			TEST_EQUAL_LINE(i->second, value, "Wrong attribute value");
			all_match &= (i->first == name);
			all_match &= (i->second == value);
			i++;
		}
	}

	return all_match;
}

#define EXAMPLE_S3_ACCESS_KEY "0PN5J17HBGZHT7JJ3X82"
#define EXAMPLE_S3_SECRET_KEY "uV3F3YluFJax1cknvbcGwgjvx4QpvB+leU8dUj2o"

bool test_httpserver()
{
	SETUP();

	{
		FileStream fs("testfiles/dsfdsfs98.fd");
		MD5DigestStream digester;
		fs.CopyStreamTo(digester);
		fs.Seek(0, IOStream::SeekType_Absolute);
		digester.Close();
		std::string digest = digester.DigestAsString();
		TEST_EQUAL("dc3b8c5e57e71d31a0a9d7cbeee2e011", digest);
	}

	// Test that HTTPRequest with parameters is encoded correctly
	{
		HTTPRequest request(HTTPRequest::Method_GET, "/newfile");
		CollectInBufferStream request_buffer;
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		request_buffer.SetForReading();

		std::string request_str((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		const std::string expected_str("GET /newfile HTTP/1.1\r\nConnection: close\r\n\r\n");
		TEST_EQUAL(expected_str, request_str);

		request.AddParameter("foo", "Bar");
		request_buffer.Reset();
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		request_str = std::string((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		TEST_EQUAL("GET /newfile?foo=Bar HTTP/1.1\r\nConnection: close\r\n\r\n", request_str);

		request.AddParameter("foo", "baz");
		request_buffer.Reset();
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		request_str = std::string((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		TEST_EQUAL("GET /newfile?foo=Bar&foo=baz HTTP/1.1\r\nConnection: close\r\n\r\n", request_str);

		request.SetParameter("whee", "bonk");
		request_buffer.Reset();
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		request_str = std::string((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		TEST_EQUAL("GET /newfile?foo=Bar&foo=baz&whee=bonk HTTP/1.1\r\nConnection: close\r\n\r\n", request_str);

		request.SetParameter("foo", "bolt");
		request_buffer.Reset();
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		request_str = std::string((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		TEST_EQUAL("GET /newfile?foo=bolt&whee=bonk HTTP/1.1\r\nConnection: close\r\n\r\n", request_str);

		HTTPRequest newreq = request;
		TEST_EQUAL("bolt", newreq.GetParameterString("foo"));
		TEST_EQUAL("bonk", newreq.GetParameterString("whee"));
		TEST_EQUAL("blue", newreq.GetParameterString("colour", "blue"));
		TEST_CHECK_THROWS(newreq.GetParameterString("colour"), HTTPException,
			ParameterNotFound);
	}

	// Test that HTTPRequest can be written to and read from a stream.
	for(int enable_continue = 0; enable_continue < 2; enable_continue++)
	{
		HTTPRequest request(HTTPRequest::Method_PUT, "/newfile");
		request.SetHostName("quotes.s3.amazonaws.com");
		// Write headers in lower case.
		request.AddHeader("date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("authorization",
			"AWS " EXAMPLE_S3_ACCESS_KEY ":XtMYZf0hdOo4TdPYQknZk0Lz7rw=");
		request.AddHeader("Content-Type", "text/plain");
		request.SetClientKeepAliveRequested(true);

		// First stream just the headers into a CollectInBufferStream, and check the
		// exact contents written:
		CollectInBufferStream request_buffer;
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite,
			(bool)enable_continue);
		request_buffer.SetForReading();
		std::string request_str((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		std::string expected_str(
			"PUT /newfile HTTP/1.1\r\n"
			"Content-Type: text/plain\r\n"
			"Host: quotes.s3.amazonaws.com\r\n"
			"Connection: keep-alive\r\n"
			"date: Wed, 01 Mar  2006 12:00:00 GMT\r\n"
			"authorization: AWS " EXAMPLE_S3_ACCESS_KEY ":XtMYZf0hdOo4TdPYQknZk0Lz7rw=\r\n");
		if(enable_continue == 1)
		{
			expected_str += "Expect: 100-continue\r\n";
		}
		TEST_EQUAL(expected_str + "\r\n", request_str);

		// Now stream the entire request into the CollectInBufferStream. Because there
		// isn't an HTTP server to respond to us, we can't use SendWithStream, so just
		// send the headers and then the content separately:
		request_buffer.Reset();
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite,
			(bool)enable_continue);
		FileStream fs("testfiles/photos/puppy.jpg");
		fs.CopyStreamTo(request_buffer);
		request_buffer.SetForReading();

		IOStreamGetLine getLine(request_buffer);
		HTTPRequest request2;
		TEST_THAT(request2.Receive(getLine, IOStream::TimeOutInfinite));

		TEST_EQUAL(HTTPRequest::Method_PUT, request2.GetMethod());
		TEST_EQUAL("PUT", request2.GetMethodName());
		TEST_EQUAL("/newfile", request2.GetRequestURI());
		TEST_EQUAL("quotes.s3.amazonaws.com", request2.GetHostName());
		TEST_EQUAL(80, request2.GetHostPort());
		TEST_EQUAL("", request2.GetQueryString());
		TEST_EQUAL("text/plain", request2.GetContentType());
		// Content-Length was not known when the stream was sent, so it should
		// be unknown in the received stream too (certainly before it has all
		// been read!)
		TEST_EQUAL(-1, request2.GetContentLength());
		const HTTPHeaders& headers(request2.GetHeaders());
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT",
			headers.GetHeaderValue("Date"));
		TEST_EQUAL("AWS " EXAMPLE_S3_ACCESS_KEY ":XtMYZf0hdOo4TdPYQknZk0Lz7rw=",
			headers.GetHeaderValue("Authorization"));
		TEST_THAT(request2.GetClientKeepAliveRequested());
		TEST_EQUAL((bool)enable_continue, request2.IsExpectingContinue());

		CollectInBufferStream request_data;
		request2.ReadContent(request_data, IOStream::TimeOutInfinite);
		TEST_EQUAL(fs.GetPosition(), request_data.GetPosition());
		request_data.SetForReading();
		fs.Seek(0, IOStream::SeekType_Absolute);
		TEST_THAT(fs.CompareWith(request_data, IOStream::TimeOutInfinite));
	}

	// Test that HTTPResponse can be written to and read from a stream.
	// TODO FIXME: we should stream the response instead of buffering it, on both
	// sides (send and receive).
	{
		// Stream it to a CollectInBufferStream
		CollectInBufferStream response_buffer;

		HTTPResponse response(&response_buffer);
		FileStream fs("testfiles/photos/puppy.jpg");
		// Write headers in lower case.
		response.SetResponseCode(HTTPResponse::Code_OK);
		response.AddHeader("date", "Wed, 01 Mar  2006 12:00:00 GMT");
		response.AddHeader("authorization",
			"AWS " EXAMPLE_S3_ACCESS_KEY ":XtMYZf0hdOo4TdPYQknZk0Lz7rw=");
		response.AddHeader("content-type", "text/perl");
		fs.CopyStreamTo(response);
		response.Send();
		response_buffer.SetForReading();

		HTTPResponse response2;
		response2.Receive(response_buffer);

		TEST_EQUAL(200, response2.GetResponseCode());
		TEST_EQUAL("text/perl", response2.GetContentType());

		// TODO FIXME: Content-Length was not known when the stream was sent,
		// so it should be unknown in the received stream too (certainly before
		// it has all been read!) This is currently wrong because we read the
		// entire response into memory immediately.
		TEST_EQUAL(fs.GetPosition(), response2.GetContentLength());

		HTTPHeaders& headers(response2.GetHeaders());
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT",
			headers.GetHeaderValue("Date"));
		TEST_EQUAL("AWS " EXAMPLE_S3_ACCESS_KEY ":XtMYZf0hdOo4TdPYQknZk0Lz7rw=",
			headers.GetHeaderValue("Authorization"));

		CollectInBufferStream response_data;
		// request2.ReadContent(request_data, IOStream::TimeOutInfinite);
		response2.CopyStreamTo(response_data);
		TEST_EQUAL(fs.GetPosition(), response_data.GetPosition());
		response_data.SetForReading();
		fs.Seek(0, IOStream::SeekType_Absolute);
		TEST_THAT(fs.CompareWith(response_data, IOStream::TimeOutInfinite));
	}

#ifndef WIN32
	TEST_THAT(system("rm -rf *.memleaks") == 0);
#endif

	// Start the server
	int pid = StartDaemon(0, TEST_EXECUTABLE " server testfiles/httpserver.conf",
		"testfiles/httpserver.pid");
	TEST_THAT_OR(pid > 0, return 1);

	// Run the request script
	TEST_THAT(::system("perl testfiles/testrequests.pl") == 0);

	#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
	#endif

#ifdef ENABLE_KEEPALIVE_SUPPORT // incomplete, need chunked encoding support
	SocketStream sock;
	sock.Open(Socket::TypeINET, "localhost", 1080);

	for (int i = 0; i < 4; i++)
	{
		HTTPRequest request(HTTPRequest::Method_GET,
			"/test-one/34/341s/234?p1=vOne&p2=vTwo");

		if (i < 2)
		{
			// first set of passes has keepalive off by default,
			// so when i == 1 the socket has already been closed
			// by the server, and we'll get -EPIPE when we try
			// to send the request.
			request.SetClientKeepAliveRequested(false);
		}
		else
		{
			request.SetClientKeepAliveRequested(true);
		}

		if (i == 1)
		{
			sleep(1); // need time for our process to realise
			// that the peer has died, otherwise no SIGPIPE :(
			TEST_CHECK_THROWS(
				request.Send(sock, SHORT_TIMEOUT),
				ConnectionException, SocketWriteError);
			sock.Close();
			sock.Open(Socket::TypeINET, "localhost", 1080);
			continue;
		}
		else
		{
			request.Send(sock, SHORT_TIMEOUT);
		}

		HTTPResponse response;
		response.Receive(sock, SHORT_TIMEOUT);

		TEST_THAT(response.GetResponseCode() == HTTPResponse::Code_OK);
		TEST_THAT(response.GetContentType() == "text/html");

		IOStreamGetLine getline(response);
		std::string line;

		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<html>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<head><title>TEST SERVER RESPONSE</title></head>",
			line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<body><h1>Test response</h1>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>URI:</b> /test-one/34/341s/234</p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Query string:</b> p1=vOne&p2=vTwo</p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Method:</b> GET </p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Decoded query:</b><br>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("PARAM:p1=vOne<br>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("PARAM:p2=vTwo<br></p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Content type:</b> </p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Content length:</b> -1</p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Cookies:</b><br>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("</p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("</body>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("</html>", line);

		if(!response.IsKeepAlive())
		{
			BOX_TRACE("Server will close the connection, closing our end too.");
			sock.Close();
			sock.Open(Socket::TypeINET, "localhost", 1080);
		}
		else
		{
			BOX_TRACE("Server will keep the connection open for more requests.");
		}
	}

	sock.Close();
#endif // ENABLE_KEEPALIVE_SUPPORT

	// Kill it
	TEST_THAT(StopDaemon(pid, "testfiles/httpserver.pid",
		"generic-httpserver.memleaks", true));

	// Copy testfiles/puppy.jpg to testfiles/store/photos/puppy.jpg
	{
		TEST_THAT(::mkdir("testfiles/store/photos", 0755) == 0);
		FileStream in("testfiles/puppy.jpg", O_RDONLY);
		FileStream out("testfiles/store/photos/puppy.jpg", O_CREAT | O_WRONLY);
		in.CopyStreamTo(out);
	}

	// This is the example from the Amazon S3 Developers Guide, page 31.
	// Correct, official signature should succeed, with lower-case headers.
	{
		// http://docs.amazonwebservices.com/AmazonS3/2006-03-01/RESTAuthentication.html
		HTTPRequest request(HTTPRequest::Method_GET, "/photos/puppy.jpg");
		request.SetHostName("johnsmith.s3.amazonaws.com");
		request.AddHeader("date", "Tue, 27 Mar 2007 19:36:42 +0000");
		std::string signature = calculate_s3_signature(request,
			EXAMPLE_S3_SECRET_KEY);
		TEST_EQUAL(signature, "xXjDGYUmKxnwqr5KXNPGldn5LbA=");
		request.AddHeader("authorization",
			"AWS " EXAMPLE_S3_ACCESS_KEY ":" + signature);

		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");

		CollectInBufferStream response_buffer;
		HTTPResponse response(&response_buffer);

		simulator.Handle(request, response);
		TEST_EQUAL(200, response.GetResponseCode());

		std::string response_data((const char *)response.GetBuffer(),
			response.GetSize());
		TEST_EQUAL("omgpuppies!\n", response_data);
	}

	// Modified signature should fail.
	{
		// http://docs.amazonwebservices.com/AmazonS3/2006-03-01/RESTAuthentication.html
		HTTPRequest request(HTTPRequest::Method_GET, "/photos/puppy.jpg");
		request.SetHostName("johnsmith.s3.amazonaws.com");
		request.AddHeader("date", "Tue, 27 Mar 2007 19:36:42 +0000");
		request.AddHeader("authorization",
			"AWS " EXAMPLE_S3_ACCESS_KEY ":xXjDGYUmKxnwqr5KXNPGldn5LbB=");

		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");

		CollectInBufferStream response_buffer;
		HTTPResponse response(&response_buffer);

		simulator.Handle(request, response);
		TEST_EQUAL(401, response.GetResponseCode());

		std::string response_data((const char *)response.GetBuffer(),
			response.GetSize());
		TEST_EQUAL("<html><head>"
			"<title>Internal Server Error</title></head>\n"
			"<h1>Internal Server Error</h1>\n"
			"<p>An error occurred while processing the request:</p>\n"
			"<pre>HTTPException(AuthenticationFailed): "
			"Authentication code mismatch: expected AWS 0PN5J17HBGZHT7JJ3X82"
			":xXjDGYUmKxnwqr5KXNPGldn5LbA= but received AWS "
			"0PN5J17HBGZHT7JJ3X82:xXjDGYUmKxnwqr5KXNPGldn5LbB=</pre>\n"
			"<p>Please try again later.</p></body>\n"
			"</html>\n", response_data);
	}

	// Copy testfiles/dsfdsfs98.fd to testfiles/store/dsfdsfs98.fd
	{
		FileStream in("testfiles/dsfdsfs98.fd", O_RDONLY);
		FileStream out("testfiles/store/dsfdsfs98.fd", O_CREAT | O_WRONLY);
		in.CopyStreamTo(out);
	}

	// Tests for the S3Simulator ListBucket implementation
	{
		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");

		// List contents of bucket
		HTTPRequest request(HTTPRequest::Method_GET, "/");
		request.SetParameter("delimiter", "/");
		request.SetHostName("johnsmith.s3.amazonaws.com");
		request.AddHeader("date", "Tue, 27 Mar 2007 19:36:42 +0000");
		std::string signature = calculate_s3_signature(request,
			EXAMPLE_S3_SECRET_KEY);
		request.AddHeader("authorization",
			"AWS " EXAMPLE_S3_ACCESS_KEY ":" + signature);

		HTTPResponse response;
		simulator.Handle(request, response);
		TEST_EQUAL(HTTPResponse::Code_OK, response.GetResponseCode());
		std::vector<std::string> expected_contents;

		if(response.GetResponseCode() == HTTPResponse::Code_OK)
		{
			ptree response_tree;
			TEST_THAT(parse_xml_response(response, response_tree,
				"ListBucketResult"));
			// A response containing a single item should not be truncated!
			TEST_EQUAL("false",
				response_tree.get<std::string>(
					"ListBucketResult.IsTruncated"));

			// Iterate over all the children of the ListBucketResult, looking for
			// nodes called "Contents", and examine them.
			std::vector<std::string> contents;
			BOOST_FOREACH(ptree::value_type &v,
				response_tree.get_child("ListBucketResult"))
			{
				if(v.first == "Contents")
				{
					std::string name = v.second.get<std::string>("Key");
					contents.push_back(name);
					if(name == "dsfdsfs98.fd")
					{
						TEST_EQUAL("&quot;dc3b8c5e57e71d31a0a9d7cbeee2e011&quot;",
							v.second.get<std::string>("ETag"));
						TEST_EQUAL("4269", v.second.get<std::string>("Size"));
					}
				}
			}

			expected_contents.push_back("dsfdsfs98.fd");
			TEST_THAT(test_equal_lists(expected_contents, contents));

			int num_common_prefixes = 0;
			BOOST_FOREACH(ptree::value_type &v,
				response_tree.get_child("ListBucketResult.CommonPrefixes"))
			{
				num_common_prefixes++;
				TEST_EQUAL("Prefix", v.first);
				std::string expected_name;
				if(num_common_prefixes == 1)
				{
					expected_name = "photos/";
				}
				else
				{
					expected_name = "subdir/";
				}
				TEST_EQUAL_LINE(expected_name, v.second.data(),
					"line " << num_common_prefixes);
			}
			TEST_EQUAL(2, num_common_prefixes);
		}
	}

	// Test the S3Simulator's implementation of PUT file uploads
	{
		HTTPRequest request(HTTPRequest::Method_PUT, "/newfile");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("date", "Wed, 01 Mar  2006 12:00:00 GMT");
		// request.AddHeader("Content-Type", "text/plain");

		std::string signature = calculate_s3_signature(request,
			EXAMPLE_S3_SECRET_KEY);
		TEST_EQUAL(signature, "XtMYZf0hdOo4TdPYQknZk0Lz7rw=");
		request.AddHeader("authorization", "AWS " EXAMPLE_S3_ACCESS_KEY ":" +
			signature);

		FileStream fs("testfiles/dsfdsfs98.fd");
		request.SetDataStream(&fs);
		request.SetForReading();

		CollectInBufferStream response_buffer;
		HTTPResponse response(&response_buffer);

		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");
		simulator.Handle(request, response);

		TEST_EQUAL(200, response.GetResponseCode());
		TEST_EQUAL("LriYPLdmOdAiIfgSm/F1YsViT1LW94/xUQxMsF7xiEb1a0wiIOIxl+zbwZ163pt7",
			response.GetHeaderValue("x-amz-id-2"));
		TEST_EQUAL("F2A8CCCA26B4B26D", response.GetHeaderValue("x-amz-request-id"));
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT", response.GetHeaderValue("Date"));
		TEST_EQUAL("Sun, 1 Jan 2006 12:00:00 GMT", response.GetHeaderValue("Last-Modified"));
		TEST_EQUAL("\"dc3b8c5e57e71d31a0a9d7cbeee2e011\"", response.GetHeaderValue("ETag"));
		TEST_EQUAL("", response.GetContentType());
		TEST_EQUAL("AmazonS3", response.GetHeaderValue("Server"));
		TEST_EQUAL(0, response.GetSize());

		FileStream f1("testfiles/dsfdsfs98.fd");
		FileStream f2("testfiles/store/newfile");
		TEST_THAT(f1.CompareWith(f2));
		TEST_EQUAL(0, EMU_UNLINK("testfiles/store/newfile"));
	}

	// S3Client tests with S3Simulator in-process server for debugging
	{
		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");
		S3Client client(&simulator, "johnsmith.s3.amazonaws.com",
			EXAMPLE_S3_ACCESS_KEY, EXAMPLE_S3_SECRET_KEY);
		TEST_THAT(exercise_s3client(client));
	}

	// Start the S3Simulator server
	pid = StartDaemon(0, TEST_EXECUTABLE " s3server testfiles/s3simulator.conf",
		"testfiles/s3simulator.pid");
	TEST_THAT_OR(pid > 0, return 1);

	// This is the example from the Amazon S3 Developers Guide, page 31
	{
		HTTPRequest request(HTTPRequest::Method_GET, "/photos/puppy.jpg");
		request.SetHostName("johnsmith.s3.amazonaws.com");
		request.AddHeader("date", "Tue, 27 Mar 2007 19:36:42 +0000");
		request.AddHeader("authorization",
			"AWS " EXAMPLE_S3_ACCESS_KEY ":xXjDGYUmKxnwqr5KXNPGldn5LbA=");

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response));
	}

	// Test that requests for nonexistent files correctly return a 404 error
	{
		HTTPRequest request(HTTPRequest::Method_GET, "/nonexist");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.SetClientKeepAliveRequested(true);

		std::string signature = calculate_s3_signature(request,
			EXAMPLE_S3_SECRET_KEY);
		TEST_EQUAL(signature, "0cSX/YPdtXua1aFFpYmH1tc0ajA=");
		request.AddHeader("authorization", "AWS " EXAMPLE_S3_ACCESS_KEY ":" +
			signature);

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response, 404));
		TEST_THAT(!response.IsKeepAlive());
	}

#ifndef WIN32 // much harder to make files inaccessible on WIN32
	// Make file inaccessible, should cause server to return a 403 error,
	// unless of course the test is run as root :)
	{
		TEST_THAT(chmod("testfiles/store/dsfdsfs98.fd", 0) == 0);
		HTTPRequest request(HTTPRequest::Method_GET, "/dsfdsfs98.fd");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS " EXAMPLE_S3_ACCESS_KEY
			":NO9tjQuMCK83z2VZFaJOGKeDi7M=");
		request.SetClientKeepAliveRequested(true);

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response, 403));
		TEST_THAT(chmod("testfiles/store/dsfdsfs98.fd", 0755) == 0);
	}
#endif

	{
		HTTPRequest request(HTTPRequest::Method_GET, "/dsfdsfs98.fd");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS " EXAMPLE_S3_ACCESS_KEY
			":NO9tjQuMCK83z2VZFaJOGKeDi7M=");
		request.SetClientKeepAliveRequested(true);

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response));

		TEST_EQUAL("qBmKRcEWBBhH6XAqsKU/eg24V3jf/kWKN9dJip1L/FpbYr9FDy7wWFurfdQOEMcY",
			response.GetHeaderValue("x-amz-id-2"));
		TEST_EQUAL("F2A8CCCA26B4B26D", response.GetHeaderValue("x-amz-request-id"));
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT", response.GetHeaderValue("Date"));
		TEST_EQUAL("Sun, 1 Jan 2006 12:00:00 GMT", response.GetHeaderValue("Last-Modified"));
		TEST_EQUAL("\"dc3b8c5e57e71d31a0a9d7cbeee2e011\"", response.GetHeaderValue("ETag"));
		TEST_EQUAL("text/plain", response.GetContentType());
		TEST_EQUAL("AmazonS3", response.GetHeaderValue("Server"));
		TEST_THAT(!response.IsKeepAlive());

		FileStream file("testfiles/dsfdsfs98.fd");
		TEST_THAT(file.CompareWith(response));
	}

	{
		SocketStream sock;
		sock.Open(Socket::TypeINET, "localhost", 1080);

		HTTPRequest request(HTTPRequest::Method_PUT, "/newfile");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS " EXAMPLE_S3_ACCESS_KEY
			":kfY1m6V3zTufRy2kj92FpQGKz4M=");
		request.AddHeader("Content-Type", "text/plain");
		FileStream fs("testfiles/dsfdsfs98.fd");
		HTTPResponse response;
		request.SendWithStream(sock, LONG_TIMEOUT, &fs, response);
		std::string value;
		TEST_EQUAL(200, response.GetResponseCode());
		TEST_EQUAL("LriYPLdmOdAiIfgSm/F1YsViT1LW94/xUQxMsF7xiEb1a0wiIOIxl+zbwZ163pt7", response.GetHeaderValue("x-amz-id-2"));
		TEST_EQUAL("F2A8CCCA26B4B26D", response.GetHeaderValue("x-amz-request-id"));
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT", response.GetHeaderValue("Date"));
		TEST_EQUAL("Sun, 1 Jan 2006 12:00:00 GMT", response.GetHeaderValue("Last-Modified"));
		TEST_EQUAL("\"dc3b8c5e57e71d31a0a9d7cbeee2e011\"", response.GetHeaderValue("ETag"));
		TEST_EQUAL("", response.GetContentType());
		TEST_EQUAL("AmazonS3", response.GetHeaderValue("Server"));
		TEST_EQUAL(0, response.GetSize());
		TEST_THAT(!response.IsKeepAlive());

		FileStream f1("testfiles/dsfdsfs98.fd");
		FileStream f2("testfiles/store/newfile");
		TEST_THAT(f1.CompareWith(f2));
		TEST_THAT(EMU_UNLINK("testfiles/store/newfile") == 0);
	}

	// S3Client tests with S3Simulator daemon for realism
	{
		S3Client client("localhost", 1080, EXAMPLE_S3_ACCESS_KEY,
			EXAMPLE_S3_SECRET_KEY, "johnsmith.s3.amazonaws.com");
		TEST_THAT(exercise_s3client(client));
	}

	// Test the HTTPQueryDecoder::URLEncode method.
	TEST_EQUAL("AZaz09-_.~", HTTPQueryDecoder::URLEncode("AZaz09-_.~"));
	TEST_EQUAL("%00%01%FF",
		HTTPQueryDecoder::URLEncode(std::string("\0\x01\xff", 3)));

	// Test that we can calculate the correct signature for a known request:
	// http://docs.aws.amazon.com/AWSECommerceService/latest/DG/rest-signature.html
	{
		HTTPRequest request(HTTPRequest::Method_GET, "/onca/xml");
		request.SetHostName("webservices.amazon.com");
		request.AddParameter("Service", "AWSECommerceService");
		request.AddParameter("AWSAccessKeyId", "AKIAIOSFODNN7EXAMPLE");
		request.AddParameter("AssociateTag", "mytag-20");
		request.AddParameter("Operation", "ItemLookup");
		request.AddParameter("ItemId", "0679722769");
		request.AddParameter("ResponseGroup",
			"Images,ItemAttributes,Offers,Reviews");
		request.AddParameter("Version", "2013-08-01");
		request.AddParameter("Timestamp", "2014-08-18T12:00:00Z");

		std::string auth_code = calculate_simpledb_signature(request,
			"1234567890");
		TEST_EQUAL("j7bZM0LXZ9eXeZruTqWm2DIvDYVUU3wxPPpp+iXxzQc=", auth_code);
	}

	// Test the S3Simulator's implementation of SimpleDB
	{
		std::string access_key = EXAMPLE_S3_ACCESS_KEY;
		std::string secret_key = EXAMPLE_S3_SECRET_KEY;

		HTTPRequest request(HTTPRequest::Method_GET, "/");
		request.SetHostName(SIMPLEDB_SIMULATOR_HOST);

		request.AddParameter("Action", "ListDomains");
		request.AddParameter("AWSAccessKeyId", access_key);
		request.AddParameter("SignatureVersion", "2");
		request.AddParameter("SignatureMethod", "HmacSHA256");
		request.AddParameter("Timestamp", "2010-01-25T15:01:28-07:00");
		request.AddParameter("Version", "2009-04-15");
		TEST_THAT(add_simpledb_signature(request, secret_key));

		// Send directly to in-process simulator, useful for debugging.
		// CollectInBufferStream response_buffer;
		// HTTPResponse response(&response_buffer);
		HTTPResponse response;

		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");
		simulator.Handle(request, response);
		std::string response_data((const char *)response.GetBuffer(),
			response.GetSize());
		TEST_EQUAL_LINE(200, response.GetResponseCode(), response_data);

		// Send to out-of-process simulator, useful for testing HTTP
		// implementation.
		TEST_THAT(send_and_receive(request, response));

		// Check that there are no existing domains at the start
		std::vector<std::string> domains = simpledb_list_domains(access_key, secret_key);
		std::vector<std::string> expected_domains;
		TEST_THAT(test_equal_lists(expected_domains, domains));

		// Create a domain
		request.SetParameter("Action", "CreateDomain");
		request.SetParameter("DomainName", "MyDomain");
		TEST_THAT(add_simpledb_signature(request, secret_key));

		ptree response_tree;
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"CreateDomainResponse"));

		// List domains again, check that our new domain is present.
		domains = simpledb_list_domains(access_key, secret_key);
		expected_domains.push_back("MyDomain");
		TEST_THAT(test_equal_lists(expected_domains, domains));

		// Create the same domain again. "CreateDomain is an idempotent operation;
		// running it multiple times using the same domain name will not result in
		// an error response."
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"CreateDomainResponse"));

		// List domains again, check that our new domain is present only once
		// (it wasn't created a second time). Therefore expected_domains is the
		// same as it was above.
		domains = simpledb_list_domains(access_key, secret_key);
		TEST_THAT(test_equal_lists(expected_domains, domains));

		// Create an item
		request.SetParameter("Action", "PutAttributes");
		request.SetParameter("DomainName", "MyDomain");
		request.SetParameter("ItemName", "JumboFez");
		request.SetParameter("Attribute.1.Name", "Color");
		request.SetParameter("Attribute.1.Value", "Blue");
		request.SetParameter("Attribute.2.Name", "Size");
		request.SetParameter("Attribute.2.Value", "Med");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"PutAttributesResponse"));

		// Get the item back, and check that all attributes were written
		// correctly.
		multimap_t expected_attrs;
		expected_attrs.insert(attr_t("Color", "Blue"));
		expected_attrs.insert(attr_t("Size", "Med"));
		TEST_THAT(simpledb_get_attributes(access_key, secret_key, expected_attrs));

		// Add more attributes. The Size attribute is added with the Replace
		// option, so it replaces the previous value. The Color attribute is not,
		// so it adds another value.
		request.SetParameter("Attribute.1.Value", "Not Blue");
		request.SetParameter("Attribute.1.Replace", "true");
		request.SetParameter("Attribute.2.Value", "Large");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"PutAttributesResponse"));

		// Check that all attributes were written correctly, by getting the item
		// again.
		expected_attrs.erase("Color");
		expected_attrs.insert(attr_t("Color", "Not Blue"));
		expected_attrs.insert(attr_t("Size", "Large"));
		TEST_THAT(simpledb_get_attributes(access_key, secret_key, expected_attrs));

		// Conditional PutAttributes that fails (doesn't match) and therefore
		// doesn't change anything.
		request.SetParameter("Attribute.1.Value", "Green");
		request.SetParameter("Attribute.2.Replace", "true");
		request.SetParameter("Expected.1.Name", "Color");
		request.SetParameter("Expected.1.Value", "What?");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive(request, response, HTTPResponse::Code_Conflict));
		TEST_THAT(simpledb_get_attributes(access_key, secret_key, expected_attrs));

		// Conditional PutAttributes again, with the correct value for the Color
		// attribute this time, so the request should succeed.
		request.SetParameter("Expected.1.Value", "Not Blue");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"PutAttributesResponse"));

		// If it does, because Replace is set for the Size parameter as well, both
		// Size values will be replaced by the new single value.
		expected_attrs.clear();
		expected_attrs.insert(attr_t("Color", "Green"));
		expected_attrs.insert(attr_t("Size", "Large"));
		TEST_THAT(simpledb_get_attributes(access_key, secret_key, expected_attrs));

		// Test that we can delete values. We are supposed to pass some
		// attribute values, but what happens if they don't match the current
		// values is not specified.
		request.SetParameter("Action", "DeleteAttributes");
		request.RemoveParameter("Expected.1.Name");
		request.RemoveParameter("Expected.1.Value");
		request.RemoveParameter("Attribute.1.Replace");
		request.RemoveParameter("Attribute.2.Replace");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"DeleteAttributesResponse"));

		// Since we've deleted all attributes, the server should have deleted
		// the whole item, and the response to a GetAttributes request should be
		// 404 not found.
		TEST_THAT(simpledb_get_attributes_error(access_key, secret_key,
			HTTPResponse::Code_NotFound));

		// Create an item to use with conditional delete tests.
		request.SetParameter("Action", "PutAttributes");
		request.SetParameter("DomainName", "MyDomain");
		request.SetParameter("ItemName", "JumboFez");
		request.SetParameter("Attribute.1.Name", "Color");
		request.SetParameter("Attribute.1.Value", "Blue");
		request.SetParameter("Attribute.2.Name", "Size");
		request.SetParameter("Attribute.2.Value", "Med");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"PutAttributesResponse"));

		// Conditional delete that should fail
		request.SetParameter("Action", "DeleteAttributes");
		request.SetParameter("Expected.1.Name", "Color");
		request.SetParameter("Expected.1.Value", "What?");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive(request, response, HTTPResponse::Code_Conflict));

		// Check that it did actually fail
		expected_attrs.clear();
		expected_attrs.insert(attr_t("Color", "Blue"));
		expected_attrs.insert(attr_t("Size", "Med"));
		TEST_THAT(simpledb_get_attributes(access_key, secret_key, expected_attrs));

		// Conditional delete of one attribute ("Color") that should succeed
		request.SetParameter("Expected.1.Value", "Blue");
		// Remove attribute 1 ("Color") from the request, so it won't be deleted.
		// Attribute 2 ("Size") remains in the request, and should be deleted.
		request.RemoveParameter("Attribute.1.Name");
		request.RemoveParameter("Attribute.1.Value");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"DeleteAttributesResponse"));

		// Check that the "Size" attribute is no longer present, but "Color"
		// still is.
		expected_attrs.erase("Size");
		TEST_THAT(simpledb_get_attributes(access_key, secret_key, expected_attrs));

		// Conditional delete without specifying attributes, should remove all
		// remaining attributes, and hence the item itself. The condition
		// (expected values) set above should still be valid and match this item.
		request.RemoveParameter("Attribute.2.Name");
		request.RemoveParameter("Attribute.2.Value");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"DeleteAttributesResponse"));

		// Since we've deleted all attributes, the server should have deleted
		// the whole item, and the response to a GetAttributes request should be
		// 404 not found.
		TEST_THAT(simpledb_get_attributes_error(access_key, secret_key,
			HTTPResponse::Code_NotFound));

		// Reset for the next test
		request.SetParameter("Action", "Reset");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"ResetResponse"));
		domains = simpledb_list_domains(access_key, secret_key);
		expected_domains.clear();
		TEST_THAT(test_equal_lists(expected_domains, domains));
	}

	// Test that SimpleDBClient works the same way.
	{
		std::string access_key = EXAMPLE_S3_ACCESS_KEY;
		std::string secret_key = EXAMPLE_S3_SECRET_KEY;
		SimpleDBClient client(access_key, secret_key, "localhost", 1080,
			SIMPLEDB_SIMULATOR_HOST);

		// Test that date formatting produces the correct output format
		// date -d "2010-01-25T15:01:28-07:00" +%s => 1264456888
		client.SetFixedTimestamp(SecondsToBoxTime(1264456888), -7 * 60);
		HTTPRequest request = client.StartRequest(HTTPRequest::Method_GET, "");
		TEST_EQUAL("2010-01-25T15:01:28-07:00",
			request.GetParameterString("Timestamp"));

		client.SetFixedTimestamp(SecondsToBoxTime(1264431688), 0);
		request = client.StartRequest(HTTPRequest::Method_GET, "");
		TEST_EQUAL("2010-01-25T15:01:28Z",
			request.GetParameterString("Timestamp"));
		client.SetFixedTimestamp(0, 0);
		TEST_EQUAL(20, request.GetParameterString("Timestamp").length());

		std::vector<std::string> domains = client.ListDomains();
		TEST_EQUAL(0, domains.size());

		std::string domain = "MyDomain";
		std::string item = "JumboFez";
		client.CreateDomain(domain);
		domains = client.ListDomains();
		TEST_EQUAL(1, domains.size());
		if(domains.size() > 0)
		{
			TEST_EQUAL(domain, domains[0]);
		}

		// Create an item
		SimpleDBClient::str_map_t expected_attrs;
		expected_attrs["Color"] = "Blue";
		expected_attrs["Size"] = "Med";
		client.PutAttributes(domain, item, expected_attrs);
		SimpleDBClient::str_map_t actual_attrs = client.GetAttributes(domain, item);
		TEST_THAT(test_equal_maps(expected_attrs, actual_attrs));

		// Add more attributes. SimpleDBClient always replaces existing values
		// for attributes.
		expected_attrs.clear();
		expected_attrs["Color"] = "Not Blue";
		expected_attrs["Size"] = "Large";
		client.PutAttributes(domain, item, expected_attrs);
		actual_attrs = client.GetAttributes(domain, item);
		TEST_THAT(test_equal_maps(expected_attrs, actual_attrs));

		// Conditional PutAttributes that fails (doesn't match) and therefore
		// doesn't change anything (so we don't change expected_attrs).
		SimpleDBClient::str_map_t new_attrs = expected_attrs;
		new_attrs["Color"] = "Green";

		SimpleDBClient::str_map_t conditional_attrs;
		conditional_attrs["Color"] = "What?";

		TEST_CHECK_THROWS(
			client.PutAttributes(domain, item, new_attrs, conditional_attrs),
			HTTPException, ConditionalRequestConflict);
		actual_attrs = client.GetAttributes(domain, item);
		TEST_THAT(test_equal_maps(expected_attrs, actual_attrs));

		// Conditional PutAttributes again, with the correct value for the Color
		// attribute this time, so the request should succeed.
		conditional_attrs["Color"] = "Not Blue";
		client.PutAttributes(domain, item, new_attrs, conditional_attrs);

		// If it does, because Replace is set by default (enforced) by
		// SimpleDBClient, the Size value will be replaced by the new single
		// value.
		actual_attrs = client.GetAttributes(domain, item);
		TEST_THAT(test_equal_maps(new_attrs, actual_attrs));

		// Test that we can delete values. We are supposed to pass some
		// attribute values, but what happens if they don't match the current
		// values is not specified.
		client.DeleteAttributes(domain, item, new_attrs);

		// Check that it has actually been removed. Since we've deleted all
		// attributes, the server should have deleted the whole item, and the
		// response to a GetAttributes request should be 404 not found.
		TEST_CHECK_THROWS(client.GetAttributes(domain, item),
			HTTPException, SimpleDBItemNotFound);

		// Create an item to use with conditional delete tests.
		expected_attrs["Color"] = "Blue";
		expected_attrs["Size"] = "Med";
		client.PutAttributes(domain, item, expected_attrs);
		actual_attrs = client.GetAttributes(domain, item);
		TEST_THAT(test_equal_maps(expected_attrs, actual_attrs));

		// Conditional delete that should fail. If it succeeded, it should delete
		// the whole item, because no attributes are provided.
		expected_attrs["Color"] = "What?";
		SimpleDBClient::str_map_t empty_attrs;
		TEST_CHECK_THROWS(
			client.DeleteAttributes(domain, item, empty_attrs, // attributes
				expected_attrs), // expected
			HTTPException, ConditionalRequestConflict);

		// Check that the item was not actually deleted, nor any of its
		// attributes, and "Color" should still be "Blue".
		expected_attrs["Color"] = "Blue";
		actual_attrs = client.GetAttributes(domain, item);
		TEST_THAT(test_equal_maps(expected_attrs, actual_attrs));

		// Conditional delete of one attribute ("Color") that should succeed
		SimpleDBClient::str_map_t attrs_to_remove;
		attrs_to_remove["Size"] = "Med";
		expected_attrs["Color"] = "Blue";
		client.DeleteAttributes(domain, item, attrs_to_remove, // attributes
			expected_attrs); // expected

		// Check that the "Size" attribute is no longer present, but "Color"
		// still is.
		expected_attrs.erase("Size");
		actual_attrs = client.GetAttributes(domain, item);
		TEST_THAT(test_equal_maps(expected_attrs, actual_attrs));

		// Conditional delete without specifying attributes, should remove all
		// remaining attributes, and hence the item itself. The condition
		// (expected_attrs) set above should still be valid and match this item.
		client.DeleteAttributes(domain, item, empty_attrs, // attributes
			expected_attrs); // expected

		// Since we've deleted all attributes, the server should have deleted
		// the whole item, and the response to a GetAttributes request should be
		// 404 not found.
		TEST_CHECK_THROWS(client.GetAttributes(domain, item),
			HTTPException, SimpleDBItemNotFound);
	}

	// Kill it
	TEST_THAT(StopDaemon(pid, "testfiles/s3simulator.pid",
		"s3simulator.memleaks", true));

	TEST_THAT(StartSimulator());

	// S3Client tests with s3simulator executable for even more realism
	{
		S3Client client("localhost", 1080, EXAMPLE_S3_ACCESS_KEY,
			EXAMPLE_S3_SECRET_KEY, "johnsmith.s3.amazonaws.com");
		TEST_THAT(exercise_s3client(client));
	}

	TEST_THAT(StopSimulator());

	TEARDOWN();
}

int test(int argc, const char *argv[])
{
	if(argc >= 2 && ::strcmp(argv[1], "server") == 0)
	{
		// Run a server
		TestWebServer server;
		return server.Main("doesnotexist", argc - 1, argv + 1);
	}

	if(argc >= 2 && ::strcmp(argv[1], "s3server") == 0)
	{
		// Run a server
		S3Simulator server;
		return server.Main("doesnotexist", argc - 1, argv + 1);
	}

	TEST_THAT(test_httpserver());

	return finish_test_suite();
}
