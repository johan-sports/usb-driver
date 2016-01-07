#ifndef _USB_DRIVER_UTILS_FORMATTERS_H__
#define _USB_DRIVER_UTILS_FORMATTERS_H__

////////////////////////////////////////////////////////////////////////////////
// Formatters
////////////////////////////////////////////////////////////////////////////////
#define HEXIFY(str)                                           \
  do {                                                        \
    char _tmp[100];                                           \
    snprintf(_tmp, sizeof _tmp, "0x%lx", atol(str.c_str()));  \
    str = _tmp;                                               \
  }                                                           \
  while (0)

#endif // _USB_DRIVER_UTILS_FORMATTERS_H__
