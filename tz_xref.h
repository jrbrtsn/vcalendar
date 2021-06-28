#ifndef TZ_XREF_H
#define TZ_XREF_H

/* Use this to cross-reference timezones between Windows & POSIX */

struct tz_xref {
   const char *ms,
              *posix;
};

extern struct tz_xref Ms2Posix[];

#endif

