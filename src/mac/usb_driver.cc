#include "../usb_driver.h"
#include "../utils.h"
#include "interop.h"

#include <sys/param.h>

#include <stdio.h>

#include <mach/mach_error.h>
#include <mach/mach_port.h>

#include <IOKit/IOBSD.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>

#include <DiskArbitration/DiskArbitration.h>

// The current OSX version
const auto CURRENT_SUPPORTED_VERSION = __MAC_OS_X_VERSION_MAX_ALLOWED;
// El Capitan is 101100 (in AvailabilityInternal.h)
const auto EL_CAPITAN = 101100;
// IOUSBDevice has become IOUSBHostDevice in El Capitan
const char *SERVICE_MATCHER = CURRENT_SUPPORTED_VERSION < EL_CAPITAN ? "IOUSBDevice" : "IOUSBHostDevice";

namespace USBDriver
{
  /**
   * Struct representing extra OS X disk data.
   */
  typedef struct USBDevice_Mac {
    std::string bsdDiskName;
  } USBDevice_Mac;

  // TODO: Remove me!
  static std::vector<USBDevice *> gAllDevices;


  /**
   * Generate a new UID for the given device
   *
   * TODO: Make this re-usable
   */
  std::string _uniqueDeviceId(const USBDevice *device)
  {
    static unsigned long numUnserializedDevices = 0;
    std::string uid;

    if (device->uid.empty()) {
      uid.append(device->vendorID);
      uid.append("-");
      uid.append(device->productID);
      uid.append("-");

      if (!device->serialNumber.empty()) {
        uid.append(device->serialNumber);
      }
      else {
        char buf[100];

        snprintf(buf, sizeof(buf), "0x%lx", numUnserializedDevices++);

        uid.append(buf);
      }
    }

    return uid;
  }


  /**
   * Unmount the device with the given identifier.
   */
  bool Unmount(const std::string &identifier)
  {
    struct USBDevice *usbInfo = GetDevice(identifier);

    // Only unmount if we're actually mounted
    if (usbInfo != nullptr && !usbInfo->mountPoint.empty()) {
      DASessionRef daSession = DASessionCreate(kCFAllocatorDefault);
      assert(daSession != nullptr);

      // Attempt to actually reference the path
      CFURLRef volumePath = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                                                                    (const UInt8 *)usbInfo->mountPoint.c_str(),
                                                                    usbInfo->mountPoint.size(),
                                                                    true);
      assert(volumePath != nullptr);

      // Attempt to get a disk reference
      DADiskRef disk = DADiskCreateFromVolumePath(kCFAllocatorDefault,
                                                  daSession,
                                                  volumePath);

      if (disk != nullptr)
        {
          // Attempt to unmount the disk
          // TODO: pass error callback and escalate error to JS.
          DADiskUnmount(disk, kDADiskUnmountOptionDefault, nullptr, NULL);
          CFRelease(disk);

          // Rewrite mount as empty
          usbInfo->mountPoint = "";

          return true;
        }

      CFRelease(volumePath);
      CFRelease(daSession);
    }

    return false;
  }

  /**
   * Get the device with the given identifier.
   */
  USBDevice *GetDevice(const std::string &identifier)
  {
    auto foundDevice = std::find_if(gAllDevices.begin(),
                                    gAllDevices.end(),
                                    // Lambda that attempts to match the identifier
                                    [&identifier](const USBDevice *dev) { return identifier == dev->uid; });

    if(foundDevice != gAllDevices.end())
      return *foundDevice;
    else
      return nullptr;
  }

  // TODO: Make this referentialy transparent, in the way that it
  // TODO: doesn't modify gAllDevices. 
  static USBDevice *usbServiceObject(io_service_t usbService)
  {
    CFMutableDictionaryRef properties;
    kern_return_t kr = IORegistryEntryCreateCFProperties(usbService,
                                                         &properties,
                                                         kCFAllocatorDefault,
                                                         kNilOptions);
    if (kr != kIOReturnSuccess) {
      dlog("IORegistryEntryCreateCFProperties() failed: %s", mach_error_string(kr));

      return nullptr;
    }

    // Get the given property

    USBDevice *usbInfo = nullptr;
    USBDevice_Mac *usbInfo_mac = nullptr;


    std::string locationID = PROP_VAL(properties, "locationID");

    for(const auto device : gAllDevices) {
      if(device->locationID == locationID) {
        usbInfo = device;
        usbInfo_mac = static_cast<USBDevice_Mac *>(usbInfo->opaque);
      }
    }

    if (usbInfo == nullptr) {
      usbInfo = new USBDevice;

      usbInfo_mac = new USBDevice_Mac;

      usbInfo->opaque = (void *)usbInfo_mac;
      gAllDevices.push_back(usbInfo);
    }


    usbInfo->locationID    = locationID;
    usbInfo->vendorID      = PROP_VAL(properties, kUSBVendorID);
    usbInfo->productID     = PROP_VAL(properties, kUSBProductID);
    usbInfo->serialNumber  = PROP_VAL(properties, kUSBSerialNumberString);
    usbInfo->product       = PROP_VAL(properties, kUSBProductString);
    usbInfo->vendor        = PROP_VAL(properties, kUSBVendorString);

    // Convert string to a hex format
    HEXIFY(usbInfo->productID);
    HEXIFY(usbInfo->vendorID);


    CFRelease(properties);

    usbInfo->uid = _uniqueDeviceId(usbInfo);

    CFStringRef bsdName = (CFStringRef)IORegistryEntrySearchCFProperty(usbService,
                                                                       kIOServicePlane,
                                                                       CFSTR(kIOBSDNameKey),
                                                                       kCFAllocatorDefault,
                                                                       kIORegistryIterateRecursively);
    if (bsdName != nullptr) {
      char bsdNameBuf[4096];
      sprintf( bsdNameBuf, "/dev/%ss1", cfStringRefToCString(bsdName));
      char* bsdNameC = &bsdNameBuf[0];

      usbInfo_mac->bsdDiskName = bsdNameC;

      DASessionRef daSession = DASessionCreate(kCFAllocatorDefault);
      assert(daSession != nullptr);

      DADiskRef disk = DADiskCreateFromBSDName(kCFAllocatorDefault,
                                               daSession, bsdNameC);

      if (disk != nullptr) {
        CFDictionaryRef desc = DADiskCopyDescription(disk);

        if (desc != nullptr) {
          //CFTypeRef str = CFDictionaryGetValue(desc, kDADiskDescriptionVolumeNameKey);
          CFTypeRef str = CFDictionaryGetValue(desc, kDADiskDescriptionVolumeNameKey);
          char* volumeName = cfTypeToCString(str);

          if (volumeName && strlen(volumeName))
            {
              char volumePath[MAXPATHLEN];

              sprintf(volumePath, "/Volumes/%s", volumeName);

              usbInfo->mountPoint = volumePath;
            }


          CFRelease(desc);
        }

        CFRelease(disk);
        CFRelease(daSession);
      }
    }

    return usbInfo;
  }

  std::vector<USBDevice *> GetDevices()
  {
    mach_port_t masterPort;
    kern_return_t kr = IOMasterPort(MACH_PORT_NULL, &masterPort);

    assert(kr == kIOReturnSuccess);

    CFDictionaryRef usbMatching = IOServiceMatching(SERVICE_MATCHER);

    assert(usbMatching != nullptr);

    std::vector<USBDevice *> devices;

    io_iterator_t iter = 0;
    kr = IOServiceGetMatchingServices(kIOMasterPortDefault,
                                      usbMatching,
                                      &iter);

    if (kr != kIOReturnSuccess)
      {
        dlog("IOServiceGetMatchingServices() failed: %s", mach_error_string(kr));
      }
    else
      {

        io_service_t usbService;

        while ((usbService = IOIteratorNext(iter)) != 0) {
          USBDevice *usbInfo = usbServiceObject(usbService);


          if (usbInfo != nullptr) {
            devices.push_back(usbInfo);
          }

          IOObjectRelease(usbService);
        }
      }


    mach_port_deallocate(mach_task_self(), masterPort);

    return devices;
  }

}
