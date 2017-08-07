// --------------------------------------------------------------------------
//
// File
//		Name:    S3Simulator.cpp
//		Purpose: Amazon S3 client helper implementation class
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include <boost/version.hpp>
#include <openssl/hmac.h>

#include "Database.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "HTTPQueryDecoder.h"
#include "autogen_HTTPException.h"
#include "IOStream.h"
#include "Logging.h"
#include "MD5Digest.h"
#include "S3Simulator.h"
#include "Utils.h" // for ObjectExists_* (object_exists_t)
#include "decode.h"
#include "encode.h"

#include "MemLeakFindOn.h"

#define PTREE_DOMAIN_NEXT_ID_SEQ "next_id_seq"
#define PTREE_ITEM_NAME "name"
#define PTREE_ITEM_ATTRIBUTES "attributes"

using boost::property_tree::ptree;

ptree XmlStringToPtree(const std::string& string)
{
	ptree pt;
	std::istringstream stream(string);
	read_xml(stream, pt, boost::property_tree::xml_parser::trim_whitespace);
	return pt;
}

std::string PtreeToXmlString(const ptree& pt)
{
	std::ostringstream buf;
// The original direct instantiation of xml_writer_settings stopped in 1.55:
// http://www.pcl-users.org/problem-getting-PCL-1-7-1-on-osx-10-9-td4035213.html
// The arguments to xml_writer_make_settings were changed backwards-incompatibly in Boost 1.56:
// https://github.com/boostorg/property_tree/commit/8af8b6bf3b65fa59792d849b526678f176d87132
#if BOOST_VERSION >= 105600
	auto settings = boost::property_tree::xml_writer_make_settings<std::string>('\t', 1);
#elif BOOST_VERSION >= 105500
	auto settings = boost::property_tree::xml_writer_make_settings<char>('\t', 1);
#else
	boost::property_tree::xml_writer_settings<char> settings('\t', 1);
#endif
	write_xml(buf, pt, settings);
	return buf.str();
}


S3Simulator::S3Simulator()
: HTTPServer(300000)
{ }


SimpleDBSimulator::SimpleDBSimulator()
: mDomainsFile("testfiles/domains.qdbm"),
  mItemsFile("testfiles/items.qdbm")
{
	Open(DP_OWRITER | DP_OCREAT);
}

// Open the database file
void SimpleDBSimulator::Open(int mode)
{
	mpDomains = dpopen(mDomainsFile.c_str(), mode, 0);
	if(!mpDomains)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			BOX_DBM_MESSAGE("Failed to open domains database: " << mDomainsFile));
	}

	mpItems = dpopen(mItemsFile.c_str(), mode, 0);
	if(!mpItems)
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			BOX_DBM_MESSAGE("Failed to open items database: " << mItemsFile));
	}
}

SimpleDBSimulator::~SimpleDBSimulator()
{
	Close();
}

void SimpleDBSimulator::Close()
{
	if(mpDomains && !dpclose(mpDomains))
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			BOX_DBM_MESSAGE("Failed to close domains database"));
	}
	mpDomains = NULL;

	if(mpItems && !dpclose(mpItems))
	{
		THROW_EXCEPTION_MESSAGE(CommonException, Internal,
			BOX_DBM_MESSAGE("Failed to close items database"));
	}
	mpItems = NULL;
}


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
//		Name:    S3Simulator::GetSortedQueryString(HTTPRequest&)
//		Purpose: Returns a query string made from the supplied
//			 request, with the parameters sorted into alphabetical
//			 order as required by Amazon authentication. See
//			 http://docs.aws.amazon.com/AmazonSimpleDB/latest/DeveloperGuide/HMACAuth.html
//		Created: 2015-11-15
//
// --------------------------------------------------------------------------

std::string S3Simulator::GetSortedQueryString(const HTTPRequest& request)
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

	bool is_simpledb = false;
	if(rRequest.GetHostName() == SIMPLEDB_SIMULATOR_HOST)
	{
		is_simpledb = true;
	}

	std::string bucket_name;

	try
	{
		const Configuration& rConfig(GetConfiguration());
		std::string access_key = rConfig.GetKeyValue("AccessKey");
		std::string secret_key = rConfig.GetKeyValue("SecretKey");
		std::ostringstream buffer_to_sign;
		buffer_to_sign << rRequest.GetMethodName() << "\n";

		if(is_simpledb)
		{
			if(rRequest.GetParameterString("AWSAccessKeyId") != access_key)
			{
				THROW_EXCEPTION_MESSAGE(HTTPException, AuthenticationFailed,
					"Unknown AWSAccessKeyId: " <<
					rRequest.GetParameterString("AWSAccessKeyId"));
			}

			buffer_to_sign << rRequest.GetHostName() << "\n";
			buffer_to_sign << rRequest.GetRequestURI() << "\n";
			buffer_to_sign << GetSortedQueryString(rRequest);
		}
		else
		{
			std::string md5, date;
			rRequest.GetHeader("content-md5", &md5);
			rRequest.GetHeader("date", &date);

			std::string host = rRequest.GetHostName();
			std::string s3suffix = ".s3.amazonaws.com";
			if (host.size() > s3suffix.size())
			{
				std::string suffix = host.substr(host.size() - s3suffix.size(),
					s3suffix.size());

				if (suffix == s3suffix)
				{
					bucket_name = host.substr(0, host.size() - s3suffix.size());
				}
			}

			buffer_to_sign << md5 << "\n";
			buffer_to_sign << rRequest.GetContentType() << "\n";
			buffer_to_sign << date << "\n";

			// header names are already in lower case, i.e. canonical form
			std::vector<HTTPRequest::Header> headers =
				rRequest.GetHeaders().GetExtraHeaders();
			std::sort(headers.begin(), headers.end());

			for (std::vector<HTTPRequest::Header>::iterator
				i = headers.begin(); i != headers.end(); i++)
			{
				if (i->first.substr(0, 5) == "x-amz")
				{
					buffer_to_sign << i->first << ":" <<
						i->second << "\n";
				}
			}

			if (! bucket_name.empty())
			{
				buffer_to_sign << "/" << bucket_name;
			}

			buffer_to_sign << rRequest.GetRequestURI();
		}

		std::string string_to_sign = buffer_to_sign.str();

		unsigned char digest_buffer[EVP_MAX_MD_SIZE];
		unsigned int digest_size = sizeof(digest_buffer);
		/* unsigned char* mac = */ HMAC(
			is_simpledb ? EVP_sha256() : EVP_sha1(),
			secret_key.c_str(), secret_key.size(),
			(const unsigned char*)string_to_sign.c_str(),
			string_to_sign.size(), digest_buffer, &digest_size);

		std::string digest((const char *)digest_buffer, digest_size);
		std::string expected_auth, actual_auth;
		base64::encoder encoder;

		if(is_simpledb)
		{
			expected_auth = encoder.encode(digest);
			actual_auth = rRequest.GetParameterString("Signature");
		}
		else
		{
			expected_auth = "AWS " + access_key + ":" + encoder.encode(digest);

			if(!rRequest.GetHeader("authorization", &actual_auth))
			{
				THROW_EXCEPTION_MESSAGE(HTTPException,
					AuthenticationFailed, "Missing Authorization header");
			}
		}

		// The Base64 encoder tends to add a newline onto the end of the encoded
		// string, which we don't want, so remove it here.
		if(expected_auth[expected_auth.size() - 1] == '\n')
		{
			expected_auth = expected_auth.substr(0, expected_auth.size() - 1);
		}

		if(actual_auth != expected_auth)
		{
			THROW_EXCEPTION_MESSAGE(HTTPException,
				AuthenticationFailed, "Authentication code mismatch: " <<
				"expected " << expected_auth << " but received " <<
				actual_auth);
		}

		if(is_simpledb && rRequest.GetMethod() == HTTPRequest::Method_GET)
		{
			HandleSimpleDBGet(rRequest, rResponse);
		}
		else if(is_simpledb)
		{
			THROW_EXCEPTION_MESSAGE(HTTPException, BadRequest,
				"Unsupported Amazon SimpleDB Method");
		}
		else if(rRequest.GetMethod() == HTTPRequest::Method_GET)
		{
			HandleGet(rRequest, rResponse);
		}
		else if(rRequest.GetMethod() == HTTPRequest::Method_PUT)
		{
			HandlePut(rRequest, rResponse);
		}
		else
		{
			THROW_EXCEPTION_MESSAGE(HTTPException, BadRequest,
				"Unsupported Amazon S3 Method: " <<
				rRequest.GetMethodName());
		}
	}
	catch (BoxException &ce)
	{
		SendInternalErrorResponse(ce.what(), rResponse);

		// Override the default status code 500 for a few specific exceptions.
		if(EXCEPTION_IS_TYPE(ce, CommonException, OSFileOpenError))
		{
			rResponse.SetResponseCode(HTTPResponse::Code_NotFound);
		}
		else if(EXCEPTION_IS_TYPE(ce, CommonException, AccessDenied))
		{
			rResponse.SetResponseCode(HTTPResponse::Code_Forbidden);
		}
		else if(EXCEPTION_IS_TYPE(ce, HTTPException, AuthenticationFailed))
		{
			rResponse.SetResponseCode(HTTPResponse::Code_Unauthorized);
		}
		else if(EXCEPTION_IS_TYPE(ce, HTTPException, ConditionalRequestConflict))
		{
			rResponse.SetResponseCode(HTTPResponse::Code_Conflict);
		}
		else if(EXCEPTION_IS_TYPE(ce, HTTPException, FileNotFound))
		{
			rResponse.SetResponseCode(HTTPResponse::Code_NotFound);
		}
		else if(EXCEPTION_IS_TYPE(ce, HTTPException, SimpleDBItemNotFound))
		{
			rResponse.SetResponseCode(HTTPResponse::Code_NotFound);
		}
		else if(EXCEPTION_IS_TYPE(ce, HTTPException, BadRequest))
		{
			rResponse.SetResponseCode(HTTPResponse::Code_BadRequest);
		}
	}
	catch (std::exception &e)
	{
		SendInternalErrorResponse(e.what(), rResponse);
	}
	catch (...)
	{
		SendInternalErrorResponse("Unknown exception", rResponse);
	}

	if (rResponse.GetResponseCode() != HTTPResponse::Code_OK &&
		rResponse.GetResponseCode() != HTTPResponse::Code_NotModified &&
		rResponse.GetResponseCode() != HTTPResponse::Code_NoContent &&
		rResponse.GetSize() == 0)
	{
		// Looks like an error response with no error message specified,
		// so write a default one.
		std::ostringstream s;
		s << rResponse.GetResponseCode();
		SendInternalErrorResponse(s.str().c_str(), rResponse);
	}

	BOX_NOTICE(rResponse.GetResponseCode() << " " << rRequest.GetMethodName() << " " <<
		rRequest.GetRequestURI(true)); // with_parameters_for_get_request

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

void S3Simulator::HandleGet(HTTPRequest &rRequest, HTTPResponse &rResponse,
	bool IncludeContent)
{
	std::string path = GetConfiguration().GetKeyValue("StoreDirectory");
	path += rRequest.GetRequestURI();
	std::auto_ptr<FileStream> apFile;
	apFile.reset(new FileStream(path));
	int64_t file_length;

	std::string digest;
	{
		MD5DigestStream digester;
		apFile->CopyStreamTo(digester);
		file_length = apFile->GetPosition();
		apFile->Seek(0, IOStream::SeekType_Absolute);
		digester.Close();
		digest = "\"" + digester.DigestAsString() + "\"";
	}

	rResponse.SetResponseCode(HTTPResponse::Code_OK);

	// For GET and HEAD requests, we must set the Content-Length.  See RFC
	// 2616 section 4.4, and the Amazon Simple Storage Service API
	// Reference, section "HEAD Object" examples, which set it. Also, our
	// S3BackupFileSystem needs it!
	//
	// There are no examples for 304 Not Modified responses to requests
	// with If-None-Match (ETag match) so clients should not depend on
	// this, so the S3Simulator should not set Content-Length or ETag, to
	// ensure that any code which tries to use these headers will fail.

	std::string if_none_match = rRequest.GetHeaders().GetHeaderValue("if-none-match",
		false); // required
	if(digest == if_none_match)
	{
		rResponse.SetResponseCode(HTTPResponse::Code_NotModified);
		rResponse.GetHeaders().SetContentLength(0);
		IncludeContent = false;
	}
	else
	{
		rResponse.GetHeaders().SetContentLength(file_length);
		rResponse.AddHeader("ETag", digest);
	}

	if(IncludeContent)
	{
		apFile->CopyStreamTo(rResponse);
	}

	// http://docs.amazonwebservices.com/AmazonS3/2006-03-01/UsingRESTOperations.html
	rResponse.AddHeader("x-amz-id-2", "qBmKRcEWBBhH6XAqsKU/eg24V3jf/kWKN9dJip1L/FpbYr9FDy7wWFurfdQOEMcY");
	rResponse.AddHeader("x-amz-request-id", "F2A8CCCA26B4B26D");
	rResponse.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
	rResponse.AddHeader("Last-Modified", "Sun, 1 Jan 2006 12:00:00 GMT");
	rResponse.AddHeader("Server", "AmazonS3");
}


ptree SimpleDBSimulator::GetDomainProps(const std::string& domain_name)
{
	char *result = dpget(mpDomains, domain_name.c_str(), domain_name.size(),
		0, -1, 0);
	if(result == NULL)
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, S3SimulatorError,
			"Domain does not exist: " << domain_name);
	}
	std::string domain_props_str = result;
	free(result);
	return XmlStringToPtree(domain_props_str);
}

void
SimpleDBSimulator::PutDomainProps(const std::string& domain_name,
	const ptree domain_props)
{
	std::string domain_props_str = PtreeToXmlString(domain_props);
	ASSERT_DBM_OK(
		dpput(mpDomains, domain_name.c_str(), domain_name.size(),
			domain_props_str.c_str(), domain_props_str.size(), DP_DOVER),
		"Failed to create domain: " << domain_name, mDomainsFile,
		HTTPException, S3SimulatorError);
}

// Generate the key that will be used to store this Item in the mpItems database.
std::string ItemKey(const std::string& domain, std::string item_name)
{
	std::ostringstream key_stream;
	key_stream << domain << "." << item_name;
	return key_stream.str();
}

void SimpleDBSimulator::DeleteItem(const std::string& domain_name,
	const std::string& item_name, bool throw_if_not_found)
{
	std::string key = ItemKey(domain_name, item_name);
	bool result = dpout(mpItems, key.c_str(), key.size());
	if(!result && throw_if_not_found)
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, S3SimulatorError,
			"DeleteItem: Item does not exist: " << key);
	}
}

void ProcessConditionalRequest(const std::string& domain, HTTPRequest& rRequest,
	SimpleDBSimulator& simpledb, bool delete_attributes)
{
	const HTTPRequest::Query_t& params(rRequest.GetParameters());

	// Get the existing attributes for this item, if it exists.
	std::multimap<std::string, std::string> attributes =
		simpledb.GetAttributes(domain,
			rRequest.GetParameterString("ItemName"),
			false); // !throw_if_not_found

	// Iterate over all parameters looking for "Attribute.X.Name" and
	// "Attribute.X.Value" attributes, putting them into the
	// param_index_to_* maps. Note that we keep the "index" as a string
	// here, even though it should be an integer, to avoid needlessly
	// converting back and forth. Hence all these maps are keyed on
	// strings.
	std::map<std::string, std::string> param_index_to_name;
	std::map<std::string, std::string> param_index_to_value;
	std::map<std::string, bool> param_index_to_replace;
	// At the same time, add all "Expected.X.Name" and "Expected.X.Value"
	// attributes to the expected_index_to_* maps.
	std::map<std::string, std::string> expected_index_to_name;
	std::map<std::string, std::string> expected_index_to_value;

	for(HTTPRequest::Query_t::const_iterator i = params.begin();
		i != params.end(); i++)
	{
		std::string param_name = i->first;
		std::string param_value = i->second;
		std::string param_number_type = RemovePrefix("Attribute.",
			param_name, false); // !force
		std::string expected_number_type = RemovePrefix("Expected.",
			param_name, false); // !force
		if(!param_number_type.empty())
		{
			std::string param_index_name = RemoveSuffix(".Name",
				param_number_type, false); // !force
			std::string param_index_value = RemoveSuffix(".Value",
				param_number_type, false); // !force
			std::string param_index_replace = RemoveSuffix(".Replace",
				param_number_type, false); // !force
			if(!param_index_name.empty())
			{
				param_index_to_name[param_index_name] =
					param_value;
			}
			else if(!param_index_value.empty())
			{
				param_index_to_value[param_index_value] =
					param_value;
			}
			// Replace mode makes no sense when deleting matching attributes
			else if(!param_index_replace.empty() && !delete_attributes)
			{
				param_index_to_replace[param_index_replace] =
					(param_value == "true");
			}
			else
			{
				THROW_EXCEPTION_MESSAGE(HTTPException,
					S3SimulatorError, "PutAttributes: "
					"Unparsed Attribute parameter: " <<
					param_name);
			}
		}
		else if(!expected_number_type.empty())
		{
			std::string expected_index_name = RemoveSuffix(".Name",
				expected_number_type, false); // !force
			std::string expected_index_value = RemoveSuffix(".Value",
				expected_number_type, false); // !force
			if(!expected_index_name.empty())
			{
				expected_index_to_name[expected_index_name] =
					param_value;
			}
			else if(!expected_index_value.empty())
			{
				expected_index_to_value[expected_index_value] =
					param_value;
			}
			else
			{
				THROW_EXCEPTION_MESSAGE(HTTPException,
					S3SimulatorError, "PutAttributes: "
					"Unparsed Expected parameter: " <<
					param_name);
			}
		}
	}

	typedef std::multimap<std::string, std::string>::iterator mm_iter_t;

	// Iterate over the expected maps, matching up the names and values, putting them
	// into the expected_values map, which is easier to work with.
	for(std::map<std::string, std::string>::iterator
		i = expected_index_to_name.begin();
		i != expected_index_to_name.end(); i++)
	{
		std::string index = i->first;
		std::string attr_name = i->second;

		// pev = pointer to expected value
		std::map<std::string, std::string>::iterator pev =
			expected_index_to_value.find(index);

		if(pev == expected_index_to_value.end())
		{
			THROW_EXCEPTION_MESSAGE(HTTPException,
				S3SimulatorError, "PutAttributes: Expected name with no "
				"value: " << attr_name);
		}

		std::string attr_value = pev->second;
		bool matched_an_actual_value = false;

		// Loop over the actual values for this attribute name, looking for one
		// that matches the expected value.
		std::pair<mm_iter_t, mm_iter_t> range = attributes.equal_range(attr_name);

		// pov = pointer to original/old/current value
		for(mm_iter_t pov = range.first; pov != range.second; pov++)
		{
			if(pov->second == attr_value)
			{
				matched_an_actual_value = true;
				break;
			}
		}

		if(!matched_an_actual_value)
		{
			THROW_EXCEPTION_MESSAGE(HTTPException,
				ConditionalRequestConflict, "The value of attribute '" <<
				attr_name << "' was expected to be '" << attr_value <<
				"', but no matching value was found");
		}
	}

	// Iterate over the attribute maps, matching up the names and values, putting them
	// into (or removing them from) the item data XML tree.
	for(std::map<std::string, std::string>::iterator
		i = param_index_to_name.begin();
		i != param_index_to_name.end(); i++)
	{
		std::string index = i->first;
		std::string attr_name = i->second;

		// pnv = pointer to new value (or value to delete).
		std::map<std::string, std::string>::iterator pnv =
			param_index_to_value.find(index);
		std::string attr_value;

		if(pnv == param_index_to_value.end())
		{
			THROW_EXCEPTION_MESSAGE(HTTPException,
				S3SimulatorError, "PutAttributes: "
				"Attribute name with no value: " <<
				attr_name);
		}
		else
		{
			attr_value = pnv->second;
		}

		// If we are deleting values, then look for a matching pair, and if we
		// find one, delete it.
		if(delete_attributes)
		{
			bool deleted;
			do
			{
				deleted = false;
				std::pair<mm_iter_t, mm_iter_t> range =
					attributes.equal_range(attr_name);

				// Loop over all values for this attribute name
				// (attr_name), which must all lie between range->first
				// and range->second.
				for(mm_iter_t
					p_orig_attr = range.first;
					p_orig_attr != range.second;
					p_orig_attr++)
				{
					if(p_orig_attr->second == attr_value)
					{
						attributes.erase(p_orig_attr);
						deleted = true;
						// The iterator is not valid any more, so
						// break out and search again.
						break;
					}
				}
			}
			while(deleted);
		}
		else
		{
			// If the Replace parameter is provided and is "true", then
			// delete any existing value, so only the new value inserted
			// below will remain in the multimap.
			std::map<std::string, bool>::iterator j =
				param_index_to_replace.find(index);
			if(j != param_index_to_replace.end() && j->second)
			{
				attributes.erase(attr_name);
			}

			attributes.insert(
				std::multimap<std::string, std::string>::value_type(
					attr_name, attr_value));
		}
	}

	// If there are no attributes provided, and we are deleting attributes, then we
	// should delete all of them (if the conditions matched, which we have already
	// ascertained above).
	if(param_index_to_name.empty() && delete_attributes)
	{
		attributes.clear();
	}

	// If there are no attributes remaining, then delete the whole item.
	if(delete_attributes && attributes.empty())
	{
		simpledb.DeleteItem(domain, rRequest.GetParameterString("ItemName"));
	}
	else
	{
		// Write the new item data XML tree back to the database, overwriting the
		// previous values of all attributes.
		simpledb.PutAttributes(domain, rRequest.GetParameterString("ItemName"),
			attributes);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    S3Simulator::HandleSimpleDBGet(HTTPRequest &rRequest,
//			 HTTPResponse &rResponse)
//		Purpose: Handles an Amazon SimpleDB GET request.
//		Created: 2015-11-16
//
// --------------------------------------------------------------------------

void S3Simulator::HandleSimpleDBGet(HTTPRequest &rRequest, HTTPResponse &rResponse,
	bool IncludeContent)
{
	SimpleDBSimulator simpledb;
	std::string action = rRequest.GetParameterString("Action");
	ptree response_tree;
	rResponse.SetResponseCode(HTTPResponse::Code_OK);

	std::string domain;
	if(action != "ListDomains" && action != "Reset")
	{
		domain = rRequest.GetParameterString("DomainName");
	}

	if(action == "ListDomains")
	{
		std::vector<std::string> domains = simpledb.ListDomains();

		response_tree.add("ListDomainsResponse.ListDomainsResult", "");
		for(std::vector<std::string>::iterator i = domains.begin();
			i != domains.end(); i++)
		{
			response_tree.add(
				"ListDomainsResponse.ListDomainsResult.DomainName", *i);
		}
	}
	else if(action == "CreateDomain")
	{
		simpledb.CreateDomain(domain);
		response_tree.add("CreateDomainResponse", "");
	}
	else if(action == "PutAttributes")
	{
		ProcessConditionalRequest(domain, rRequest, simpledb,
			false); // delete_attributes
		response_tree.add("PutAttributesResponse", "");
	}
	else if(action == "GetAttributes")
	{
		std::multimap<std::string, std::string> attributes =
			simpledb.GetAttributes(domain,
				rRequest.GetParameterString("ItemName"));

		// Ensure that the root element is present, even if there are no
		// individual attributes.
		response_tree.add("GetAttributesResponse.GetAttributesResult", "");

		// Add the attributes to the response tree.
		for(std::multimap<std::string, std::string>::iterator
			i = attributes.begin();
			i != attributes.end(); i++)
		{
			ptree attribute;
			attribute.add("Name", i->first);
			attribute.add("Value", i->second);
			response_tree.add_child(
				"GetAttributesResponse.GetAttributesResult.Attribute",
				attribute);
		}
	}
	else if(action == "DeleteAttributes")
	{
		ProcessConditionalRequest(domain, rRequest, simpledb,
			true); // delete_attributes
		response_tree.add("DeleteAttributesResponse", "");
	}
	else if(action == "Reset")
	{
		simpledb.Reset();
		response_tree.add("ResetResponse", "");
	}
	else
	{
		rResponse.SetResponseCode(HTTPResponse::Code_NotFound);
		THROW_EXCEPTION_MESSAGE(HTTPException, S3SimulatorError,
			"Unsupported SimpleDB Action: " << action);
	}

	rResponse.Write(PtreeToXmlString(response_tree));
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
	std::string base_path = GetConfiguration().GetKeyValue("StoreDirectory");
	std::auto_ptr<FileStream> apFile;

	// Amazon S3 has no explicit directories or directory creation operation, but we
	// are using the filesystem for storage, so we need to ensure that any directories
	// used in the file's path actually exist before we can create the file itself.
	std::string file_uri = rRequest.GetRequestURI();
	for(std::string::size_type next_slash = file_uri.find('/', 1);
		next_slash != std::string::npos;
		next_slash = file_uri.find('/', next_slash + 1))
	{
		std::string parent_dir_path = base_path + file_uri.substr(0, next_slash);
		object_exists_t what_exists = ObjectExists(parent_dir_path);
		if(what_exists == ObjectExists_NoObject)
		{
			// Does not exist, need to create it
			mkdir(parent_dir_path.c_str(), 0755);
		}
		else if(what_exists == ObjectExists_Dir)
		{
			// Directory already exists, nothing to do
		}
		else
		{
			THROW_EXCEPTION_MESSAGE(HTTPException, BadRequest,
				"Cannot create directory: something else already exists "
				"with this name: " << parent_dir_path);
		}
	}

	std::string file_path = base_path + file_uri;
	try
	{
		apFile.reset(new FileStream(file_path, O_CREAT | O_TRUNC | O_RDWR));
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

	rRequest.ReadContent(*apFile, GetTimeout());
	apFile->Seek(0, IOStream::SeekType_Absolute);

	std::string digest;
	{
		MD5DigestStream digester;
		apFile->CopyStreamTo(digester);
		digester.Close();
		digest = "\"" + digester.DigestAsString() + "\"";
	}

	// http://docs.amazonwebservices.com/AmazonS3/2006-03-01/RESTObjectPUT.html
	rResponse.AddHeader("x-amz-id-2", "LriYPLdmOdAiIfgSm/F1YsViT1LW94/xUQxMsF7xiEb1a0wiIOIxl+zbwZ163pt7");
	rResponse.AddHeader("x-amz-request-id", "F2A8CCCA26B4B26D");
	rResponse.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
	rResponse.AddHeader("Last-Modified", "Sun, 1 Jan 2006 12:00:00 GMT");
	rResponse.AddHeader("ETag", digest);
	rResponse.SetContentType("");
	rResponse.AddHeader("Server", "AmazonS3");
	rResponse.SetResponseCode(HTTPResponse::Code_OK);
}


std::vector<std::string> SimpleDBSimulator::ListDomains()
{
	if(!dpiterinit(mpDomains))
	{
		THROW_EXCEPTION_MESSAGE(HTTPException, S3SimulatorError,
			"Failed to start iterating over domains database");
	}

	std::vector<std::string> domains;
	for(char *key = dpiternext(mpDomains, NULL);
		key != NULL;
		key = dpiternext(mpDomains, NULL))
	{
		domains.push_back(key);
	}

	return domains;
}

void SimpleDBSimulator::Reset()
{
	Close();
	Open(DP_OWRITER | DP_OCREAT | DP_OTRUNC);
}

void SimpleDBSimulator::CreateDomain(const std::string& domain_name)
{
	char *result = dpget(mpDomains, domain_name.c_str(), domain_name.size(), 0, 0, 0);
	if(result != NULL)
	{
		free(result);
		// "CreateDomain is an idempotent operation; running it multiple times
		// using the same domain name will not result in an error response."
		return;
	}

	ptree domain_props;
	domain_props.add(PTREE_DOMAIN_NEXT_ID_SEQ, 1);
	PutDomainProps(domain_name, domain_props);
}


void SimpleDBSimulator::PutAttributes(const std::string& domain_name,
	const std::string& item_name,
	const std::multimap<std::string, std::string> attributes)
{
	ptree item_data;

	// Iterate over the attribute map, adding names and values to the item data
	// structure (XML tree).
	for(std::multimap<std::string, std::string>::const_iterator
		i = attributes.begin();
		i != attributes.end(); i++)
	{
		std::string path = PTREE_ITEM_ATTRIBUTES "." + i->first;
		item_data.add(path, i->second);
	}

	// Generate the key that will be used to store this item data in the
	// mpItems database.
	std::string key = ItemKey(domain_name, item_name);
	std::string value = PtreeToXmlString(item_data);
	ASSERT_DBM_OK(
		dpput(mpItems, key.c_str(), key.size(), value.c_str(),
			value.size(), DP_DOVER),
		"PutAttributes: Failed to add item", mItemsFile,
		HTTPException, S3SimulatorError);
}

std::multimap<std::string, std::string> SimpleDBSimulator::GetAttributes(
	const std::string& domain_name,
	const std::string& item_name,
	bool throw_if_not_found)
{
	std::multimap<std::string, std::string> attributes;
	std::string key = ItemKey(domain_name, item_name);
	char* result = dpget(mpItems, key.c_str(), key.size(), 0, -1, 0);
	if(result == NULL)
	{
		if(throw_if_not_found)
		{
			THROW_EXCEPTION_MESSAGE(HTTPException, SimpleDBItemNotFound,
				"GetAttributes: Item does not exist: " << key);
		}
		else
		{
			return attributes;
		}
	}

	std::string item_data_str = result;
	free(result);
	ptree item_data = XmlStringToPtree(item_data_str);

	// There might not be any attributes, e.g. if they have all been deleted,
	// so we need to check for and handle that situation, as it's not an error.
	try
	{
		// Iterate over the attributes in the item data tree, adding names and values
		// to the attributes map.
		BOOST_FOREACH(ptree::value_type &v,
			item_data.get_child(PTREE_ITEM_ATTRIBUTES))
		{
			std::string name = v.first;
			std::string value = v.second.data();
			attributes.insert(
				std::multimap<std::string, std::string>::value_type(name,
					value));
		}
	}
	catch(boost::property_tree::ptree_bad_path &e)
	{
		// Do nothing, just don't add any attributes to the list.
	}

	return attributes;
}
