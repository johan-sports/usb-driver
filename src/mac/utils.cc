#include "utils.h"

#include <sys/param.h>

char *cfStringRefToCString(CFStringRef cfString)
{
  if (!cfString) return "";

  static char string[2048];

  string[0] = '\0';
  CFStringGetCString(cfString,
                     string,
                     MAXPATHLEN,
                     kCFStringEncodingASCII);

  return &string[0];
}

char *cfTypeToCString(CFTypeRef cfString)
{
  if(!cfString) return "";

  // Check that we're actually using a string
  if(CFGetTypeID(cfString) != CFStringGetTypeID()) {
    // Attempt to convert it to a string
    cfString = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@"), cfString);
  }

  static char deviceFilePath[2048];

  deviceFilePath[0] = '\0';

  CFStringGetCString(CFCopyDescription(cfString),
                     deviceFilePath,
                     MAXPATHLEN,
                     kCFStringEncodingASCII);

  char* p = deviceFilePath;

  while (*p != '\"')
    p++;

  p++;

  char* pp = p;

  while (*pp != '\"')
    pp++;

  *pp = '\0';

  if (isdigit(*p))
    *p = 'x';

  elog("C string: %s\n", p);

  return p;
}

