/*
 * TCPClientProxy.h
 *
 *  Created on: Apr 10, 2016
 *      Author: james
 */

#ifndef INCLUDE_DEVICE_SOCKET_TCP_TCPCLIENTPROXY_H_
#define INCLUDE_DEVICE_SOCKET_TCP_TCPCLIENTPROXY_H_

#include <string>

#include "../../../Beetle.h"
#include "../TCPConnection.h"

/*
 * Dummy device representing a client at a different Beetle gateway.
 */
class TCPClientProxy: public TCPConnection {
public:
	TCPClientProxy(Beetle &beetle, int sockfd, std::string clientGateway,
			struct sockaddr_in clientGatewaySockAddr, device_t localProxyFor);
	virtual ~TCPClientProxy();

	device_t getLocalDeviceId();
	std::string getClientGateway();

private:
	device_t localProxyFor;
	std::string clientGateway;
};

#endif /* INCLUDE_DEVICE_SOCKET_TCP_TCPCLIENTPROXY_H_ */