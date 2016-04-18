/*
 * Router.cpp
 *
 *  Created on: Mar 24, 2016
 *      Author: james
 */

#include "Router.h"

#include <bluetooth/bluetooth.h>
#include <boost/thread/lock_types.hpp>
#include <boost/thread/pthread/shared_mutex.hpp>
#include <cassert>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "ble/att.h"
#include "ble/gatt.h"
#include "ble/helper.h"
#include "Debug.h"
#include "Device.h"
#include "hat/HandleAllocationTable.h"
#include "Handle.h"
#include "UUID.h"

Router::Router(Beetle &beetle_) : beetle(beetle_) {

}

Router::~Router() {

}

int Router::route(uint8_t *buf, int len, device_t src) {
	assert(len > 0);
	int result;

	switch(buf[0]) {
	case ATT_OP_FIND_INFO_REQ:
		result = routeFindInfo(buf, len, src);
		break;
	case ATT_OP_FIND_BY_TYPE_REQ:
		result = routeFindByTypeValue(buf, len, src);
		break;
	case ATT_OP_READ_BY_TYPE_REQ:
		result = routeReadByType(buf, len, src);
		break;
	case ATT_OP_READ_BY_GROUP_REQ:
		result = routeReadByGroupType(buf, len, src);
		break;
	case ATT_OP_HANDLE_NOTIFY:
	case ATT_OP_HANDLE_IND:
		result = routeHandleNotifyOrIndicate(buf, len, src);
		break;
	case ATT_OP_READ_REQ:
	case ATT_OP_READ_BLOB_REQ:
	case ATT_OP_WRITE_REQ:
	case ATT_OP_WRITE_CMD:
	case ATT_OP_SIGNED_WRITE_CMD:
		result = routeReadWrite(buf, len, src);
		break;
	default:
		result = routeUnsupported(buf, len, src);
		break;
	}
	return result;
}

int Router::routeUnsupported(uint8_t *buf, int len, device_t src) {
	if (debug_router) {
		pwarn("unimplemented command " + std::to_string(buf[0]));
	}

	boost::shared_lock<boost::shared_mutex> devicesLk(beetle.devicesMutex);
	if (beetle.devices.find(src) == beetle.devices.end()) {
		pwarn(std::to_string(src) + " does not id a device");
		return -1;
	} else {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], 0, ATT_ECODE_REQ_NOT_SUPP, err);
		beetle.devices[src]->writeResponse(err, len);
		delete[] err;
		return 0;
	}
}

int Router::routeFindInfo(uint8_t *buf, int len, device_t src) {
	/*
	 * Lock devices
	 */
	boost::shared_lock<boost::shared_mutex> devicesLk(beetle.devicesMutex);

	if (beetle.devices.find(src) == beetle.devices.end()) {
		pwarn(std::to_string(src) + " does not id a device");
		return -1;
	}

	Device *sourceDevice = beetle.devices[src];

	uint16_t startHandle;
	uint16_t endHandle;
	if (!parse_find_info_request(buf, len, startHandle, endHandle)) {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], 0, ATT_ECODE_INVALID_PDU, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
		return 0;
	}

	if (debug_router) {
		std::stringstream ss;
		ss << "FindInfo to [" << startHandle << "," << endHandle << "] ";
		pdebug(ss.str());
	}

	if (startHandle == 0 || startHandle > endHandle) {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], startHandle, ATT_ECODE_INVALID_HANDLE, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
		return 0;
	}

	int srcMTU = sourceDevice->getMTU();
	int respLen = 0;
	int respHandleCount = 0;
	uint8_t *resp = new uint8_t[srcMTU];
	resp[0] = ATT_OP_FIND_INFO_RESP;
	resp[1] = 1; // TODO handle non standard uuids (cb4.2 p2177)
	respLen += 2;

	uint16_t currHandle = startHandle;
	bool done = false;
	while (currHandle <= endHandle && !done) {
		if (debug_router) {
			pdebug("RouteFindInfo @" + std::to_string(currHandle));
		}

		/*
		 * Lock hat
		 */
		std::lock_guard<std::mutex> hatLg(sourceDevice->hatMutex);

		device_t dst = sourceDevice->hat->getDeviceForHandle(currHandle);
		handle_range_t handleRange = sourceDevice->hat->getHandleRange(currHandle);
		if (dst == NULL_RESERVED_DEVICE) { // -1
			// do nothing
		} else if (dst == src) {
			// do nothing
		} else if (dst >= 0) {
			if (beetle.devices.find(dst) == beetle.devices.end()) {
				pwarn(std::to_string(dst) + " does not id a device");
			}

			Device *destinationDevice = beetle.devices[dst];

			/*
			 * Lock handles
			 */
			std::lock_guard<std::recursive_mutex> handlesLg(destinationDevice->handlesMutex);

			for (auto &mapping : destinationDevice->handles) {
				uint16_t offset = mapping.first + handleRange.start;
				if (offset < startHandle) {
					continue;
				} else if (offset > endHandle) {
					done = true;
					break;
				}

				// TODO this allows 16bit handles only
				Handle *handle = mapping.second;
				*(uint16_t *)(resp + respLen) = htobs(offset);
				*(uint16_t *)(resp + respLen + 2) = htobs(handle->getUuid().getShort());
				respLen += 4;
				respHandleCount++;
				if (respLen + 4 > srcMTU) {
					done = true;
					break;
				}
			}
			if (done) {
				break;
			}
		}

		/*
		 * Advance range
		 */
		currHandle = handleRange.end + 1;
		if (currHandle <= handleRange.start) {
			done = true;
			break;
		}
	}

	if (respHandleCount > 0) {
		sourceDevice->writeResponse(resp, respLen);
	} else {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], startHandle, ATT_ECODE_ATTR_NOT_FOUND, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
	}

	delete[] resp;
	return 0;
}

int Router::routeFindByTypeValue(uint8_t *buf, int len, device_t src) {
	/*
	 * Lock devices
	 */
	boost::shared_lock<boost::shared_mutex> devicesLk(beetle.devicesMutex);

	if (beetle.devices.find(src) == beetle.devices.end()) {
		pwarn(std::to_string(src)+ " does not id a device");
		return -1;
	}

	Device *sourceDevice = beetle.devices[src];

	uint16_t startHandle;
	uint16_t endHandle;
	uint16_t attType;
	uint8_t *attValue;
	int attValLen;
	if (!parse_find_by_type_value_request(buf, len, startHandle, endHandle, attType, attValue, attValLen)) {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], 0, ATT_ECODE_INVALID_PDU, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
		return 0;
	}

	if (debug_router) {
		std::stringstream ss;
		ss << "FindByTypeValue to [" << startHandle << "," << endHandle << "] ";
		pdebug(ss.str());
		pdebug(attValue, attValLen);
	}

	if (startHandle == 0 || startHandle > endHandle) {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], startHandle, ATT_ECODE_INVALID_HANDLE, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
		return 0;
	}

	/*
	 * Lock hat
	 */
	std::lock_guard<std::mutex> hatLg(sourceDevice->hatMutex);

	UUID attUUID(attType);

	int srcMTU = sourceDevice->getMTU();
	int respLen = 0;
	int respHandleCount = 0;
	// bool cutShort = false;
	uint8_t *resp = new uint8_t[srcMTU];
	resp[0] = ATT_OP_FIND_BY_TYPE_RESP;
	respLen++;

	uint16_t currHandle = startHandle;
	bool done = false;
	while (currHandle <= endHandle && !done) {
		if (debug_router) {
			pdebug("RouteFindByTypeValue @" + std::to_string(currHandle));
		}

		device_t dst = sourceDevice->hat->getDeviceForHandle(currHandle);
		handle_range_t handleRange = sourceDevice->hat->getHandleRange(currHandle);
		if (dst == NULL_RESERVED_DEVICE){ // -1
			// do nothing
		} else if (dst == src) {
			// do nothing
		} else if (dst >= 0) {
			/*
			 * Lock handles
			 */
			if (beetle.devices.find(dst) == beetle.devices.end()) {
				pwarn(std::to_string(dst) + " does not id a device");
				return -1;
			}

			Device *destinatonDevice = beetle.devices[dst];

			/*
			 * Lock handles
			 */
			std::lock_guard<std::recursive_mutex> handlesLg(destinatonDevice->handlesMutex);

			for (auto &mapping : destinatonDevice->handles) {
				uint16_t offset = mapping.first + handleRange.start;
				if (offset < startHandle) {
					continue;
				} else if (offset > endHandle) {
					done = true;
					break;
				}

				Handle *handle = mapping.second;
				if (handle->getUuid().getShort() == attType) {
					int cmpLen = (attValLen < handle->cache.len) ? attValLen : handle->cache.len;
					if (memcmp(handle->cache.value, attValue, cmpLen) == 0) {
						*(uint16_t *)(resp + respLen) = htobs(offset);
						*(uint16_t *)(resp + respLen + 2) = htobs(handle->getEndGroupHandle());
						respLen += 4;
						respHandleCount++;

						if (respLen + 4 > srcMTU) {
							done = true;
							// cutShort = true;
							// TODO not convinced that this is in the standard
//							resp[respLen - 1] = 0xFF;
//							resp[respLen - 2] = 0xFF;
							break;
						}
					}
				}
			}
			if (done) {
				break;
			}
		}

		/*
		 * Advance range
		 */
		uint16_t nextHandle = handleRange.end + 1;
		if (nextHandle <= currHandle) {
			done = true;
		} else {
			currHandle = nextHandle;
		}
	}

	if (respHandleCount > 0) {
		sourceDevice->writeResponse(resp, respLen);
	} else {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], startHandle, ATT_ECODE_ATTR_NOT_FOUND, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
	}

	delete[] resp;
	return 0;
}

int Router::routeReadByType(uint8_t *buf, int len, device_t src) {
	/*
	 * Lock devices
	 */
	boost::shared_lock<boost::shared_mutex> devicesLk(beetle.devicesMutex);

	if (beetle.devices.find(src) == beetle.devices.end()) {
		pwarn(std::to_string(src)+ " does not id a device");
		return -1;
	}

	Device *sourceDevice = beetle.devices[src];

	uint16_t startHandle;
	uint16_t endHandle;
	UUID *attType;
	if (!parse_read_by_type_value_request(buf, len, startHandle, endHandle, attType)) {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], 0, ATT_ECODE_INVALID_PDU, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
		return 0;
	}

	if (debug_router) {
		std::stringstream ss;
		ss << "ReadByTypeRequest [" <<  startHandle << "," << endHandle << "]";
		pdebug(ss.str());
	}

	if (startHandle == 0 || startHandle > endHandle) {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], startHandle, ATT_ECODE_INVALID_HANDLE, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
		delete attType;
		return 0;
	}

	/*
	 * Lock hat
	 */
	std::lock_guard<std::mutex> hatLg(sourceDevice->hatMutex);

	// TODO forward the request to the device where the range falls, this does not work if you want to read across multiple devices

	device_t dst = sourceDevice->hat->getDeviceForHandle(startHandle);
	if (dst == BEETLE_RESERVED_DEVICE) {
		Device *destinationDevice = beetle.devices[dst];
		if (debug_router) {
			pdebug("ReadByTypeRequest to " + destinationDevice->getName());
		}

		/*
		 * Lock handles
		 */
		std::lock_guard<std::recursive_mutex> handlesLg(destinationDevice->handlesMutex);

		// TODO be less lazy and return more than 1
		uint8_t resp[destinationDevice->getMTU()];
		resp[0] = ATT_OP_READ_BY_TYPE_RESP;
		int respLen = 2;
		for (auto &kv : destinationDevice->handles) {
			if (kv.first < startHandle) {
				continue;
			} else if (kv.first > endHandle) {
				break;
			}

			Handle *handle = kv.second;
			if (handle->getUuid().compareTo(*attType)) {
				*(uint16_t*)(resp + 2) = htobs(handle->getHandle());
				memcpy(resp + 4, handle->cache.value, handle->cache.len);
				respLen += 2 + handle->cache.len;
				resp[1] = 2 + handle->cache.len;
				break;
			}
		}

		if (respLen > 2) {
			sourceDevice->writeResponse(resp, respLen);
		} else {
			uint8_t *err;
			int len = pack_error_pdu(buf[0], startHandle, ATT_ECODE_ATTR_NOT_FOUND, err);
			sourceDevice->writeResponse(err, len);
			delete[] err;
		}
	} else if (dst == NULL_RESERVED_DEVICE) {
		if (debug_router) {
			pdebug("ReadByTypeRequest to null");
		}
		uint8_t *err;
		int len = pack_error_pdu(buf[0], startHandle, ATT_ECODE_ATTR_NOT_FOUND, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
	} else {
		handle_range_t handleRange = sourceDevice->hat->getHandleRange(startHandle);
		*(uint16_t *)(buf + 1) = htobs(startHandle - handleRange.start);
		*(uint16_t *)(buf + 3) = htobs(endHandle - handleRange.start);

		if (beetle.devices.find(dst) == beetle.devices.end()) {
			pwarn(std::to_string(dst) + " does not id a device");
			return -1;
		}

		Device *destinationDevice = beetle.devices[dst];
		if (debug_router) {
			pdebug("ReadByTypeRequest to " + destinationDevice->getName());
		}

		destinationDevice->writeTransaction(buf, len, [this, src, dst, attType, handleRange, startHandle](uint8_t *resp, int respLen) {
			/*
			 * Lock devices
			 */
			boost::shared_lock<boost::shared_mutex> devicesLk(this->beetle.devicesMutex);

			if (beetle.devices.find(src) == beetle.devices.end()) {
				pwarn(std::to_string(src) + " does not id a device");
				delete attType;
				return;
			}
			Device *sourceDevice = beetle.devices[src];

			if (resp == NULL || respLen <= 2) {
				uint8_t *err;
				int len = pack_error_pdu(ATT_OP_READ_BY_TYPE_REQ, startHandle, ATT_ECODE_ABORTED, err);
				sourceDevice->writeResponse(err, len);
				delete[] err;
			} else if (resp[0] == ATT_OP_ERROR) {
				*(uint16_t *)(resp + 2) = htobs(startHandle);
				sourceDevice->writeResponse(resp, respLen);
			} else {
				int segLen = resp[1];
				for (int i = 2; i < respLen; i += segLen) {
					uint16_t handle = btohs(*(uint16_t *)(resp + i));
					handle += handleRange.start;
					*(uint16_t *)(resp + i) = htobs(handle);
					if (attType->isShort() && attType->getShort() == GATT_CHARAC_UUID) {
						int j = i + 3;
						uint16_t valueHandle = btohs(*(uint16_t *)(resp + j));
						valueHandle += handleRange.start;
						*(uint16_t *)(resp + j) = htobs(valueHandle);
					}
				}
				sourceDevice->writeResponse(resp, respLen);
			}
		});
	}
	delete attType;
	return 0;
}

int Router::routeReadByGroupType(uint8_t *buf, int len, device_t src) {
	/*
	 * Lock devices
	 */
	boost::shared_lock<boost::shared_mutex> devicesLk(beetle.devicesMutex);

	if (beetle.devices.find(src) == beetle.devices.end()) {
		pwarn(std::to_string(src) + " does not id a device");
		return -1;
	}

	Device *sourceDevice = beetle.devices[src];

	uint16_t startHandle;
	uint16_t endHandle;
	UUID *attType;
	if (!parse_read_by_group_type_request(buf, len, startHandle, endHandle, attType)) {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], 0, ATT_ECODE_INVALID_PDU, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
		return 0;
	}

	if (debug_router) {
		std::stringstream ss;
		ss << "ReadByGroupType to [" << startHandle << "," << endHandle << "]";
		pdebug(ss.str());
	}

	if (startHandle == 0 || startHandle > endHandle) {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], startHandle, ATT_ECODE_INVALID_HANDLE, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
		delete attType;
		return 0;
	}

	/*
	 * Lock hat
	 */
	std::lock_guard<std::mutex> hatLg(sourceDevice->hatMutex);

	int srcMTU = sourceDevice->getMTU();
	int respLen = 0;
	int respHandleCount = 0;
	uint8_t *resp = new uint8_t[srcMTU];
	resp[0] = ATT_OP_READ_BY_GROUP_RESP;
//	resp[1] is length, fill later
	respLen += 2;

	uint16_t currHandle = startHandle;
	bool done = false;
	while (currHandle <= endHandle && !done) {
		if (debug_router) {
			pdebug("ReadByGroupType @" + std::to_string(currHandle));
		}

		device_t dst = sourceDevice->hat->getDeviceForHandle(currHandle);
		handle_range_t handleRange = sourceDevice->hat->getHandleRange(currHandle);
		if (dst == NULL_RESERVED_DEVICE) { // -1
			// do nothing
		} else if (dst == src) {
			// do nothing
		} else if (dst >= 0) {
			if (beetle.devices.find(dst) == beetle.devices.end()) {
				pwarn(std::to_string(dst) + " does not id a device");
			}

			Device *destinationDevice = beetle.devices[dst];

			/*
			 * Lock handles
			 */
			std::lock_guard<std::recursive_mutex> handlesLg(destinationDevice->handlesMutex);

			for (auto &mapping : destinationDevice->handles) {
				uint16_t offset = mapping.first + handleRange.start;
				if (offset < startHandle) {
					continue;
				} else if (offset > endHandle) {
					done = true;
					break;
				}

				// TODO this allows 16bit handles only
				Handle *handle = mapping.second;

				if (handle->getUuid().compareTo(*attType)) {
					if (respHandleCount == 0) { // set the length
						resp[1] = 4 + handle->cache.len;
					} else {
						if (4 + handle->cache.len != resp[1]) { // incompatible length
							done = true;
							break;
						}
					}

					*(uint16_t *)(resp + respLen) = htobs(offset);
					*(uint16_t *)(resp + respLen + 2) = htobs(handle->getEndGroupHandle() + handleRange.start);
					memcpy(resp + respLen + 4, handle->cache.value, handle->cache.len);
					respLen += resp[1];
					respHandleCount++;
					if (respLen + resp[1] > srcMTU) {
						done = true;
						break;
					}
				}
			}
			if (done) {
				break;
			}
		}

		/*
		 * Advance range
		 */
		currHandle = handleRange.end + 1;
		if (currHandle <= handleRange.start) {
			done = true;
			break;
		}
	}

	if (respHandleCount > 0) {
		sourceDevice->writeResponse(resp, respLen);
	} else {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], startHandle, ATT_ECODE_ATTR_NOT_FOUND, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
	}

	delete[] resp;
	delete attType;
	return 0;
}

int Router::routeHandleNotifyOrIndicate(uint8_t *buf, int len, device_t src) {
	/*
	 * Lock devices
	 */
	boost::shared_lock<boost::shared_mutex> devicesLk(beetle.devicesMutex);

	if (beetle.devices.find(src) == beetle.devices.end()) {
		pwarn(std::to_string(src) + " does not id a device");
		return -1;
	}

	Device *sourceDevice = beetle.devices[src];

	/*
	 * Lock hat
	 */
	std::lock_guard<std::mutex> hatLk(sourceDevice->hatMutex);

	/*
	 * Lock handles
	 */
	std::lock_guard<std::recursive_mutex> handlesLg(sourceDevice->handlesMutex);

	uint16_t handle = btohs(*(uint16_t *)(buf + 1));
	Handle *h;
	if (sourceDevice->handles.find(handle) == sourceDevice->handles.end()) {
		return -1; // no such handle
	}
	h = sourceDevice->handles[handle];

	for (device_t dst : h->subscribers) {
		if (beetle.devices.find(dst) == beetle.devices.end()) {
			pwarn(std::to_string(dst) + " does not id a device");
			continue;
		}
		Device *destinationDevice = beetle.devices[dst];

		handle_range_t handleRange = destinationDevice->hat->getDeviceRange(src);
		if (handleRange.start == 0 && handleRange.start == handleRange.end) {
			pwarn(std::to_string(src) + "has inconsistent state");
			continue; // this device has no mapped handles
		} else {
			handle += handleRange.start;
			*(uint16_t *)(buf + 1) = htobs(handle);

			if (buf[0] == ATT_OP_HANDLE_NOTIFY) {
				destinationDevice->writeCommand(buf, len);
			} else if (buf[0] == ATT_OP_HANDLE_IND) {
				destinationDevice->writeTransaction(buf, len, [dst](uint8_t *a, int b) {
					if (debug_router) {
						pdebug("got confirmation from " + std::to_string(dst));
					}
				});
			}
		}
	}

	if (buf[0] == ATT_OP_HANDLE_IND) {
		uint8_t buf = ATT_OP_HANDLE_CNF;
		sourceDevice->writeResponse(&buf, sizeof(buf));
	}

	return 0;
}

int Router::routeReadWrite(uint8_t *buf, int len, device_t src) {
	/*
	 * Lock devices
	 */
	boost::shared_lock<boost::shared_mutex> devicesLk(beetle.devicesMutex);

	if (beetle.devices.find(src) == beetle.devices.end()) {
		pwarn(std::to_string(src) + " does not id a device");
		return -1;
	}

	Device *sourceDevice = beetle.devices[src];

	uint8_t opCode = buf[0];
	uint16_t handle = btohs(*(uint16_t *)(buf + 1));

	/*
	 * Lock hat
	 */
	std::lock_guard<std::mutex> hatLg(sourceDevice->hatMutex);

	device_t dst = sourceDevice->hat->getDeviceForHandle(handle);
	if (dst == NULL_RESERVED_DEVICE) {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], handle, ATT_ECODE_ATTR_NOT_FOUND, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
		return 0;
	}

	handle_range_t handleRange = sourceDevice->hat->getDeviceRange(dst);

	if (beetle.devices.find(dst) == beetle.devices.end()) {
		pwarn(std::to_string(dst) + " does not id a device");
		return -1;
	}
	Device *destinationDevice = beetle.devices[dst];

	/*
	 * Lock handles
	 */
	std::lock_guard<std::recursive_mutex> handlesLg(destinationDevice->handlesMutex);

	uint16_t remoteHandle = handle - handleRange.start;
	if (destinationDevice->handles.find(remoteHandle) == destinationDevice->handles.end()) {
		uint8_t *err;
		int len = pack_error_pdu(buf[0], handle, ATT_ECODE_ATTR_NOT_FOUND, err);
		sourceDevice->writeResponse(err, len);
		delete[] err;
		return 0;
	}

	Handle *proxyH = destinationDevice->handles[remoteHandle];
	if (opCode == ATT_OP_WRITE_REQ && proxyH->getUuid().isShort()
			&& proxyH->getUuid().getShort() == GATT_CLIENT_CHARAC_CFG_UUID) {
		/*
		 * Update to subscription
		 */
		uint16_t charHandle = proxyH->getCharHandle();
		Handle *charH = beetle.devices[dst]->handles[charHandle];
		Handle *charAttrH = beetle.devices[dst]->handles[charH->getCharHandle()];
		if (buf[3] == 0) {
			charAttrH->subscribers.erase(src);
		} else {
			charAttrH->subscribers.insert(src);
		}
		int numSubscribers = charAttrH->subscribers.size();
		if (dst != BEETLE_RESERVED_DEVICE &&
				((numSubscribers == 0 && buf[3] == 0) || (numSubscribers == 1 && buf[3] == 1))) {
			*(uint16_t *)(buf + 1) = htobs(remoteHandle);
			destinationDevice->writeTransaction(buf, len, [this, handle, src, dst, opCode](uint8_t *resp, int respLen) {
				/*
				 * Lock devices
				 */
				boost::shared_lock<boost::shared_mutex> devicesLk(beetle.devicesMutex);

				if (beetle.devices.find(src) == beetle.devices.end()) {
					pwarn(std::to_string(src) + " does not id a device");
					return;
				}

				if (resp == NULL) {
					uint8_t *err;
					int len = pack_error_pdu(opCode, handle, ATT_ECODE_UNLIKELY, err);
					beetle.devices[src]->writeResponse(err, len);
					delete[] err;
				} else {
					beetle.devices[src]->writeResponse(resp, respLen);
				}
			});
		} else {
			uint8_t resp = ATT_OP_WRITE_RESP;
			sourceDevice->writeResponse(&resp, 1);
		}
	} else if (opCode == ATT_OP_READ_REQ && proxyH->cache.value != NULL
			&& (dst == BEETLE_RESERVED_DEVICE
					|| proxyH->cache.cachedSet.find(src) == proxyH->cache.cachedSet.end())
			&& proxyH->isCacheInfinite()) {
		/*
		 * Serve read from cache
		 */
		proxyH->cache.cachedSet.insert(src);
		int respLen = 1 + proxyH->cache.len;
		uint8_t resp[respLen];
		resp[0] = ATT_OP_READ_RESP;
		memcpy(resp + 1, proxyH->cache.value, proxyH->cache.len);
		sourceDevice->writeResponse(resp, respLen);
	} else {
		/*
		 * Route packet to device
		 */
		*(uint16_t *)(buf + 1) = htobs(remoteHandle);
		if (opCode == ATT_OP_WRITE_CMD || opCode == ATT_OP_SIGNED_WRITE_CMD) {
			destinationDevice->writeCommand(buf, len);
		} else {
			destinationDevice->writeTransaction(buf, len,
					[this, opCode, src, dst, handle, remoteHandle](uint8_t *resp, int respLen) {
				/*
				 * Lock devices
				 */
				boost::shared_lock<boost::shared_mutex> devicesLk(beetle.devicesMutex);

				if (beetle.devices.find(src) == beetle.devices.end()) {
					pwarn(std::to_string(src) + " does not id a device");
					return;
				}

				if (resp == NULL) {
					uint8_t *err;
					int len = pack_error_pdu(opCode, handle, ATT_ECODE_UNLIKELY, err);
					beetle.devices[src]->writeResponse(err, len);
					delete[] err;
				} else {
					if (resp[0] == ATT_OP_READ_RESP) {
						if (beetle.devices.find(dst) == beetle.devices.end()) {
							pwarn(std::to_string(dst) + " does not id a device");
						} else {
							Device *destinationDevice = beetle.devices[dst];
							std::lock_guard<std::recursive_mutex> handlesLg(destinationDevice->handlesMutex);
							Handle *proxyH = destinationDevice->handles[remoteHandle];
							proxyH->cache.cachedSet.clear();
							int tmpLen = respLen - 1;
							uint8_t *tmpVal = new uint8_t[tmpLen];
							memcpy(tmpVal, resp + 1, tmpLen);
							/*
							 * Cache takes over the pointer
							 */
							proxyH->cache.set(tmpVal, tmpLen);
							proxyH->cache.cachedSet.insert(src);
						}
					}
					beetle.devices[src]->writeResponse(resp, respLen);
				}
			});
		}
	}
	return 0;
}
