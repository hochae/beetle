/*
 * TCPClientProxy.h
 *
 *  Created on: Apr 10, 2016
 *      Author: James Hong
 */

#ifndef INCLUDE_DEVICE_SOCKET_TCP_TCPCLIENTPROXY_H_
#define INCLUDE_DEVICE_SOCKET_TCP_TCPCLIENTPROXY_H_

#include <string>
#include <openssl/ossl_typ.h>

#include "device/socket/TCPConnection.h"


/*
 * Dummy device representing a client at a different Beetle gateway.
 */
class TCPClientProxy: public TCPConnection {
public:
	TCPClientProxy(Beetle &beetle, SSL *ssl, int sockfd, std::string clientGateway,
			struct sockaddr_in clientGatewaySockAddr, device_t localProxyFor);
	virtual ~TCPClientProxy();

	/*
	 * Returns the device id that this is proxying.
	 */
	device_t getLocalDeviceId();

	/*
	 * Returns the name of the client gateway.
	 */
	std::string getClientGateway();

private:
	device_t localProxyFor;
	std::string clientGateway;
};

#endif /* INCLUDE_DEVICE_SOCKET_TCP_TCPCLIENTPROXY_H_ */
