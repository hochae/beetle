/*
 * BeetleConfig.h
 *
 *  Created on: May 23, 2016
 *      Author: James Hong
 */

#ifndef BEETLECONFIG_H_
#define BEETLECONFIG_H_

#include <string>

class BeetleConfig {
public:
	/*
	 * Load configuration from json file.
	 */
	BeetleConfig(std::string filename = "");
	virtual ~BeetleConfig();

	/*
	 * Dump configuration as a json object.
	 */
	std::string str(unsigned int indent = 4) const;

	/*
	 * Gateway name
	 */
	std::string name;

	/*
	 * Scan settings
	 */
	bool scanEnabled = true;

	/*
	 * Autoconnect settings
	 */
	bool autoConnectAll = false;
	double autoConnectMinBackoff = 60.0;
	std::string autoConnectBlacklist = "";

	/*
	 * TCP settings
	 */
	bool tcpEnabled = false;
	int tcpPort = 3002;

	/*
	 * Unix domain socket settings
	 */
	bool ipcEnabled = false;
	std::string ipcPath = "/tmp/beetle";

	/*
	 * Controller settings
	 */
	bool controllerEnabled = false;
	std::string controllerHost = "localhost";
	int controllerPort = 443;
	std::string getControllerHostAndPort() const;

	/*
	 * SSL settings
	 */
	std::string sslServerKey = "../certs/key.pem";
	std::string sslServerCert = "../certs/cert.pem";
	std::string sslClientKey = "../certs/key.pem";
	std::string sslClientCert = "../certs/cert.pem";
	bool sslVerifyPeers = false;

	/*
	 * Debug settings
	 */
	bool debugController = false;
	bool debugDiscovery = false;
	bool debugPerformance = false;
	bool debugRouter = false;
	bool debugScan = false;
	bool debugSocket = false;
};

#endif /* BEETLECONFIG_H_ */
