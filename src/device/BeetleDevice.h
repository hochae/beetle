/*
 * BeetleDevice.h
 *
 *  Created on: Apr 3, 2016
 *      Author: root
 */

#ifndef DEVICE_BEETLEDEVICE_H_
#define DEVICE_BEETLEDEVICE_H_

#include <cstdint>
#include <functional>
#include <string>

#include "../ble/att.h"
#include "../Beetle.h"
#include "../Device.h"
#include "../hat/HandleAllocationTable.h"

/*
 * Fully synchronous simulated device for this Beetle.
 */
class BeetleDevice: public Device {
public:
	/*
	 * Initialize Beetle with name in its own handle space.
	 */
	BeetleDevice(Beetle &beetle, std::string name);
	virtual ~BeetleDevice();

	int getMTU() { return ATT_DEFAULT_LE_MTU; };

	bool writeResponse(uint8_t *buf, int len);
	bool writeCommand(uint8_t *buf, int len);
	bool writeTransaction(uint8_t *buf, int len, std::function<void(uint8_t*, int)> cb);
	int writeTransactionBlocking(uint8_t *buf, int len, uint8_t *&resp);

	/*
	 * Notify every device that is subscribed to the services changed characteristic
	 * that services have changed.
	 *
	 * Caller should be holding Beetle's device list lock.
	 */
	void informServicesChanged(handle_range_t range, device_t dst);

private:
	/*
	 * Setup Beetle's own handle space.
	 */
	void init();

	Handle *serviceChangedAttr;
};

#endif /* DEVICE_BEETLEDEVICE_H_ */
