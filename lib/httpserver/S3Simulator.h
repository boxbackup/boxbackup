// --------------------------------------------------------------------------
//
// File
//		Name:    S3Simulator.h
//		Purpose: Amazon S3 simulation HTTP server for S3 testing
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------

#ifndef S3SIMULATOR__H
#define S3SIMULATOR__H

#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include "HTTPServer.h"
#include "depot.h"

class ConfigurationVerify;
class HTTPRequest;
class HTTPResponse;

#define SIMPLEDB_SIMULATOR_HOST "sdb.localhost"

// --------------------------------------------------------------------------
//
// Class
//		Name:    SimpleDBSimulator
//		Purpose: Amazon SimpleDB simulation interface.
//		Created: 2015-11-21
//
// --------------------------------------------------------------------------
class SimpleDBSimulator
{
public:
	SimpleDBSimulator();
	~SimpleDBSimulator();

	std::vector<std::string> ListDomains();
	void CreateDomain(const std::string& domain_name);
	void PutAttributes(const std::string& domain_name,
		const std::string& item_name,
		const std::multimap<std::string, std::string> attributes);
	std::multimap<std::string, std::string> GetAttributes(
		const std::string& domain_name,
		const std::string& item_name,
		bool throw_if_not_found = true);
	void DeleteItem(const std::string& domain_name, const std::string& item_name,
		bool throw_if_not_found = true);
	void Reset();

protected:
	void Open(int mode);
	void Close();
	boost::property_tree::ptree GetDomainProps(const std::string& domain_name);
	void PutDomainProps(const std::string& domain_name,
		const boost::property_tree::ptree domain_props);

private:
	DEPOT* mpDomains;
	DEPOT* mpItems;
	std::string mDomainsFile, mItemsFile;
};


// --------------------------------------------------------------------------
//
// Class
//		Name:    S3Simulator
//		Purpose: Amazon S3 simulation HTTP server for S3 testing
//		Created: 09/01/2009
//
// --------------------------------------------------------------------------
class S3Simulator : public HTTPServer
{
public:
	// Increase timeout to 5 minutes, from HTTPServer default of 1 minute,
	// to help with debugging.
	S3Simulator();
	~S3Simulator() { }

	const ConfigurationVerify* GetConfigVerify() const;
	virtual void Handle(HTTPRequest &rRequest, HTTPResponse &rResponse);
	virtual void HandleGet(HTTPRequest &rRequest, HTTPResponse &rResponse);
	virtual void HandlePut(HTTPRequest &rRequest, HTTPResponse &rResponse);
	virtual void HandleSimpleDBGet(HTTPRequest &rRequest, HTTPResponse &rResponse,
		bool IncludeContent = true);
	std::string GetSortedQueryString(const HTTPRequest& request);

	virtual const char *DaemonName() const
	{
		return "s3simulator";
	}
};

#endif // S3SIMULATOR__H

