#include <stdio.h>

#include "atnd.h"
#include "ez_libc.h"
#include "util.h"
#include "vcalendar.h"

ATND*
ATND_constructor (ATND * self, const char *src)
/***********************************************
 * Construct a ATND.
 *
 * src is the string with attendee information.
 * returns - pointer to the object, or NULL for failure.
 */
{
   ATND *rtn= NULL;

   memset(self, 0, sizeof(*self));

   const char *str= strstr(src, "CN=");
   if(!str)
      goto abort;

   int rc= sscanf(str + 3, "%63[^:]:MAILTO:%127s", self->name, self->email);
   if(2 != rc) {
      rc= sscanf(str + 3, "%63[^:]:mailto:%127s", self->name, self->email);
      if(2 != rc)
         goto abort;
   }

   /* Note required participants */
   if(strstr(src, "REQ-PARTICIPANT"))
      self->flags |= ATND_REQD_FLG;

   rtn= self;
abort:
   if(!rtn)
      eprintf("ERROR: cannot extract attendee from  \"%s\"", src);
   return rtn;
}

void*
ATND_destructor (ATND * self)
/***********************************************
 * Destruct a ATND.
 */
{
   return self;
}

int
ATND_report(ATND *self, FILE *fh)
/***********************************************
 * Print out Attendee information for report
 */
{
   const char *reqd= G.BOLD[0] ? G.BOLD : "*";

   ez_fprintf(fh, "\t%s%s%s <%s>\n"
         , self->flags & ATND_REQD_FLG ? reqd : ""
         , self->name
         , G.NORMAL
         , self->email
         );
   return 0;
}

int
ATND_ptrvec_cmp(const void *const* pp1, const void *const* pp2)
/***********************************************
 * Comparision function for PTRVEC_sort()
 */
{
   const ATND *a1= *(const ATND *const*)pp1,
              *a2= *(const ATND *const*)pp2;

   return strcasecmp(a1->name, a2->name);
}
