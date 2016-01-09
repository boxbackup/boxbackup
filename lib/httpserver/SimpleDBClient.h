// --------------------------------------------------------------------------
//
// File
//		Name:    SimpleDBClient.h
//		Purpose: Amazon SimpleDB client class
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

#ifndef SIMPLEDBCLIENT__H
#define SIMPLEDBCLIENT__H

#include <string>
#include <map>
#include <vector>

#include <boost/property_tree/ptree.hpp>

#include "BoxTime.h"
#include "HTTPRequest.h"

using boost::property_tree::ptree;

class HTTPResponse;
class HTTPServer;

// --------------------------------------------------------------------------
//
// Class
//		Name:    SimpleDBClient
//		Purpose: Amazon S3 client helper implementation class
//		Created: 04/01/2016
//
// --------------------------------------------------------------------------

class SimpleDBClient
{
private:
	std::string mHostName, mEndpoint, mAccessKey, mSecretKey;
	box_time_t mFixedTimestamp;
	int mOffsetMinutes, mPort, mTimeout;

public:
	// Note: endpoint controls the Host: header. If not set, the hostname is used. If
	// you want to connect to a particular host and send a different Host: header,
	// then set both hostname (to connect to) and endpoint (for the host header),
	// otherwise leave endpoint empty.
	SimpleDBClient(const std::string& access_key, const std::string& secret_key,
		const std::string& hostname = "sdb.eu-west-1.amazonaws.com", int port = 0,
		const std::string& endpoint = "",
		// Set a default timeout of 300 seconds to make debugging easier
		int timeout = 300)
	: mHostName(hostname),
	  mEndpoint(endpoint),
	  mAccessKey(access_key),
	  mSecretKey(secret_key),
	  mFixedTimestamp(0),
	  mOffsetMinutes(0),
	  mPort(port),
	  mTimeout(timeout)
	{ }

	typedef std::vector<std::string> list_t;
	typedef std::map<std::string, std::string> str_map_t;

	list_t ListDomains();
	void CreateDomain(const std::string& domain_name);
	str_map_t GetAttributes(const std::string& domain_name,
		const std::string& item);
	void PutAttributes(const std::string& domain_name,
		const std::string& item_name, const str_map_t& attributes,
		const str_map_t& expected = str_map_t());
	void DeleteAttributes(const std::string& domain_name,
		const std::string& item_name, const str_map_t& attributes,
		const str_map_t& expected = str_map_t());

	// These shouldn't really be APIs, but exposing them makes it easier to test
	// this class.
	HTTPRequest StartRequest(HTTPRequest::Method method, const std::string& action);
	std::string GenerateQueryString(const HTTPRequest& request);
	void SetFixedTimestamp(box_time_t fixed_timestamp, int offset_minutes)
	{
		mFixedTimestamp = fixed_timestamp;
		mOffsetMinutes = offset_minutes;
	}

private:
	void SendAndReceive(HTTPRequest& request, HTTPResponse& response,
		int expected_status_code = 200);
	void SendAndReceiveXML(HTTPRequest& request, ptree& response_tree,
		const std::string& expected_root_element);
	std::string CalculateSimpleDBSignature(const HTTPRequest& request);
	void AddPutAttributes(HTTPRequest& request, const str_map_t& attributes,
		const str_map_t& expected, bool add_required);
};


#endif // SIMPLEDBCLIENT__H

