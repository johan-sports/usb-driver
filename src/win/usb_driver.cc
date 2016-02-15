#include "../usb_driver.h"
#include "../usb_common.h"

#include "../utils.h"

#include <windows.h>
#include <windowsx.h>
#include <devguid.h>
#include <initguid.h>
#include <usbiodef.h>
#include <setupapi.h>
#include <winioctl.h>
#include <usbioctl.h>
#include <cfgmgr32.h>
#include <assert.h>

#include <unordered_map>
#include <bitset>

#define FORMAT_FLAGS (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS)

#define _PWSTR(str) reinterpret_cast<PWSTR>(str)

////////////////////////////////////////////////////////////////////////////////
// Dynamicaly loaded SetupAPI functions.
////////////////////////////////////////////////////////////////////////////////
typedef HDEVINFO (WINAPI *_SetupDiGetClassDevs) (
	const GUID *ClassGuid,
	PCTSTR Enumerator,
	HWND hwndParent,
	DWORD flags
);

typedef BOOL (WINAPI *_SetupDiGetDeviceRegistryProperty) (
	HDEVINFO DeviceInfoSet,
	PSP_DEVINFO_DATA DeviceInfoData,
	DWORD Property,
	PDWORD PropertyRegDataType,
	PBYTE PropertyBuffer,
	DWORD PropertyBufferSize,
	PDWORD RequiredSize
);

typedef BOOL (WINAPI *_SetupDiGetDeviceInterfaceDetail) (
	HDEVINFO DeviceInfoSet,
	PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
	PSP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData,
	DWORD DeviceInterfaceDetailDataSize,
	PDWORD RequiredSize,
	PSP_DEVINFO_DATA DeviceInfoData
);

typedef BOOL(WINAPI *_SetupDiEnumDeviceInterfaces) (
	HDEVINFO DeviceInfoSet,
	PSP_DEVINFO_DATA DeviceInfoData,
	const GUID *InterfaceClassGuid,
	DWORD MemberIndex,
	PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData
);

typedef BOOL (WINAPI *_SetupDiEnumDeviceInfo) (
	HDEVINFO DeviceInfoSet,
	DWORD MemberIndex,
	PSP_DEVINFO_DATA DeviceInfoData
);

typedef CONFIGRET (WINAPI *_CM_Get_Device_ID) (
  DEVINST dnDevInst,
  PWSTR Buffer,
  ULONG BufferLen,
  ULONG ulFlags
);

typedef CONFIGRET (WINAPI *_CM_Get_Parent) (
  PDEVINST pdnDevInst,
  DEVINST dnDevInst,
  ULONG ulFlags
);

_SetupDiGetClassDevs DLLSetupDiGetClassDevs;
_SetupDiGetDeviceRegistryProperty DLLSetupDiGetDeviceRegistryProperty;
_SetupDiGetDeviceInterfaceDetail DLLSetupDiGetDeviceInterfaceDetail;
_SetupDiEnumDeviceInterfaces DLLSetupDiEnumDeviceInterfaces;
_SetupDiEnumDeviceInfo DLLSetupDiEnumDeviceInfo;

_CM_Get_Device_ID DLLCM_Get_Device_ID;
_CM_Get_Parent DLLCM_Get_Parent;


/*
 * Dynamically load a function from a DLL reference.
 *
 * Returns NULL on failure.
 */
template<typename T>
T _loadProcedure(HINSTANCE hDLL, const std::string &name)
{
	 T fn = (T) GetProcAddress(hDLL, name.c_str());

	 // Throw a fatal error on load failure
	 if (fn == nullptr) {
     CORE_FATAL(name + " could not be linked.");
	 }

	 return fn;
}

HINSTANCE  _loadSetupApi()
{
	// Use as static to avoid reloading
	static HINSTANCE hDLL = nullptr;

	if (hDLL == nullptr) {
		hDLL = LoadLibrary("setupapi.dll");
	}

	// Should be loaded now
	if (!hDLL) {
		// THROW A FATAL ERROR
    CORE_FATAL("setupapi.dll failed to load.");
	}

  // Device setup procedures
	DLLSetupDiGetClassDevs = _loadProcedure<_SetupDiGetClassDevs>(hDLL, "SetupDiGetClassDevsA");
	DLLSetupDiGetDeviceRegistryProperty = _loadProcedure<_SetupDiGetDeviceRegistryProperty>(hDLL, "SetupDiGetDeviceRegistryPropertyA");
	DLLSetupDiGetDeviceInterfaceDetail = _loadProcedure<_SetupDiGetDeviceInterfaceDetail>(hDLL, "SetupDiGetDeviceInterfaceDetailA");
	DLLSetupDiEnumDeviceInterfaces = _loadProcedure<_SetupDiEnumDeviceInterfaces>(hDLL, "SetupDiEnumDeviceInterfaces");
	DLLSetupDiEnumDeviceInfo = _loadProcedure<_SetupDiEnumDeviceInfo>(hDLL, "SetupDiEnumDeviceInfo");

  // CM procedures
  DLLCM_Get_Parent = _loadProcedure<_CM_Get_Parent>(hDLL, "CM_Get_Parent");
  DLLCM_Get_Device_ID = _loadProcedure<_CM_Get_Device_ID>(hDLL, "CM_Get_Device_IDA");

	return hDLL;
}

void _closeSetupApi(HINSTANCE hDLL)
{
	if (hDLL != nullptr) {
		FreeLibrary(hDLL);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Implementation
////////////////////////////////////////////////////////////////////////////////
namespace USBDriver {
  typedef unsigned long ulong;
  typedef unsigned int  uint;

  static std::unordered_map<std::string, USBDevicePtr> gAllDevices;

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

  static bool _deviceProperty(HDEVINFO hDeviceInfo, PSP_DEVINFO_DATA device_info_data,
                              DWORD property, std::string &buf_str)
  {
    DWORD bufLen = MAX_PATH;
    DWORD nSize;

    char *buf = static_cast<char*>(malloc(bufLen));
    assert(buf != NULL);

    bool ok = DLLSetupDiGetDeviceRegistryProperty(hDeviceInfo, device_info_data,
                                               property, NULL, (PBYTE)buf,
                                               bufLen, &nSize);

    if (ok)
      buf_str.assign(buf);
    else
      CORE_ERROR("Failed to get device registry property: " + std::to_string(property));

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


  static ULONG _deviceNumberFromHandle(HANDLE handle)
  {
    STORAGE_DEVICE_NUMBER sdn;
    sdn.DeviceNumber = -1;

    DWORD bytesReturned = 0; // Ignored

    if (!DeviceIoControl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn,
                        sizeof(sdn), &bytesReturned, NULL)) {
      CORE_WARNING("Failed to get device number from handle.");

      return -1;
    }

    return sdn.DeviceNumber;
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
          CORE_ERROR("Failed to get file handle to " + path);
          continue;
        }

        ULONG num = _deviceNumberFromHandle(driveHandle);
        if (num != -1) {
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

        if (!DLLSetupDiEnumDeviceInfo(hDeviceInfo, index, &spDevInfoData))
          {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
              CORE_ERROR("Failed to retrieve device information.");

            break; // We're out of devices
          }

        SP_DEVICE_INTERFACE_DATA spDeviceInterfaceData = _createSPType<SP_DEVICE_INTERFACE_DATA>();

        if (!DLLSetupDiEnumDeviceInterfaces(hDeviceInfo, 0, guid, index, &spDeviceInterfaceData)) {
          CORE_ERROR("Failed to get device interfaces.");
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

    if (!DLLSetupDiGetDeviceInterfaceDetail(hDeviceInfo, &sp.inter, spDeviceInterfaceDetail,
                                         interfaceDetailLen, &interfaceDetailLen, &spDeviceInfoData)) {
      CORE_ERROR("Failed to retrieve device interface details.");
      return nullptr;
    }

    HANDLE handle = CreateFile(spDeviceInterfaceDetail->DevicePath,
                               0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);

    if (handle == INVALID_HANDLE_VALUE) {
      CORE_ERROR("Failed to create file handle");
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
    if (DLLCM_Get_Parent(&devInstParent, spDeviceInfoData.DevInst, 0) != CR_SUCCESS) {
      return nullptr;
    }

    char devInstParentID[MAX_DEVICE_ID_LEN];
    if (DLLCM_Get_Device_ID(devInstParent, _PWSTR(devInstParentID), MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
      return nullptr;
    }

    std::string vid, pid, serial;
    if (!_parseDeviceID(devInstParentID, vid, pid, serial)) {
      return nullptr;
    }

    USBDevicePtr pUsbDevice = USBDevicePtr(new USBDevice());

    pUsbDevice->locationID = 0; // Not set.
    // Convert HEX values to integers
    pUsbDevice->productID = std::stoi(pid, nullptr, 0);
    pUsbDevice->vendorID = std::stoi(vid, nullptr, 0);
    pUsbDevice->product = deviceName;
    pUsbDevice->serialNumber = serial;
    pUsbDevice->vendor = vendor;
    pUsbDevice->mountPoint = mount;
    pUsbDevice->uid = uniqueDeviceID(pUsbDevice);

    // Add to global device map
    gAllDevices[pUsbDevice->uid] = pUsbDevice;

    return pUsbDevice;
  }

  std::vector<USBDevicePtr> getDevices()
  {
    HINSTANCE hDLL = _loadSetupApi();

    std::vector<USBDevicePtr> ret;

    const GUID *guid = &GUID_DEVINTERFACE_DISK;
    HDEVINFO hDeviceInfo = DLLSetupDiGetClassDevs(guid, NULL, NULL,
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

    _closeSetupApi(hDLL);

    return ret;
  }

  USBDevicePtr getDevice(const std::string &uid)
  {
    return gAllDevices[uid];
  }

  bool unmount(const std::string &uid)
  {
  	throw "Not implemented";
	}
}  // namespace usb_driver
