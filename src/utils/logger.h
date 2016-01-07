#ifndef _USB_DRIVER_UTILS_LOGGER_H__
#define _USB_DRIVER_UTILS_LOGGER_H__

////////////////////////////////////////////////////////////////////////////////
// Logging
////////////////////////////////////////////////////////////////////////////////
// Log to FILE stream
#define flog(file, fmt, args...) fprintf(file, "%s:%d " fmt, __func__, __LINE__, ##args)
// Log to stdout
#define dlog(fmt, args...) flog(stdout, fmt, ##args)
// Log to the stderr
#define elog(fmt, args...) flog(stderr, fmt, ##args)

#endif // _USB_DRIVER_UTILS_LOGGER_H__
