#include "../usb_driver.h"
#include "../usb_common.h"
#include "../utils.h"

#include <stdio.h>
#include <unordered_map>

#include <libudev.h>

namespace USBDriver
{
  typedef std::unordered_map<std::string, USBDevicePtr> DeviceMap;

  // TODO: Remove me!
  static DeviceMap gAllDevices;

  std::vector<USBDevicePtr> getDevices()
  {
    struct udev *udev;
    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices, *dev_list_entry;
    struct udev_device *dev;

    udev = udev_new();
    if(!udev) {
      CORE_FATAL("Can't create udev");
    }

    // Create a list of items in the 'hidraw' subsystem.
    enumerate = udev_enumerate_new(udev);
    // We're looking for devices that have file systems
    udev_enumerate_add_match_subsystem(enumerate, "block");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    std::vector<USBDevicePtr> returnDevices;

    udev_list_entry_foreach(dev_list_entry, devices) {
      const char *path;

      // Get the filename of the /sys entry for the device
      // and create a udev_device object (dev) representing it.
      path = udev_list_entry_get_name(dev_list_entry);
      dev = udev_device_new_from_syspath(udev, path);

      printf("Device node path: %s\n", udev_device_get_devnode(dev));

      // Attempt to get USB device that contains USB specific data
      dev = udev_device_get_parent_with_subsystem_devtype(
              dev,
              "usb",
              "usb_device"
      );

      if(!dev) {
        CORE_INFO("Block has no parent with USB device, continuing...");
        continue;
      }

      printf("sys path: %s\n", udev_device_get_syspath(dev));
      printf("dev path: %s\n", udev_device_get_devpath(dev));

      USBDevicePtr usbInfo = USBDevicePtr(new USBDevice);
      // FIXME: This is a hex string
      usbInfo->vendorID = atoi(udev_device_get_sysattr_value(dev, "idVendor"));
      usbInfo->productID = atoi(udev_device_get_sysattr_value(dev, "idProduct"));
      usbInfo->serialNumber = udev_device_get_sysattr_value(dev, "serial");
      usbInfo->vendor = udev_device_get_sysattr_value(dev, "manufacturer");
      usbInfo->product = udev_device_get_sysattr_value(dev, "product");
      usbInfo->uid = uniqueDeviceID(usbInfo);

      returnDevices.push_back(usbInfo);

      // Cleanup
      udev_device_unref(dev);

      // Cache
      gAllDevices[usbInfo->uid] = usbInfo;
    }
    // Free enumerator
    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    return returnDevices;
  }

  USBDevicePtr getDevice(const std::string &uid)
  {
    return gAllDevices[uid];
  }

  // TODO
  bool unmount(const std::string &uid)
  {
    return false;
  }
}
