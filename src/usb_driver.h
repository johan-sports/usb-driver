#ifndef SRC_USB_DRIVER_H_
#define SRC_USB_DRIVER_H_

#include <string>
#include <vector>

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

    void *opaque;
  } USBDevice;

  std::vector<USBDevice*> GetDevices();
  USBDevice *GetDevice(const std::string &device_id);

  bool Unmount(const std::string &device_id);
}

#endif  // SRC_USB_DRIVER_H_
