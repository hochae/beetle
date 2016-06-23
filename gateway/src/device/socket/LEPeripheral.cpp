/*
 * LEPeripheral.cpp
 *
 *  Created on: Mar 28, 2016
 *      Author: James Hong
 */

#include "device/socket/LEPeripheral.h"

#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sstream>
#include <unistd.h>
#include <memory>

#include "Beetle.h"
#include "ble/att.h"
#include "Debug.h"
#include "Device.h"
#include "device/socket/shared.h"
#include "sync/OrderedThreadPool.h"
#include "util/write.h"

LEPeripheral::LEPeripheral(Beetle &beetle, bdaddr_t addr, AddrType addrType) :
		VirtualDevice(beetle, true) {
	type = LE_PERIPHERAL;

	bdaddr = addr;
	bdaddrType = addrType;
	stopped = false;

	struct sockaddr_l2 loc_addr;
	memset(&loc_addr, 0, sizeof(struct sockaddr_l2));
	loc_addr.l2_family = AF_BLUETOOTH;
	bdaddr_t tmp;
	memset(tmp.b, 0, sizeof(bdaddr_t));
	bacpy(&loc_addr.l2_bdaddr, &tmp);
	loc_addr.l2_psm = 0;
	loc_addr.l2_cid = htobs(ATT_CID);
	loc_addr.l2_bdaddr_type = BDADDR_LE_PUBLIC;

	struct sockaddr_l2 rem_addr;
	memset(&rem_addr, 0, sizeof(struct sockaddr_l2));
	rem_addr.l2_family = AF_BLUETOOTH;
	bacpy(&rem_addr.l2_bdaddr, &bdaddr);
	rem_addr.l2_psm = 0;
	rem_addr.l2_cid = htobs(ATT_CID);
	rem_addr.l2_bdaddr_type = (addrType == PUBLIC) ? BDADDR_LE_PUBLIC : BDADDR_LE_RANDOM;

	sockfd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
	if (sockfd < 0) {
		throw DeviceException("could not create socket");
	}
	if (bind(sockfd, (struct sockaddr *) &loc_addr, sizeof(loc_addr)) < 0) {
		close(sockfd);
		throw DeviceException("could not bind");
	}
	if (connect(sockfd, (struct sockaddr *) &rem_addr, sizeof(rem_addr)) < 0) {
		close(sockfd);
		throw DeviceException("could not connect");
	}

	unsigned int connInfoLen = sizeof(connInfo);
	if (getsockopt(sockfd, SOL_L2CAP, L2CAP_CONNINFO, &connInfo, &connInfoLen) < 0) {
		close(sockfd);
		throw DeviceException("could not get l2cap conn info");
	}

	/*
	 * Set default connection interval.
	 */
	beetle.hci.setConnectionInterval(connInfo.hci_handle, 10, 40, 0, 0x0C80, 0);

	if (!request_device_name(sockfd, name, delayedPackets)) {
		throw DeviceException("could not discover device name");
	}
}

LEPeripheral::LEPeripheral(Beetle &beetle, int sockfd_, struct sockaddr_l2 sockaddr) :
		VirtualDevice(beetle, true) {
	type = LE_PERIPHERAL;

	bdaddr = sockaddr.l2_bdaddr;
	bdaddrType = (sockaddr.l2_bdaddr_type == LE_PUBLIC_ADDRESS) ? PUBLIC : RANDOM;
	stopped = false;

	sockfd = sockfd_;

	unsigned int connInfoLen = sizeof(connInfo);
	if (getsockopt(sockfd, SOL_L2CAP, L2CAP_CONNINFO, &connInfo, &connInfoLen) < 0) {
		close(sockfd);
		throw DeviceException("could not get l2cap conn info");
	}

	/*
	 * Set default connection interval.
	 */
	beetle.hci.setConnectionInterval(connInfo.hci_handle, 10, 40, 0, 0x0C80, 0);

	if (!request_device_name(sockfd, name, delayedPackets)) {
		throw DeviceException("could not discover device name");
	}
}

LEPeripheral::~LEPeripheral() {
	stopped = true;

	pendingWrites.wait();

	beetle.readers.remove(sockfd);

	if (debug_socket) {
		pdebug("shutting down socket for " + getName());
	}
	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);
}

bdaddr_t LEPeripheral::getBdaddr() {
	return bdaddr;
}

LEPeripheral::AddrType LEPeripheral::getAddrType() {
	return bdaddrType;
}

void LEPeripheral::startInternal() {
	for (delayed_packet_t &packet : delayedPackets) {
		readHandler(packet.buf.get(), packet.len);
	}
	delayedPackets.clear();

	beetle.readers.add(sockfd, [this] {
		if (stopped) {
			return;
		}

		uint8_t buf[256];
		int n = read(sockfd, buf, sizeof(buf));
		if (debug_socket) {
			pdebug("read " + std::to_string(n) + " bytes from " + getName());
		}
		if (n <= 0) {
			if (debug_socket) {
				std::stringstream ss;
				ss << "socket errno: " << strerror(errno);
				pdebug(ss.str());
			}
			stopInternal();
		} else {
			if (debug_socket) {
				phex(buf, n);
			}
			readHandler(buf, n);
		}
	});
}

struct l2cap_conninfo LEPeripheral::getL2capConnInfo() {
	return connInfo;
}

bool LEPeripheral::write(uint8_t *buf, int len) {
	if (stopped) {
		return false;
	}

	boost::shared_array<uint8_t> bufCpy(new uint8_t[len]);
	memcpy(bufCpy.get(), buf, len);
	pendingWrites.increment();
	beetle.writers.schedule(getId(), [this, bufCpy, len] {
		if (write_all(sockfd, bufCpy.get(), len) != len) {
			if (debug_socket) {
				std::stringstream ss;
				ss << "socket write failed : " << strerror(errno);
				pdebug(ss.str());
			}
			stopInternal();
		} else {
			if (debug_socket) {
				pdebug("wrote " + std::to_string(len) + " bytes to " + getName());
				phex(bufCpy.get(), len);
			}
		}
		pendingWrites.decrement();
	});
	return true;
}

void LEPeripheral::stopInternal() {
	if(!stopped.exchange(true)) {
		if (debug) {
			pdebug("terminating " + getName());
		}
		beetle.removeDevice(getId());
	}
}
