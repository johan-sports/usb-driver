#ifndef SRC_USB_DRIVER_H_
#define SRC_USB_DRIVER_H_

#include <string>
#include <vector>
#include <memory>

namespace USBDriver
{
  typedef struct USBDevice {
    std::string uid;
    int locationID;
    int productID;
    int vendorID;
    std::string product;
    std::string serialNumber;
    std::string vendor;
    std::string mountPoint;

    // Extra device data
    std::shared_ptr<void> opaque;
  } USBDevice;

  // Shared resource to the USB device
  // TODO: Make all of this const
  typedef std::shared_ptr<USBDevice> USBDevicePtr;

  std::vector<USBDevicePtr> GetDevices();
  USBDevicePtr GetDevice(const std::string &device_id);

  bool Unmount(const std::string &device_id);
}

#endif  // SRC_USB_DRIVER_H_
