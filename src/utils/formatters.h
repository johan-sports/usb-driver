#ifndef _USB_DRIVER_UTILS_FORMATTERS_H__
#define _USB_DRIVER_UTILS_FORMATTERS_H__

#include <string>
#include <stdlib.h>

////////////////////////////////////////////////////////////////////////////////
// Formatters
////////////////////////////////////////////////////////////////////////////////
template<typename T>
std::string hexify(T val)
{
  std::string str = std::to_string(val);

  char tmp[str.size()];
  snprintf(tmp, sizeof(tmp), "0x%lx", atol(str.c_str()));

  return tmp;
}

#endif // _USB_DRIVER_UTILS_FORMATTERS_H__
