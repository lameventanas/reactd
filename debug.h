#ifndef DEBUG_H
#define DEBUG_H

#ifdef DEBUG

#include <stdarg.h>

void dprint(const char *fmt, ...);

#else

#define dprint

#endif

#endif
