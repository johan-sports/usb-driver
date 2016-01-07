#ifndef _USB_DRIVER_MAC_UTILS_H__
#define _USB_DRIVER_MAC_UTILS_H__

#include <stdio.h>

#include <CoreFoundation/CoreFoundation.h>

////////////////////////////////////////////////////////////////////////////////
// Converters
////////////////////////////////////////////////////////////////////////////////
/**
 * Convert the given CFString to a CString
 */
char *cfStringRefToCString(CFStringRef cfString);
/**
 * Attempt to convert a CFTypeRef to a c string. This
 * could possibly fail and currently causes segfaults.
 */
char *cfTypeToCString(CFTypeRef cfString);

////////////////////////////////////////////////////////////////////////////////
// Working with data structures
////////////////////////////////////////////////////////////////////////////////
#define PROP_VAL(dict, key)                                   \
  ({                                                          \
    CFTypeRef _str = CFDictionaryGetValue(dict, CFSTR(key));  \
    cfTypeToCString(_str);                                    \
  })


#endif // _USB_DRIVER_MAC_UTILS_H__
