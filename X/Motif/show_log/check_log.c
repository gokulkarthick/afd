/*
 *  check_log.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1996 - 2003 Holger Kiehl <Holger.Kiehl@dwd.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "afddefs.h"

DESCR__S_M1
/*
 ** NAME
 **   check_log - Checks if there is any new data to be displayed
 **
 ** SYNOPSIS
 **   void check_log(Widget w)
 **
 ** DESCRIPTION
 **   The function check_log() always checks 'p_log_file' for any
 **   new data to be displayed.
 **
 ** RETURN VALUES
 **   None.
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   16.03.1996 H.Kiehl Created
 **   31.05.1997 H.Kiehl Added debug toggle.
 **   22.11.2003 H.Kiehl Do not show each individual line, buffer the
 **                      information and then show in one block.
 **
 */
DESCR__E_M1

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <Xm/Xm.h>
#include <Xm/Text.h>
#include <errno.h>
#include "show_log.h"
#include "logdefs.h"

extern Display        *display;
extern XtAppContext   app;
extern XtIntervalId   interval_id_host;
extern XmTextPosition wpr_position;
extern Cursor         cursor1,
                      cursor2;
extern Widget         toplevel,
                      counterbox,
                      log_scroll_bar;
extern int            toggles_set,
                      current_log_number,
                      line_counter,
                      log_type_flag,
                      no_of_hosts;
extern unsigned int   total_length,
                      toggles_set_parallel_jobs;
extern ino_t          current_inode_no;
extern char           log_dir[MAX_PATH_LENGTH],
                      log_name[MAX_FILENAME_LENGTH],
                      **hosts;
extern FILE           *p_log_file;
static int            first_time = YES;

/* Local function */
static void           display_data(Widget, int *, int *, int *,
                                   unsigned int *, char *);

#define MAX_LINES_IN_ONE_GO 100


/*############################# check_log() #############################*/
void
check_log(w)
Widget w;
{
#ifdef _SLOW_COUNTER
   static int old_line_counter = 0;
#endif
   int        i,
              length,
              locked = 0,
              lock_counter = 1,
              cursor_counter = 1;
   char       line[MAX_LINE_LENGTH + 1],
              str_line[MAX_LINE_COUNTER_DIGITS + 1];

   if (p_log_file != NULL)
   {
      int          max_lines = 0;
      unsigned int chars_buffered = 0;
      char         *line_buffer;

      if ((line_buffer = malloc(MAX_LINE_LENGTH * MAX_LINES_IN_ONE_GO)) == NULL)
      {
         (void)xrec(toplevel, FATAL_DIALOG, "malloc() error : %s (%s %d)",
                    strerror(errno), __FILE__, __LINE__);
         return;
      }
      if (no_of_hosts > 0)
      {
         while (fgets(line, MAX_LINE_LENGTH, p_log_file) != NULL)
         {
            if (max_lines < MAX_LINES_IN_ONE_GO)
            {
               length = strlen(line);
               total_length += length;
               if ((log_type_flag == TRANSFER_LOG_TYPE) ||
                   (log_type_flag == TRANS_DB_LOG_TYPE))
               {
                  if ((length > (LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4)) &&
                      ((((toggles_set & SHOW_INFO) == 0) && (line[LOG_SIGN_POSITION] == 'I')) ||
                       (((toggles_set & SHOW_WARN) == 0) && (line[LOG_SIGN_POSITION] == 'W')) ||
                       (((toggles_set & SHOW_ERROR) == 0) && (line[LOG_SIGN_POSITION] == 'E')) ||
                       (((toggles_set & SHOW_FATAL) == 0) && (line[LOG_SIGN_POSITION] == 'F')) ||
                       (((toggles_set & SHOW_DEBUG) == 0) && (line[LOG_SIGN_POSITION] == 'D')) ||
#ifdef _TOGGLED_PROC_SELECTION
                       (((toggles_set_parallel_jobs & 1) == 0) && (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] == '0')) ||
                       (((toggles_set_parallel_jobs & 2) == 0) && (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] == '1')) ||
                       (((toggles_set_parallel_jobs & 4) == 0) && (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] == '2')) ||
                       (((toggles_set_parallel_jobs & 8) == 0) && (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] == '3')) ||
                       (((toggles_set_parallel_jobs & 16) == 0) && (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] == '4'))))
#else
                       (((toggles_set_parallel_jobs - 1) != (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] - 48)) &&
                       (toggles_set_parallel_jobs != 0))))
#endif
                  {
                     continue;
                  }
               }
               else
               {
                  if ((length > LOG_SIGN_POSITION) &&
                      ((((toggles_set & SHOW_INFO) == 0) && (line[LOG_SIGN_POSITION] == 'I')) ||
                       (((toggles_set & SHOW_CONFIG) == 0) && (line[LOG_SIGN_POSITION] == 'C')) ||
                       (((toggles_set & SHOW_WARN) == 0) && (line[LOG_SIGN_POSITION] == 'W')) ||
                       (((toggles_set & SHOW_ERROR) == 0) && (line[LOG_SIGN_POSITION] == 'E')) ||
                       (((toggles_set & SHOW_FATAL) == 0) && (line[LOG_SIGN_POSITION] == 'F')) ||
                       (((toggles_set & SHOW_DEBUG) == 0) && (line[LOG_SIGN_POSITION] == 'D'))))
                  {
                     continue;
                  }
               }

               for (i = 0; i < no_of_hosts; i++)
               {
                  if (log_filter(hosts[i], &line[16]) == 0)
                  {
                     memcpy(&line_buffer[chars_buffered], line, length);
                     chars_buffered += length;
                     line_counter++;
                     max_lines++;

                     break;
                  }
               }
            }
            else
            {
               max_lines = 0;
               display_data(w, &lock_counter, &cursor_counter, &locked,
                            &chars_buffered, line_buffer);
            }
         }
      }
      else /* We are searching for ALL hosts */
      {
         while (fgets(line, MAX_LINE_LENGTH, p_log_file) != NULL)
         {
            if (max_lines < MAX_LINES_IN_ONE_GO)
            {
               length = strlen(line);
               total_length += length;
               if ((log_type_flag == TRANSFER_LOG_TYPE) ||
                   (log_type_flag == TRANS_DB_LOG_TYPE))
               {
                  if ((length > (LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4)) &&
                      ((((toggles_set & SHOW_INFO) == 0) && (line[LOG_SIGN_POSITION] == 'I')) ||
                       (((toggles_set & SHOW_WARN) == 0) && (line[LOG_SIGN_POSITION] == 'W')) ||
                       (((toggles_set & SHOW_ERROR) == 0) && (line[LOG_SIGN_POSITION] == 'E')) ||
                       (((toggles_set & SHOW_FATAL) == 0) && (line[LOG_SIGN_POSITION] == 'F')) ||
                       (((toggles_set & SHOW_DEBUG) == 0) && (line[LOG_SIGN_POSITION] == 'D')) ||
#ifdef _TOGGLED_PROC_SELECTION
                       (((toggles_set_parallel_jobs & 1) == 0) && (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] == '0')) ||
                       (((toggles_set_parallel_jobs & 2) == 0) && (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] == '1')) ||
                       (((toggles_set_parallel_jobs & 4) == 0) && (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] == '2')) ||
                       (((toggles_set_parallel_jobs & 8) == 0) && (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] == '3')) ||
                       (((toggles_set_parallel_jobs & 16) == 0) && (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] == '4'))))
#else
                       (((toggles_set_parallel_jobs - 1) != (line[LOG_SIGN_POSITION + MAX_HOSTNAME_LENGTH + 4] - 48)) &&
                       (toggles_set_parallel_jobs != 0))))
#endif
                  {
                     continue;
                  }
               }
               else
               {
                  if ((length > LOG_SIGN_POSITION) &&
                      ((((toggles_set & SHOW_INFO) == 0) && (line[LOG_SIGN_POSITION] == 'I')) ||
                       (((toggles_set & SHOW_CONFIG) == 0) && (line[LOG_SIGN_POSITION] == 'C')) ||
                       (((toggles_set & SHOW_WARN) == 0) && (line[LOG_SIGN_POSITION] == 'W')) ||
                       (((toggles_set & SHOW_ERROR) == 0) && (line[LOG_SIGN_POSITION] == 'E')) ||
                       (((toggles_set & SHOW_FATAL) == 0) && (line[LOG_SIGN_POSITION] == 'F')) ||
                       (((toggles_set & SHOW_DEBUG) == 0) && (line[LOG_SIGN_POSITION] == 'D'))))
                  {
                     continue;
                  }
               }
               memcpy(&line_buffer[chars_buffered], line, length);
               chars_buffered += length;
               line_counter++;
               max_lines++;
            }
            else
            {
               max_lines = 0;
               display_data(w, &lock_counter, &cursor_counter, &locked,
                            &chars_buffered, line_buffer);
            }
         }
      }
      if (chars_buffered > 0)
      {
         max_lines = 0;
         display_data(w, &lock_counter, &cursor_counter, &locked,
                      &chars_buffered, line_buffer);
      }
      free(line_buffer);
   }

   /* Has a new log file been created? */
   if ((log_type_flag != TRANSFER_LOG_TYPE) &&
       (log_type_flag != RECEIVE_LOG_TYPE) &&
       (total_length > MAX_LOGFILE_SIZE) &&
       (current_log_number == 0))
   {
      char        log_file[MAX_PATH_LENGTH];
      struct stat stat_buf;

      /*
       * When disk is full the process system_log/transfer_log will not
       * be able to start a new log file. We must check if this is the
       * case by looking at the inode number of the 'new' log file and
       * compare this with the old one. If the inodes are the same, we
       * know that the log process has failed to create a new log file.
       */
      (void)sprintf(log_file, "%s/%s0", log_dir, log_name);
      if ((stat(log_file, &stat_buf) != -1) &&
          (stat_buf.st_ino != current_inode_no))
      {
         /* Yup, time to change the log file! */
         if (p_log_file != NULL)
         {
            (void)fclose(p_log_file);
            p_log_file = NULL;
         }

         /* Lets see if there is a new log file */
         if ((p_log_file = fopen(log_file, "r")) != NULL)
         {
#ifdef _SLOW_COUNTER
            old_line_counter = 0;
#endif
            line_counter = 0;
            wpr_position = 0;
            total_length = 0;
            XmTextSetInsertionPosition(w, wpr_position);
            XmTextSetString(w, NULL);  /* Clears all old entries */
            (void)sprintf(str_line, "%*d", MAX_LINE_COUNTER_DIGITS, 0);
            XmTextSetString(counterbox, str_line);
            current_inode_no = stat_buf.st_ino;
         }
      }
   }

   /* Reset cursor and ignore any events that might have occurred */
   if (locked == 1)
   {
      XSetWindowAttributes attrs;
      XEvent               event;

      attrs.cursor = None;
      XChangeWindowAttributes(display, XtWindow(toplevel),
                              CWCursor, &attrs);
      XFlush(display);

      /* Get rid of all events that have occurred */
      while (XCheckMaskEvent(XtDisplay(toplevel),
                             ButtonPressMask | ButtonReleaseMask |
                             ButtonMotionMask | PointerMotionMask |
                             KeyPressMask, &event) == True)
      {
         /* do nothing */;
      }
   }

#ifdef _SLOW_COUNTER
   if (old_line_counter != line_counter)
   {
      old_line_counter = line_counter;

      (void)sprintf(str_line, "%*d", MAX_LINE_COUNTER_DIGITS, line_counter);
      XmTextSetString(counterbox, str_line);
   }
#endif

   first_time = NO;
   interval_id_host = XtAppAddTimeOut(app, LOG_TIMEOUT,
                                      (XtTimerCallbackProc)check_log, w);

   return;
}


/*---------------------------- display_data() ---------------------------*/
static void
display_data(Widget       w,
             int          *lock_counter,
             int          *cursor_counter,
             int          *locked,
             unsigned int *chars_buffered,
             char         *line_buffer)
{
   static int           tflag = 0;
   int                  current_value,
                        max_value,
                        slider_size;
#ifndef _SLOW_COUNTER
   char                 str_line[MAX_LINE_COUNTER_DIGITS + 1];
#endif
   XSetWindowAttributes attrs;

   if ((*lock_counter % 10) == 0)
   {
      if (*locked == 0)
      {
         *locked = 1;
         attrs.cursor = cursor2;
         XChangeWindowAttributes(display, XtWindow(toplevel), CWCursor, &attrs);
      }
      XFlush(display);
      XmUpdateDisplay(toplevel);
   }
   if ((*cursor_counter % FALLING_SAND_SPEED) == 0)
   {
      if (tflag == 0)
      {
         tflag = 1;
         attrs.cursor = cursor1;
      }
      else
      {
         tflag = 0;
         attrs.cursor = cursor2;
      }
      XChangeWindowAttributes(display, XtWindow(toplevel), CWCursor, &attrs);
   }
   (*lock_counter)++; (*cursor_counter)++;

   /*
    * When searching in a log file, by moving slider up/down, it is
    * annoying when new log information arrives and the cursor jumps to
    * the bottom to show the new text. Thus lets disable auto scrolling
    * when we are not at the end of the text.
    */ 
   XtVaGetValues(log_scroll_bar,
                 XmNvalue,      &current_value,
                 XmNmaximum,    &max_value,
                 XmNsliderSize, &slider_size,
                 NULL);

   XmTextInsert(w, wpr_position, line_buffer);
   wpr_position += *chars_buffered;
   XtVaSetValues(w, XmNcursorPosition, wpr_position, NULL);
   *chars_buffered = 0;

   if (((max_value - slider_size) == current_value) ||
        (first_time == YES))
   {
      XmTextShowPosition(w, wpr_position);
   }

#ifndef _SLOW_COUNTER
   (void)sprintf(str_line, "%*d", MAX_LINE_COUNTER_DIGITS, line_counter);
   XmTextSetString(counterbox, str_line);
#endif

   return;
}
