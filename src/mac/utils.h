#ifndef _USB_DRIVER_UTILS_H__
#define _USB_DRIVER_UTILS_H__

#include <stdio.h>

#include <CoreFoundation/CoreFoundation.h>

////////////////////////////////////////////////////////////////////////////////
// Logging
////////////////////////////////////////////////////////////////////////////////
#define log(file, fmt, args...) fprintf(file, "%s:%d " fmt, __func__, __LINE__, ##args)
// Log to stdout
#define dlog(fmt, args...) log(stdout, fmt, ##args)
// Log to the stderr
#define elog(fmt, args...) log(stderr, fmt, ##args)

////////////////////////////////////////////////////////////////////////////////
// Converters
////////////////////////////////////////////////////////////////////////////////
/**
 * Convert the given CFString to a CString
 */
char *cfStringRefToCString(CFStringRef cfString);
/**
 */
char *cfTypeToCString(CFTypeRef cfString);

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

#define PROP_VAL(dict, key)                                   \
  ({                                                          \
    CFTypeRef _str = CFDictionaryGetValue(dict, CFSTR(key));  \
    cfTypeToCString(_str);                                    \
  })



#endif // _USB_DRIVER_UTILS_H__
