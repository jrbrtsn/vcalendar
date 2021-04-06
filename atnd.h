/************************************************************
 * Class to help with printing out attendees.
 */
#ifndef ATND_H
#define ATND_H


typedef struct _ATND {
   enum {
      ATND_REQD_FLG=1<<0
   } flags;

   char name[64],
        email[128];
} ATND;

#ifdef __cplusplus
extern "C"
{
#endif

#define ATND_create(p, initMaxItems) \
  ((p)=(ATND_constructor((p)=malloc(sizeof(ATND)), initMaxItems) ? (p) : ( p ? realloc(ATND_destructor(p),0) : 0 )))
ATND*
ATND_constructor (ATND * self, const char *src);
/***********************************************
 * Construct a ATND.
 *
 * src is the string with attendee information.
 * returns - pointer to the object, or NULL for failure.
 */


void*
ATND_destructor (ATND * self);
/***********************************************
 * Destruct a ATND.
 */

int
ATND_report(ATND *self, FILE *fh);
/***********************************************
 * Print out Attendee information for report
 */

int
ATND_ptrvec_cmp(const void *const* pp1, const void *const* pp2);
/***********************************************
 * Comparision function for PTRVEC_sort()
 */

#define ATND_destroy(p) \
  do {if(ATND_destructor(p)) {free(p); p= NULL;}} while(0)

#ifdef __cplusplus
}
#endif

#endif
