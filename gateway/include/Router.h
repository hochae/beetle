/*
 * Router.h
 *
 *  Created on: Mar 24, 2016
 *      Author: James Hong
 */

#ifndef INCLUDE_ROUTER_H_
#define INCLUDE_ROUTER_H_

#include <cstdint>

#include "BeetleTypes.h"

class Router {
public:
	Router(Beetle &beetle);
	virtual ~Router();

	/*
	 * Route the packet. Returns 0 if no further action is needed by the
	 * caller, or -1 if an unhandled error occurred.
	 */
	int route(uint8_t *buf, int len, device_t src);
private:
	Beetle &beetle;

	int routeFindInfo(uint8_t *buf, int len, device_t src);
	int routeFindByTypeValue(uint8_t *buf, int len, device_t src);
	int routeReadByType(uint8_t *buf, int len, device_t src);
	int routeReadByGroupType(uint8_t *buf, int len, device_t src);
	int routeHandleNotifyOrIndicate(uint8_t *buf, int len, device_t src);
	int routeReadWrite(uint8_t *buf, int len, device_t src);
	int routeUnsupported(uint8_t *buf, int len, device_t src);
};

#endif /* INCLUDE_ROUTER_H_ */
