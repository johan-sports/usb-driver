#include "../usb_driver.h"

#include <windows.h>
#include <windowsx.h>
#include <setupapi.h>
#include <devguid.h>
#include <initguid.h>
#include <usbiodef.h>
#include <winioctl.h>
#include <usbioctl.h>
#include <cfgmgr32.h>
#include <assert.h>

#include <unordered_map>
#include <bitset>

#define FORMAT_FLAGS (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS)

namespace USBDriver {
	typedef unsigned long ulong;
	typedef unsigned int  uint;

	typedef std::unordered_map<std::string, USBDevicePtr> DeviceMap;

	static DeviceMap gAllDevices;

	/**
	 * Create a new windows SP type and automatically set the property cbSize 
	 * to the sizeof the type, as required by many functions in the windows
	 * API.
	 */
	template<typename SP_T>
	SP_T _createSPType()
	{
		SP_T sp;
		sp.cbSize = sizeof(sp);

		return sp;
	}

	static void _printError(const char *funcName)
	{
		DWORD error = GetLastError();
		char *errorStr = nullptr;

		FormatMessage(FORMAT_FLAGS, NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&errorStr, 0, NULL);
		fprintf(stderr, "%s failed: error %d: %s", funcName, error, errorStr);

		free(errorStr);
	}

	static bool _deviceProperty(HDEVINFO hDeviceInfo, PSP_DEVINFO_DATA device_info_data,
								DWORD property, std::string &buf_str)
	{
		DWORD bufLen = MAX_PATH;
		DWORD nSize;

		char *buf = static_cast<char*>(malloc(bufLen));
		assert(buf != NULL);

		bool ok = SetupDiGetDeviceRegistryProperty(hDeviceInfo, device_info_data,
												   property, NULL, (PBYTE)buf, 
												   bufLen, &nSize);

		if (ok) {
			buf_str.assign(buf);
		}
		else {
			_printError("SetupDiGetDeviceRegistryProperty()");
		}

		free(buf);

		return ok;
	}

	static bool _parseDeviceID(const char *device_path, std::string &vid,
							   std::string &pid, std::string &serial)
	{
		const char *p = device_path;

		if (strncmp(p, "USB\\", 4) != 0) {
			return false;
		}
		p += 4;

		if (strncmp(p, "VID_", 4) != 0) {
			return false;
		}
		p += 4;

		vid.assign("0x");

		while (*p != '&') {
			if (*p == '\0') {
				return false;
			}
			vid.push_back(tolower(*p));
			p++;
		}
		p++;

		if (strncmp(p, "PID_", 4) != 0) {
			return false;
		}
		p += 4;

		// Clear serial, just to be sure we don't have any left over state.
		serial.clear();

		pid.assign("0x");

		while (*p != '\\') {
			if (*p == '\0') {
				return false;
			}
			if (*p == '&') {
				// No serial.
				return true;
			}

			pid.push_back(tolower(*p));
			p++;
		}
		p++;

		while (*p != '\0') {
			if (*p == '&') {
				return true;
			}

			serial.push_back(toupper(*p));
			p++;
		}

		return true;
	}


	static ULONG _deviceNumberFromHandle(HANDLE handle, bool bReportError = true)
	{
		STORAGE_DEVICE_NUMBER sdn;
		sdn.DeviceNumber = -1;

		DWORD bytesReturned = 0; // Ignored

		if (DeviceIoControl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn,
						    sizeof(sdn), &bytesReturned, NULL)) {
			return sdn.DeviceNumber;
		}

		if (bReportError) {
			_printError("DeviceIOControl(IOCTL_STORAGE_GET_DEVICE_NUMBER)");
		}

		return 0;
	}

	static std::string _driveForDeviceNumber(ULONG deviceNumber)
	{
		static std::unordered_map<ULONG, unsigned char> deviceDrivesCache;

		if (deviceDrivesCache.empty()) {
			std::bitset<32> drives(GetLogicalDrives());

			// We start iteration from C
			for (char c = 'D'; c <= 'Z'; c++) {
				if (!drives[c - 'A']) {
					continue;
				}

				std::string path = std::string("\\\\.\\") + c + ":";

				HANDLE driveHandle = CreateFileA(path.c_str(), GENERIC_READ,
												  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
												  FILE_FLAG_NO_BUFFERING | FILE_FLAG_RANDOM_ACCESS, NULL);

				if (driveHandle == INVALID_HANDLE_VALUE) {
					_printError("CreateFileA()");
					continue;
				}

				ULONG num = _deviceNumberFromHandle(driveHandle, false);
				if (num != 0) {
					deviceDrivesCache[num] = c;
				}

				CloseHandle(driveHandle);
			}
		}

		auto iter = deviceDrivesCache.find(deviceNumber);

		if (iter == deviceDrivesCache.end()) {
			return "";
		}

		return std::string(1, iter->second).append(":");
	}

	typedef struct DeviceSPData {
		SP_DEVINFO_DATA info;
		SP_DEVICE_INTERFACE_DATA inter;
		SP_DEVICE_INTERFACE_DETAIL_DATA *interDetails;
	} SPData;

	std::vector<DeviceSPData> _deviceSPs(HDEVINFO hDeviceInfo, const GUID *guid)
	{
		std::vector<DeviceSPData> sps;

		uint index = 0;

		while(true) 
		{ 
			SP_DEVINFO_DATA spDevInfoData = _createSPType<SP_DEVINFO_DATA>();

			if (!SetupDiEnumDeviceInfo(hDeviceInfo, index, &spDevInfoData))
			{
				if (GetLastError() != ERROR_NO_MORE_ITEMS)
					_printError("SetupDiEnumDeviceInfo");

				break; // We're out of devices
			}

			SP_DEVICE_INTERFACE_DATA spDeviceInterfaceData = _createSPType<SP_DEVICE_INTERFACE_DATA>();

			if (!SetupDiEnumDeviceInterfaces(hDeviceInfo, 0, guid, index, &spDeviceInterfaceData)) {
				_printError("SetupDiEnumDeviceInterfaces()");
				continue; // Invalid device, skip it
			}

			DeviceSPData deviceSP;
			deviceSP.info = spDevInfoData;
			deviceSP.inter = spDeviceInterfaceData;

			sps.push_back(deviceSP);

			++index;
		}

		return sps;
	}

	USBDevicePtr _extractUSBDeviceData(HDEVINFO hDeviceInfo, DeviceSPData &sp)
	{
		std::string deviceName;
		if (!_deviceProperty(hDeviceInfo, &sp.info, SPDRP_FRIENDLYNAME, deviceName)) {
			return nullptr;
		}

		std::string vendor;
		if (!_deviceProperty(hDeviceInfo, &sp.info, SPDRP_MFG, vendor)) {
			return nullptr;
		}

		ULONG interfaceDetailLen = MAX_PATH;
		SP_DEVICE_INTERFACE_DETAIL_DATA *spDeviceInterfaceDetail =
			(SP_DEVICE_INTERFACE_DETAIL_DATA *)malloc(interfaceDetailLen);
		assert(spDeviceInterfaceDetail != NULL);
		spDeviceInterfaceDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

		SP_DEVINFO_DATA spDeviceInfoData = _createSPType<SP_DEVINFO_DATA>();

		if (!SetupDiGetDeviceInterfaceDetail(hDeviceInfo, &sp.inter, spDeviceInterfaceDetail, 
											 interfaceDetailLen, &interfaceDetailLen, &spDeviceInfoData)) {
			_printError("SetupDiGetDeviceInterfaceDetail()");
			return nullptr;
		}

		HANDLE handle = CreateFile(spDeviceInterfaceDetail->DevicePath,
								   0, FILE_SHARE_READ | FILE_SHARE_WRITE,
								   NULL, OPEN_EXISTING, 0, NULL);

		if (handle == INVALID_HANDLE_VALUE) {
			_printError("CreateFile()");
			return nullptr;
		}

		free(spDeviceInterfaceDetail);

		std::string mount;
		ULONG deviceNumber = _deviceNumberFromHandle(handle);
		if (deviceNumber != 0) {
			mount = _driveForDeviceNumber(deviceNumber);
		}

		CloseHandle(handle);

		DEVINST devInstParent;
		if (CM_Get_Parent(&devInstParent, spDeviceInfoData.DevInst, 0)
			!= CR_SUCCESS) {
			return nullptr;
		}

		char devInstParentID[MAX_DEVICE_ID_LEN];
		if (CM_Get_Device_ID(devInstParent, devInstParentID, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
			return nullptr;
		}

		std::string vid, pid, serial;
		if (!_parseDeviceID(devInstParentID, vid, pid, serial)) {
			return nullptr;
		}

		USBDevicePtr pUsbDevice = USBDevicePtr(new USBDevice());
		//struct USBDrive_Win *pUsbDevice2 = new struct USBDrive_Win;
		//assert(pUsbDevice2 != NULL);

		//pUsbDevice->opaque = (void *)usb_info2;
		//pUsbDevice2->device_inst = devInstParent;

		pUsbDevice->uid = "";
		pUsbDevice->uid.append(vid);
		pUsbDevice->uid.append("-");
		pUsbDevice->uid.append(pid);
		pUsbDevice->uid.append("-");

		if (serial.empty()) {
			static ulong numUnserializedDevices = 0;

			char buf[100];
			sprintf_s(buf, sizeof buf, "0x%lx", numUnserializedDevices++);
			pUsbDevice->uid.append(buf);
		}
		else {
			pUsbDevice->uid.append(serial);
		}

		pUsbDevice->locationID = 0; // Not set.
		// Convert HEX values to integers
		pUsbDevice->productID = std::stoi(pid, nullptr, 0);
		pUsbDevice->vendorID = std::stoi(vid, nullptr, 0);
		pUsbDevice->product = deviceName;
		pUsbDevice->serialNumber = serial;
		pUsbDevice->vendor = vendor; // TODO
		pUsbDevice->mountPoint = mount;

		// Add to global device map
		gAllDevices[pUsbDevice->uid] = pUsbDevice;

		return pUsbDevice;
	}

	std::vector<USBDevicePtr> getDevices()
	{
		std::vector<USBDevicePtr> ret;

		const GUID *guid = &GUID_DEVINTERFACE_DISK;
		HDEVINFO hDeviceInfo = SetupDiGetClassDevs(guid, NULL, NULL,
												   (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

		if (hDeviceInfo != INVALID_HANDLE_VALUE) {
			std::vector<DeviceSPData> spsData = _deviceSPs(hDeviceInfo, guid);

			for (auto &sp : spsData)
			{
				auto pDevice = _extractUSBDeviceData(hDeviceInfo, sp);

				if (pDevice != nullptr) {
					ret.push_back(pDevice);
				}
			}
		}

		return ret;
	}

	USBDevicePtr getDevice(const std::string &uid)
	{
		return gAllDevices[uid];
	}

	bool unmount(const std::string &uid)
	{
		/*
		struct USBDrive *pUsbDevice = GetDevice(device_id);
		if (pUsbDevice == NULL || usb_info->mount.size() == 0) {
			return false;
		}

		PNP_VETO_TYPE VetoType = PNP_VetoTypeUnknown;
		char VetoName[MAX_PATH];
		VetoName[0] = '\0';

		if (CM_Request_Device_Eject(
			((struct USBDrive_Win *)pUsbDevice->opaque)->device_inst,
			&VetoType, VetoName, MAX_PATH, 0) == CR_SUCCESS) {
			return true;
		}
		printf("error when requesting device eject %d: %s\n", VetoType, VetoName);
		return false;
		*/

		throw "Not implemented";

		return false;
	}
}  // namespace usb_driver
