/******************************************************************************
 * The purpose of this program is to interpret Microsoft Outlook vcalendar
 * attachments, and display the times unambiguously in *your* local timezone.
 *
 * If you need to add new MS Outlook <-> POSIX timezone mappings, place them
 * below in Ms2Posix[].
 * 
 * Tue Apr  6 11:23:22 EDT 2021
 * John Robertson <john@rrci.com>
 */

#define _GNU_SOURCE
#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ez_libc.h"
#include "ptrvec.h"
#include "str.h"
#include "util.h"

/* My preferred output format for date+time */
#define STRFTIME_FMT "%H:%M %A %B %d, %Y %Z"

/*===========================================================================*/
/*=================== Forward declarations ==================================*/
/*===========================================================================*/
/* Can't get the #define _XOPEN_SOURCE thing to work */
char *strptime(const char *s, const char *format, struct tm *tm);

/* Application-specific functions */
static time_t vcal2utc(const char *src);
static const char *unescape(const char *src);
static const char *fetchPerson(const char *src);

/*===========================================================================*/
/*=================== static data ===========================================*/
/*===========================================================================*/
/***************************************************
 * Array of these structs provides the mapping between
 * Microsoft TZID or "TZID display name" to a POSIX
 * timezone. If you encounter a TZID not listed here,
 * please add in the correct mapping.
 */
const static struct tz_xref {
   const char *ms,
              *posix;
} Ms2Posix[]= {
   {.ms= "(UTC-06:00) Central Time (US & Canada)", .posix= "America/Chicago" },
   {.ms= "Central Standard Time", .posix= "America/Chicago" },
   /*--- >>> LOOK <<< Add other members here ---*/
   { /* Terminating member */ }
};

/*** Main information for code in this source file ***/
static struct {
   /* Flags to make a note of information we've found */
   enum {
      START_FLG    =1<<0,
      END_FLG      =1<<1,
      SUMMARY_FLG  =1<<2,
      LOCATION_FLG =1<<3,
      ORG_FLG      =1<<4,
      DESC_FLG     =1<<5,
      SCHED_FLG    =1<<6,
   } flags;

   /* String storage for report information */
   char summary[1024],
        location[1024],
        organizer[1024],
        description[4096];

   /* Time storage for report information */
   time_t start,
          end,
          scheduled;

   /* Vector of attendee strings */
   PTRVEC attendee_vec;

   struct {
      int major,
          minor,
          patch;
   } version;

} S= {
   .version.major= 0,
   .version.minor= 0,
   .version.patch= 0
};

/*===========================================================================*/
/*========================== Stuff for main() ===============================*/
/*===========================================================================*/

/* Enums for long options */
enum {
   VERSION_OPT_ENUM=128, /* Larger than any printable character */
   HELP_OPT_ENUM
};

/*===========================================================================*/
/*======================== main() ===========================================*/
/*===========================================================================*/
int
main(int argc, char **argv)
/******************************************************
 * Program execution begins here.
 */
{
   int rtn= EXIT_FAILURE;
   /* Static data construction */
   PTRVEC_constructor(&S.attendee_vec, 10);

   { /****** Command line option processing ******/
      extern char *optarg;
      extern int optind, opterr, optopt;
      int errflg= 0;
      for(;;) {

         static const struct option long_options[]= {
            {"version", no_argument, 0, VERSION_OPT_ENUM},
            {"help", no_argument, 0, HELP_OPT_ENUM},
            {/* Terminating member */}
         };

         int c, option_ndx= 0;

         c= getopt_long(argc, argv, ":", long_options, &option_ndx);
         if(-1 == c) break;

         switch(c) {

            case VERSION_OPT_ENUM:
               ez_fprintf(stdout, "%s v%d.%d.%d\n", argv[0], S.version.major, S.version.minor, S.version.patch);
               fflush(stdout);
               return EXIT_SUCCESS;

            case HELP_OPT_ENUM:
               ++errflg;
               break;

         }
      }

      if(errflg) {
         ez_fprintf(stderr,
            "Usage:\n"
            "%s [options] [vcalendar_file]\n"
            " vcalendar_file\t\tMS Outlook vcalendar attachment (if absent, stdin is used).\n"
            " --help\t\t\tprint this Help message and exit.\n"
            " --version\t\tprint program Version numbers.\n"
            , argv[0]
             );

         fflush(stderr);
         goto abort;
         
      }
   } /* End command line option processing */

   static char buf[4096];
   FILE *fh= stdin;

   /* File name may have been supplied on command line */
   if(1 < argc)
      fh= ez_fopen(argv[1], "r");

   /*=== Grab one line at a time from source ===*/
   while (ez_fgets(buf, sizeof(buf), fh)) {

      /* Get rid of whitespace on the end */
      trimend(buf);

      /** Possible continuation(s) **/
      while(!feof(fh)) {

         int bol= fgetc(fh);

         if(' ' != bol) { /* Not a continutation, put back & quit loop */
            ungetc(bol, fh);
            break;
         }
         
         size_t sz= strlen(buf);
         ez_fgets(buf+sz, sizeof(buf)-sz, fh);
         trimend(buf);
      }

      /** At this point the line has been reassembled **/
      if(!strncmp(buf, "DTSTART;", 8)) { // Start time of the event

         S.start= vcal2utc(buf + 8);
         if(-1 == S.start)
            goto abort;

         S.flags |= START_FLG;

      } else if(!strncmp(buf, "DTEND;", 6)) { // End time of the event

         S.end= vcal2utc(buf + 6);
         if(-1 == S.end)
            goto abort;

         S.flags |= END_FLG;

      } else if(!strncmp(buf, "DTSTAMP:", 8)) { // When meeting was scheduled, UTC

         const char *tm_str= buf + 7; // NOTE: vcal2utc() needs a preceding colon

         /* Convert 'struct tm' into time_t */
         S.scheduled= vcal2utc(tm_str);
         if(-1 == S.scheduled)
            goto abort;

         S.flags |= SCHED_FLG;

      } else if(!strncmp(buf, "ORGANIZER;", 10)) { //  Event organizer

         /* Fetch formatted personal information */
         const char *str= fetchPerson(buf + 10);

         if(!str)
            goto abort;

         strncpy(S.organizer, skipspacec(str), sizeof(S.organizer));
         trimend(S.organizer);
         S.flags |= ORG_FLG;

      } else if(!strncmp(buf, "LOCATION;", 9)) { // Event location

         const char *line= strstr(buf + 9, ":");
         if(!line) {
            eprintf("ERROR: cannot extract location from  \"%s\"", buf);
            goto abort;
         }
         ++line;
         strncpy(S.location, line, sizeof(S.location));

         S.flags |= LOCATION_FLG;

      } else if(!strncmp(buf, "SUMMARY;", 8)) { //  Event summary

         const char *line= strstr(buf + 8, ":");
         if(!line) {
            eprintf("ERROR: cannot extract summary from  \"%s\"", buf);
            goto abort;
         }
         ++line;
         strncpy(S.summary, line, sizeof(S.summary));

         S.flags |= SUMMARY_FLG;

      } else if(!strncmp(buf, "DESCRIPTION;", 12)) { // Event description

         const char *line= strstr(buf + 8, ":");
         if(!line) {
            eprintf("ERROR: cannot extract description from  \"%s\"", buf);
            goto abort;
         }
         ++line;

         /* Copy unescaped string to our storage location */
         strncpy(S.description, skipspacec(unescape(line)), sizeof(S.description));

         /* Get rid of trailing whitespace */
         trimend(S.description);

         S.flags |= DESC_FLG;

      } else if(!strncmp(buf, "ATTENDEE;", 9)) { // Attendees

         const char *str= fetchPerson(buf + 9);
         if(!str)
            goto abort;

         PTRVEC_addTail(&S.attendee_vec, strdup(str));
      }
   }

   /*=== Should have all available information. Print formatted info ===*/
   if(S.flags & START_FLG)
      ez_fprintf(stdout, "Event start: %s\n", local_strftime(&S.start, STRFTIME_FMT));

   if(S.flags & END_FLG)
      ez_fprintf(stdout, "  Event end: %s\n", local_strftime(&S.end, STRFTIME_FMT));

   if(S.flags & SUMMARY_FLG)
      ez_fprintf(stdout, "\nSummary: %s\n\t%s\n"
            , S.flags & SCHED_FLG ? local_strftime(&S.scheduled, STRFTIME_FMT) : ""
            , S.summary
            );

   if(S.flags & LOCATION_FLG)
      ez_fprintf(stdout, "\nEvent location: %s\n", S.location);

   if(S.flags & ORG_FLG)
      ez_fprintf(stdout, "\nEvent organizer: %s\n", S.organizer);

   /* Attendees */
   if(PTRVEC_numItems(&S.attendee_vec)) {
      ez_fprintf(stdout, "\nAttendees:\n");
      unsigned i;
      const char *str;
      PTRVEC_loopFwd(&S.attendee_vec, i, str) {
         ez_fprintf(stdout, "\t%s\n", str);
      }
   }

   if(S.flags & DESC_FLG)
      ez_fprintf(stdout, "\nDescription:\n\t%s\n", S.description);

   /* Successful */
   rtn= EXIT_SUCCESS;

abort:
   return rtn;
}

/*===========================================================================*/
/*===================== supporting functions ================================*/
/*===========================================================================*/

static time_t
vcal2utc(const char *src)
/******************************************************
 * Convert the vcalendar time to UTC time_t
 */
{
   time_t rtn= -1;
   /* Find the ": sequence preceding the UTC time */

   const char *tm_str;
   if(strchr(src, '"')) {

      /* Case for Microsoft timezone "display name" */
      tm_str= strstr(src, "\":");

      /* Make sure we found it */
      if(!tm_str) {
         eprintf("ERROR: Could not find date+time string in \"%s\"", src);
         goto abort;
      }

      /* Skip over sentinel chars */
      tm_str += 2;
   } else {
      /* Case for regular Microsoft timezone */
      tm_str= strstr(src, ":");
      /* Make sure we found it */
      if(!tm_str) {
         eprintf("ERROR: Could not find date+time string in \"%s\"", src);
         goto abort;
      }

      /* Skip over sentinel chars */
      tm_str += 1;
   }

   /* Initialize a 'struct tm' buffer */
   struct tm tm= TM_INITIAL;
   char *nxt;

   /* Parse string to get populate 'struct tm' */
   if(!(nxt= strptime(tm_str, "%Y%m%dT%H%M%S", &tm))) {
      eprintf("ERROR: strptime failed parsing \"%s\"", tm_str);
      goto abort;
   }

   /* Date+time string may have been supplied UTC, or some local time zone */
   if('Z' == *nxt) { // UTC

      /* Convert 'struct tm' into time_t */
      rtn= timegm(&tm);

   } else {

      /* Date+time was supplied in local time */
      const char *TZ_orig= getenv("TZ");

      /* Identify the timezone, and set it */
      const struct tz_xref *xref;
      for(xref= Ms2Posix; xref->ms; ++xref) {
         if(strcasestr(src, xref->ms)) {
            setenv("TZ", xref->posix, 1);
            break;
         } 
      }

      /* Make sure we didn't reach the end */
      if(!xref->ms) {
         eprintf("ERROR: Could not find timezone match for \"%s\"", src);
         goto abort;
      }

      /* Convert 'struct tm' into time_t */
      rtn= mktime(&tm);

      /* Now switch the TZ back to what it was */
      if(TZ_orig)
         setenv("TZ", TZ_orig, 1);
      else
         unsetenv("TZ");
   }

abort:
   return rtn;

}

static const char*
unescape(const char *src)
/******************************************************
 * Un-escape escaped characters in string, return result
 * in a static buffer
 */
{
   static STR sb;
   STR_sinit(&sb, 1024);
   for(; *src; ++src) {

      /* Check for possible escaped character */
      if('\\' == *src && *(src+1)) {
         switch(*(src+1)) {
            case 'n':
               STR_append(&sb, "\n\t", 2);
               break;

            case 't':
               STR_putc(&sb, '\t');
               break;

            /* NOTE: There could be other escaped characters,
             * but I haven't seen them
             */

            default:
               /* Escaped character has no special meaning */
               STR_putc(&sb, *(src+1));
         }

         ++src;
         continue;
      }

      /* Otherwise copy character as-is */
      STR_putc(&sb, *src);
   }

   return STR_str(&sb);
}

static const char*
fetchPerson(const char *src)
/******************************************************
 * Fetch person information & supply result in static
 * buffer.
 */
{
   const char *rtn= NULL;
   static STR sb;
   STR_sinit(&sb, 1024);

   char name[64],
        email[128];

   const char *str= strstr(src, "CN=");
   if(!str)
      goto abort;

   int rc= sscanf(str + 3, "%63[^:]:MAILTO:%127s", name, email);
   if(2 != rc)
      goto abort;

   /* Note required participants */
   if(strstr(src, "REQ-PARTICIPANT"))
      STR_append(&sb, "(req'd) ", -1);

   /* Print formatted info to buffer */
   STR_sprintf(&sb, "%s <%s>", name, email);


   /* Successful return */
   rtn= STR_str(&sb);

abort:
   if(!rtn)
      eprintf("ERROR: cannot extract organizer from  \"%s\"", src);
   return rtn;
}