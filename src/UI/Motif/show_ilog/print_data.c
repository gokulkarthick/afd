/*
 *  print_data.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1997 - 2012 Holger Kiehl <Holger.Kiehl@dwd.de>
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

DESCR__S_M3
/*
 ** NAME
 **   print_data - prints data from the input log
 **
 ** SYNOPSIS
 **   void print_data(void)
 **
 ** DESCRIPTION
 **
 ** RETURN VALUES
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   26.05.1997 H.Kiehl Created
 **   18.03.2000 H.Kiehl Modified to make it more generic.
 **
 */
DESCR__E_M3

#include <stdio.h>               /* sprintf(), popen(), pclose()         */
#include <string.h>              /* strcpy(), strcat(), strerror()       */
#include <stdlib.h>              /* exit()                               */
#include <unistd.h>              /* write(), close()                     */
#include <time.h>                /* strftime()                           */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <Xm/Xm.h>
#include <Xm/Text.h>
#include <Xm/List.h>
#include <Xm/LabelP.h>
#include <errno.h>
#include "mafd_ctrl.h"
#include "show_ilog.h"

/* External global variables. */
extern Display      *display;
extern Widget       listbox_w,
                    printshell,
                    statusbox_w,
                    summarybox_w;
extern char         file_name[],
                    header_line[],
                    search_file_name[],
                    **search_dir,
                    **search_recipient,
                    search_file_size_str[],
                    summary_str[],
                    total_summary_str[];
extern int          no_of_search_dirs,
                    no_of_search_dirids,
                    no_of_search_hosts,
                    sum_line_length;
extern unsigned int *search_dirid;
extern XT_PTR_TYPE  device_type,
                    range_type;
extern time_t       start_time_val,
                    end_time_val;
extern FILE         *fp;

/* Local function prototypes. */
static void        write_header(int, char *),
                   write_summary(int, char *);


/*######################### print_data_button() #########################*/
void
print_data_button(Widget w, XtPointer client_data, XtPointer call_data)
{
   char message[MAX_MESSAGE_LENGTH],
        sum_sep_line[MAX_OUTPUT_LINE_LENGTH + SHOW_LONG_FORMAT + 1];

   /* Prepare separator line. */
   (void)memset(sum_sep_line, '=', sum_line_length);
   sum_sep_line[sum_line_length] = '\0';

   if (range_type == SELECTION_TOGGLE)
   {
      int no_selected,
          *select_list;

      if (XmListGetSelectedPos(listbox_w, &select_list, &no_selected) == False)
      {
         show_message(statusbox_w, "No data selected for printing!");
         XtPopdown(printshell);

         return;
      }
      else
      {
         int           fd,
                       prepare_status;
         char          *line,
                       line_buffer[256];
         XmStringTable all_items;

         if (device_type == PRINTER_TOGGLE)
         {
            prepare_status = prepare_printer(&fd);
         }
         else
         {
            prepare_status = prepare_file(&fd, (device_type == MAIL_TOGGLE) ? 0 : 1);
            if ((prepare_status != SUCCESS) && (device_type == MAIL_TOGGLE))
            {
               prepare_tmp_name();
               prepare_status = prepare_file(&fd, 1);
            }
         }
         if (prepare_status == SUCCESS)
         {
            register int i,
                         length;

            write_header(fd, sum_sep_line);

            XtVaGetValues(listbox_w, XmNitems, &all_items, NULL);
            for (i = 0; i < no_selected; i++)
            {
               XmStringGetLtoR(all_items[select_list[i] - 1],
                               XmFONTLIST_DEFAULT_TAG, &line);
               length = sprintf(line_buffer, "%s\n", line);
               if (write(fd, line_buffer, length) != length)
               {
                  (void)fprintf(stderr, "write() error : %s (%s %d)\n",
                                strerror(errno), __FILE__, __LINE__);
                  XtFree(line);
                  exit(INCORRECT);
               }
               XtFree(line);
               XmListDeselectPos(listbox_w, select_list[i]);
            }

            /*
             * Remember to insert the correct summary, since all files
             * have now been deselected.
             */
            write_summary(fd, sum_sep_line);
            (void)strcpy(summary_str, total_summary_str);
            SHOW_SUMMARY_DATA();

            if (device_type == PRINTER_TOGGLE)
            {
               int  status;
               char buf;

               /* Send Control-D to printer queue. */
               buf = CONTROL_D;
               if (write(fd, &buf, 1) != 1)
               {
                  (void)fprintf(stderr, "write() error : %s (%s %d)\n",
                                strerror(errno), __FILE__, __LINE__);
                  XtFree(line);
                  exit(INCORRECT);
               }

               if ((status = pclose(fp)) < 0)
               {
                  (void)sprintf(message,
                                "Failed to send printer command (%d) : %s",
                                status, strerror(errno));
               }
               else
               {
                  (void)sprintf(message, "Send job to printer (%d)", status);
               }
            }
            else
            {
               if (close(fd) < 0)
               {
                  (void)fprintf(stderr, "close() error : %s (%s %d)\n",
                                strerror(errno), __FILE__, __LINE__);
               }
               if (device_type == MAIL_TOGGLE)
               {
                  send_mail_cmd(message);
               }
               else
               {
                  (void)sprintf(message, "Send job to file %s.", file_name);
               }
            }
         }
         XtFree((char *)select_list);
      }
   }
   else /* Print everything! */
   {
      int           fd,
                    no_of_items,
                    prepare_status;
      char          *line,
                    line_buffer[256];
      XmStringTable all_items;

      if (device_type == PRINTER_TOGGLE)
      {
         prepare_status = prepare_printer(&fd);
      }
      else
      {
         prepare_status = prepare_file(&fd, (device_type == MAIL_TOGGLE) ? 0 : 1);
         if ((prepare_status != SUCCESS) && (device_type == MAIL_TOGGLE))
         {
            prepare_tmp_name();
            prepare_status = prepare_file(&fd, 1);
         }
      }
      if (prepare_status == SUCCESS)
      {
         register int i,
                      length;

         write_header(fd, sum_sep_line);

         XtVaGetValues(listbox_w,
                       XmNitemCount, &no_of_items,
                       XmNitems,     &all_items,
                       NULL);
         for (i = 0; i < no_of_items; i++)
         {
            XmStringGetLtoR(all_items[i], XmFONTLIST_DEFAULT_TAG, &line);
            length = sprintf(line_buffer, "%s\n", line);
            if (write(fd, line_buffer, length) != length)
            {
               (void)fprintf(stderr, "write() error : %s (%s %d)\n",
                             strerror(errno), __FILE__, __LINE__);
               XtFree(line);
               exit(INCORRECT);
            }
            XtFree(line);
         }
         write_summary(fd, sum_sep_line);

         if (device_type == PRINTER_TOGGLE)
         {
            int  status;
            char buf;

            /* Send Control-D to printer queue. */
            buf = CONTROL_D;
            if (write(fd, &buf, 1) != 1)
            {
               (void)fprintf(stderr, "write() error : %s (%s %d)\n",
                             strerror(errno), __FILE__, __LINE__);
               XtFree(line);
               exit(INCORRECT);
            }

            if ((status = pclose(fp)) < 0)
            {
               (void)sprintf(message,
                             "Failed to send printer command (%d) : %s",
                             status, strerror(errno));
            }
            else
            {
               (void)sprintf(message, "Send job to printer (%d)", status);
            }
         }
         else
         {
            if (close(fd) < 0)
            {
               (void)fprintf(stderr, "close() error : %s (%s %d)\n",
                             strerror(errno), __FILE__, __LINE__);
            }
            if (device_type == MAIL_TOGGLE)
            {
               send_mail_cmd(message);
            }
            else
            {
               (void)sprintf(message, "Send job to file %s.", file_name);
            }
         }
      }
   }

   show_message(statusbox_w, message);
   XtPopdown(printshell);

   return;
}


/*+++++++++++++++++++++++++++ write_header() ++++++++++++++++++++++++++++*/
static void
write_header(int fd, char *sum_sep_line)
{
   int  length;
   char buffer[1024];

   length = sprintf(buffer,
                    "                                AFD INPUT LOG\n\n");
   if ((start_time_val < 0) && (end_time_val < 0))
   {
      length += sprintf(&buffer[length],
                       "\tTime Interval : earliest entry - latest entry\n\tFile name     : %s\n\tFile size     : %s\n",
                       search_file_name, search_file_size_str);
   }
   else if ((start_time_val > 0) && (end_time_val < 0))
        {
           length += strftime(&buffer[length], 1024 - length,
                              "\tTime Interval : %m.%d. %H:%M",
                              localtime(&start_time_val));
           length += sprintf(&buffer[length],
                             " - latest entry\n\tFile name     : %s\n\tFile size     : %s\n",
                             search_file_name, search_file_size_str);
        }
        else if ((start_time_val < 0) && (end_time_val > 0))
             {
                length += strftime(&buffer[length], 1024 - length,
                                   "\tTime Interval : earliest entry - %m.%d. %H:%M",
                                   localtime(&end_time_val));
                length += sprintf(&buffer[length],
                                  "\n\tFile name     : %s\n\tFile size     : %s\n",
                                  search_file_name, search_file_size_str);
             }
             else
             {
                length += strftime(&buffer[length], 1024 - length,
                                   "\tTime Interval : %m.%d. %H:%M",
                                   localtime(&start_time_val));
                length += strftime(&buffer[length], 1024 - length,
                                   " - %m.%d. %H:%M",
                                   localtime(&end_time_val));
                length += sprintf(&buffer[length],
                                  "\n\tFile name     : %s\n\tFile size     : %s\n",
                                  search_file_name, search_file_size_str);
             }

   if ((no_of_search_dirs > 0) || (no_of_search_dirids > 0))
   {
      int i;

      if (no_of_search_dirs > 0)
      {
         length += sprintf(&buffer[length], "\tDirectory     : %s\n",
                           search_dir[0]);
         for (i = 1; i < no_of_search_dirs; i++)
         {
            length += sprintf(&buffer[length], "\t                %s\n",
                              search_dir[i]);
         }
      }
      if (no_of_search_dirids > 0)
      {
         length += sprintf(&buffer[length], "\tDir Identifier: %x",
                           search_dirid[0]);
         for (i = 1; i < no_of_search_dirids; i++)
         {
            length += sprintf(&buffer[length], ", %x", search_dirid[i]);
         }
         length += sprintf(&buffer[length], "\n");
      }
   }
   else
   {
      length += sprintf(&buffer[length], "\tDirectory     :\n");
   }
   if (no_of_search_hosts > 0)
   {
      int i;

      length += sprintf(&buffer[length], "\tHost name     : %s",
                        search_recipient[0]);
      for (i = 1; i < no_of_search_hosts; i++)
      {
         length += sprintf(&buffer[length], ", %s", search_recipient[i]);
      }
      length += sprintf(&buffer[length], "\n\n");
   }
   else
   {
      length += sprintf(&buffer[length], "\tHost name     :\n\n");
   }

   /* Don't forget the heading for the data. */
   length += sprintf(&buffer[length], "%s\n%s\n",
                     header_line, sum_sep_line);

   /* Write heading to file/printer. */
   if (write(fd, buffer, length) != length)
   {
      (void)fprintf(stderr, "write() error : %s (%s %d)\n",
                    strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }

   return;
}


/*+++++++++++++++++++++++++++ write_summary() +++++++++++++++++++++++++++*/
static void
write_summary(int fd, char *sum_sep_line)
{
   int  length;
   char buffer[(2 * (MAX_OUTPUT_LINE_LENGTH + SHOW_LONG_FORMAT + 5 + 1))];

   length = sprintf(buffer, "%s\n%s\n", sum_sep_line, summary_str);

   /* Write summary to file/printer. */
   if (write(fd, buffer, length) != length)
   {
      (void)fprintf(stderr, "write() error : %s (%s %d)\n",
                    strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }

   return;
}
