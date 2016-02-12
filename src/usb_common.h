#ifndef _USB_DRIVER_USB_COMMON_H__
#define _USB_DRIVER_USB_COMMON_H__

#include "usb_driver.h"
#include <string>

namespace USBDriver
{
  std::string uniqueDeviceID(const USBDevicePtr device);
}

#endif // _USB_DRIVER_USB_COMMON_H__
