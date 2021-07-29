#ifndef DEBUG_H
#define DEBUG_H

#ifdef DEBUG

#include <time.h>
#include <stdarg.h>

extern time_t dprint_time;

void dprint_init();

void dprint(const char *fmt, ...);

#else

#define dprint_init(a)
#define dprint

#endif

#endif
