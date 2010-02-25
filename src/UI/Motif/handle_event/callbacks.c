/*
 *  callbacks.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 2007, 2008 Holger Kiehl <Holger.Kiehl@dwd.de>
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
 **   callbacks - all callback functions for module handle_event
 **
 ** SYNOPSIS
 **   void close_button(Widget w, XtPointer client_data, XtPointer call_data)
 **   void radio_button(Widget w, XtPointer client_data, XtPointer call_data)
 **   void set_button(Widget w, XtPointer client_data, XtPointer call_data)
 **   void toggle_button(Widget w, XtPointer client_data, XtPointer call_data)
 **
 ** DESCRIPTION
 **
 ** RETURN VALUES
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   24.06.2007 H.Kiehl Created
 **
 */
DESCR__E_M3

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <Xm/Xm.h>
#include <Xm/Text.h>
#include <Xm/ToggleB.h>
#include <errno.h>
#include "handle_event.h"

/* External global variables. */
extern Widget                     end_time_w,
                                  entertime_w,
                                  start_time_w,
                                  statusbox_w,
                                  text_w;
extern int                        acknowledge_type,
                                  fra_fd,
                                  fsa_fd,
                                  no_of_alias,
                                  no_of_dirs,
                                  no_of_hosts;
extern time_t                     end_time_val,
                                  start_time_val;
extern char                       **dir_alias,
                                  **host_alias,
                                  user[];
extern struct filetransfer_status *fsa;
extern struct fileretrieve_status *fra;

/* Local function prototypes. */
static int                        eval_time(char *, Widget, time_t *);


/*########################### close_button() ############################*/
void
close_button(Widget w, XtPointer client_data, XtPointer call_data)
{
   exit(0);
}


/*########################### toggle_button() ###########################*/
void
toggle_button(Widget w, XtPointer client_data, XtPointer call_data)
{
   if (XmToggleButtonGetState(w) == True)
   {
      XtSetSensitive(entertime_w, True);
      XmProcessTraversal(w, XmTRAVERSE_NEXT_TAB_GROUP);
   }
   else
   {
      XtSetSensitive(entertime_w, False);
   }

   return;
}


/*############################# save_input() ############################*/
void
save_input(Widget w, XtPointer client_data, XtPointer call_data)
{
   XT_PTR_TYPE type = (XT_PTR_TYPE)client_data;
   char        *value = XmTextGetString(w);

   switch (type)
   {
      case START_TIME_NO_ENTER :
         if (value[0] == '\0')
         {
            start_time_val = -1;
         }
         else if (eval_time(value, w, &start_time_val) < 0)
              {
                 show_message(statusbox_w, TIME_FORMAT);
                 XtFree(value);
                 return;
              }
         reset_message(statusbox_w);
         break;

      case START_TIME :
         if (eval_time(value, w, &start_time_val) < 0)
         {
            show_message(statusbox_w, TIME_FORMAT);
         }
         else
         {
            reset_message(statusbox_w);
            XmProcessTraversal(w, XmTRAVERSE_NEXT_TAB_GROUP);
         }
         break;

      case END_TIME_NO_ENTER :
         if (value[0] == '\0')
         {
            end_time_val = -1;
         }
         else if (eval_time(value, w, &end_time_val) < 0)
              {
                 show_message(statusbox_w, TIME_FORMAT);
                 XtFree(value);
                 return;
              }
         reset_message(statusbox_w);
         break;

      case END_TIME :
         if (eval_time(value, w, &end_time_val) < 0)
         {
            show_message(statusbox_w, TIME_FORMAT);
         }
         else
         {
            reset_message(statusbox_w);
            XmProcessTraversal(w, XmTRAVERSE_NEXT_TAB_GROUP);
         }
         break;

      default :
         (void)fprintf(stderr, "ERROR   : Impossible! (%s %d)\n",
                       __FILE__, __LINE__);
         exit(INCORRECT);
   }
   XtFree(value);

   return;
}


/*++++++++++++++++++++++++++++ eval_time() ++++++++++++++++++++++++++++++*/
static int
eval_time(char *numeric_str, Widget w, time_t *value)
{
   int    length = strlen(numeric_str),
          min,
          hour;
   time_t time_val;
   char   str[3];

   time_val = time(NULL);

   switch (length)
   {
      case 0 : /* Assume user means current time. */
               {
                  char time_str[9];

                  (void)strftime(time_str, 9, "%m%d%H%M", localtime(&time_val));
                  XmTextSetString(w, time_str);
               }
               return(time_val);
      case 3 :
      case 4 :
      case 5 :
      case 6 :
      case 7 :
      case 8 : break;
      default: return(INCORRECT);
   }

   if (numeric_str[0] == '-')
   {
      if ((!isdigit((int)numeric_str[1])) || (!isdigit((int)numeric_str[2])))
      {
         return(INCORRECT);
      }

      if (length == 3) /* -mm */
      {
         str[0] = numeric_str[1];
         str[1] = numeric_str[2];
         str[2] = '\0';
         min = atoi(str);
         if ((min < 0) || (min > 59))
         {
            return(INCORRECT);
         }

         *value = time_val - (min * 60);
      }
      else if (length == 5) /* -hhmm */
           {
              if ((!isdigit((int)numeric_str[3])) ||
                  (!isdigit((int)numeric_str[4])))
              {
                 return(INCORRECT);
              }

              str[0] = numeric_str[1];
              str[1] = numeric_str[2];
              str[2] = '\0';
              hour = atoi(str);
              if ((hour < 0) || (hour > 23))
              {
                 return(INCORRECT);
              }
              str[0] = numeric_str[3];
              str[1] = numeric_str[4];
              min = atoi(str);
              if ((min < 0) || (min > 59))
              {
                 return(INCORRECT);
              }

              *value = time_val - (min * 60) - (hour * 3600);
           }
      else if (length == 7) /* -DDhhmm */
           {
              int days;

              if ((!isdigit((int)numeric_str[3])) ||
                  (!isdigit((int)numeric_str[4])) ||
                  (!isdigit((int)numeric_str[5])) ||
                  (!isdigit((int)numeric_str[6])))
              {
                 return(INCORRECT);
              }

              str[0] = numeric_str[1];
              str[1] = numeric_str[2];
              str[2] = '\0';
              days = atoi(str);
              str[0] = numeric_str[3];
              str[1] = numeric_str[4];
              str[2] = '\0';
              hour = atoi(str);
              if ((hour < 0) || (hour > 23))
              {
                 return(INCORRECT);
              }
              str[0] = numeric_str[5];
              str[1] = numeric_str[6];
              min = atoi(str);
              if ((min < 0) || (min > 59))
              {
                 return(INCORRECT);
              }

              *value = time_val - (min * 60) - (hour * 3600) - (days * 86400);
           }
           else
           {
              return(INCORRECT);
           }

      return(SUCCESS);
   }

   if ((!isdigit((int)numeric_str[0])) || (!isdigit((int)numeric_str[1])) ||
       (!isdigit((int)numeric_str[2])) || (!isdigit((int)numeric_str[3])))
   {
      return(INCORRECT);
   }

   str[0] = numeric_str[0];
   str[1] = numeric_str[1];
   str[2] = '\0';

   if (length == 4) /* hhmm */
   {
      struct tm *bd_time;     /* Broken-down time. */

      hour = atoi(str);
      if ((hour < 0) || (hour > 23))
      {
         return(INCORRECT);
      }
      str[0] = numeric_str[2];
      str[1] = numeric_str[3];
      min = atoi(str);
      if ((min < 0) || (min > 59))
      {
         return(INCORRECT);
      }
      bd_time = localtime(&time_val);
      bd_time->tm_sec  = 0;
      bd_time->tm_min  = min;
      bd_time->tm_hour = hour;

      *value = mktime(bd_time);
   }
   else if (length == 6) /* DDhhmm */
        {
           int       day;
           struct tm *bd_time;     /* Broken-down time. */

           if ((!isdigit((int)numeric_str[4])) ||
               (!isdigit((int)numeric_str[5])))
           {
              return(INCORRECT);
           }
           day = atoi(str);
           if ((day < 0) || (day > 31))
           {
              return(INCORRECT);
           }
           str[0] = numeric_str[2];
           str[1] = numeric_str[3];
           hour = atoi(str);
           if ((hour < 0) || (hour > 23))
           {
              return(INCORRECT);
           }
           str[0] = numeric_str[4];
           str[1] = numeric_str[5];
           min = atoi(str);
           if ((min < 0) || (min > 59))
           {
              return(INCORRECT);
           }
           bd_time = localtime(&time_val);
           bd_time->tm_sec  = 0;
           bd_time->tm_min  = min;
           bd_time->tm_hour = hour;
           bd_time->tm_mday = day;

           *value = mktime(bd_time);
        }
        else /* MMDDhhmm */
        {
           int       month,
                     day;
           struct tm *bd_time;     /* Broken-down time. */

           if ((!isdigit((int)numeric_str[4])) ||
               (!isdigit((int)numeric_str[5])) ||
               (!isdigit((int)numeric_str[6])) ||
               (!isdigit((int)numeric_str[7])))
           {
              return(INCORRECT);
           }
           month = atoi(str);
           if ((month < 0) || (month > 12))
           {
              return(INCORRECT);
           }
           str[0] = numeric_str[2];
           str[1] = numeric_str[3];
           day = atoi(str);
           if ((day < 0) || (day > 31))
           {
              return(INCORRECT);
           }
           str[0] = numeric_str[4];
           str[1] = numeric_str[5];
           hour = atoi(str);
           if ((hour < 0) || (hour > 23))
           {
              return(INCORRECT);
           }
           str[0] = numeric_str[6];
           str[1] = numeric_str[7];
           min = atoi(str);
           if ((min < 0) || (min > 59))
           {
              return(INCORRECT);
           }
           bd_time = localtime(&time_val);
           bd_time->tm_sec  = 0;
           bd_time->tm_min  = min;
           bd_time->tm_hour = hour;
           bd_time->tm_mday = day;
           if ((bd_time->tm_mon == 0) && (month == 12))
           {
              bd_time->tm_year -= 1;
           }
           bd_time->tm_mon  = month - 1;

           *value = mktime(bd_time);
        }

   return(SUCCESS);
}


/*############################ set_button() #############################*/
void
set_button(Widget w, XtPointer client_data, XtPointer call_data)
{
   int          flags_changed,
                flags_unchangeable,
                i,
                not_enough_errors,
                pos;
   unsigned int event_action;
   char         *reason_str,
                *text;

   if ((text = XmTextGetString(text_w)))
   {
      if (text[0] == '\0')
      {
         reason_str = NULL;
      }
      else
      {
         size_t length;

         length = strlen(text);
         if ((reason_str = malloc((3 * length))) == NULL)
         {
            (void)fprintf(stderr, "malloc() error : %s\n", strerror(errno));
            exit(INCORRECT);
         }
         else
         {
            int j = 0;

            for (i = 0; i < length; i++)
            {
               if (text[i] >= ' ')
               {
                  if (text[i] == '%')
                  {
                     reason_str[j++] = text[i];
                     j += sprintf(&reason_str[j], "%02x", (int)text[i]);
                  }
                  else
                  {
                     reason_str[j++] = text[i];
                  }
               }
               else
               {
                  reason_str[j++] = '%';
                  j += sprintf(&reason_str[j], "%02x", (int)text[i]);
               }
            }
            reason_str[j] = '\0';
         }
      }
      XtFree(text);
   }
   else
   {
      reason_str = NULL;
   }

   flags_changed = flags_unchangeable = not_enough_errors = 0;
   for (i = 0; i < no_of_alias; i++)
   {
      event_action = 0;
      if (fra_fd == -1)
      {
         pos = get_host_position(fsa, host_alias[i], no_of_hosts);
         if (pos == INCORRECT)
         {
            (void)fprintf(stderr, "Failed to locate `%s' in FSA.",
                          host_alias[i]);
         }
         else
         {
            int flag_changed = NO;

            if ((fsa[pos].error_counter > 0) ||
                ((start_time_val != -1) && (start_time_val != end_time_val)) ||
                (acknowledge_type == UNSET_SELECT))
            {
               if (acknowledge_type == ACKNOWLEDGE_SELECT)
               {
                  if (start_time_val == -1)
                  {
                     if ((fsa[pos].host_status & HOST_ERROR_ACKNOWLEDGED) == 0)
                     {
#ifdef LOCK_DEBUG
                        lock_region_w(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS), __FILE__, __LINE__);
#else
                        lock_region_w(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS));
#endif
                        fsa[pos].host_status |= HOST_ERROR_ACKNOWLEDGED;
#ifdef LOCK_DEBUG
                        unlock_region(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS), __FILE__, __LINE__);
#else
                        unlock_region(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS));
#endif
                        flag_changed = YES;
                        flags_changed++;
                     }
                     else
                     {
                        flags_unchangeable++;
                     }
                  }
                  else
                  {
                     if ((fsa[pos].host_status & HOST_ERROR_ACKNOWLEDGED_T) == 0)
                     {
#ifdef LOCK_DEBUG
                        lock_region_w(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS), __FILE__, __LINE__);
#else
                        lock_region_w(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS));
#endif
                        fsa[pos].host_status |= HOST_ERROR_ACKNOWLEDGED_T;
#ifdef LOCK_DEBUG
                        unlock_region(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS), __FILE__, __LINE__);
#else
                        unlock_region(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS));
#endif
                        fsa[pos].start_event_handle = start_time_val;
                        fsa[pos].end_event_handle = end_time_val;
                        flag_changed = YES;
                        flags_changed++;
                     }
                     else
                     {
                        flags_unchangeable++;
                     }
                  }
                  event_action = EA_ACKNOWLEDGE;
               }
               else if (acknowledge_type == OFFLINE_SELECT)
                    {
                       if (start_time_val == -1)
                       {
                          if ((fsa[pos].host_status & HOST_ERROR_OFFLINE) == 0)
                          {
#ifdef LOCK_DEBUG
                             lock_region_w(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS), __FILE__, __LINE__);
#else
                             lock_region_w(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS));
#endif
                             fsa[pos].host_status |= HOST_ERROR_OFFLINE;
#ifdef LOCK_DEBUG
                             unlock_region(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS), __FILE__, __LINE__);
#else
                             unlock_region(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS));
#endif
                             flag_changed = YES;
                             flags_changed++;
                          }
                          else
                          {
                             flags_unchangeable++;
                          }
                       }
                       else
                       {
                          if ((fsa[pos].host_status & HOST_ERROR_OFFLINE_T) == 0)
                          {
#ifdef LOCK_DEBUG
                             lock_region_w(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS), __FILE__, __LINE__);
#else
                             lock_region_w(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS));
#endif
                             fsa[pos].host_status |= HOST_ERROR_OFFLINE_T;
#ifdef LOCK_DEBUG
                             unlock_region(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS), __FILE__, __LINE__);
#else
                             unlock_region(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS));
#endif
                             fsa[pos].start_event_handle = start_time_val;
                             fsa[pos].end_event_handle = end_time_val;
                             flag_changed = YES;
                             flags_changed++;
                          }
                          else
                          {
                             flags_unchangeable++;
                          }
                       }
                       event_action = EA_OFFLINE;
                    }
                    else /* Unset all the flags. */
                    {
#ifdef LOCK_DEBUG
                       lock_region_w(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS), __FILE__, __LINE__);
#else
                       lock_region_w(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS));
#endif
                       if (fsa[pos].host_status & HOST_ERROR_OFFLINE)
                       {
                          fsa[pos].host_status &= ~HOST_ERROR_OFFLINE;
                          flag_changed = YES;
                       }
                       if (fsa[pos].host_status & HOST_ERROR_ACKNOWLEDGED)
                       {
                          fsa[pos].host_status &= ~HOST_ERROR_ACKNOWLEDGED;
                          flag_changed = YES;
                       }
                       if (fsa[pos].host_status & HOST_ERROR_OFFLINE_T)
                       {
                          fsa[pos].host_status &= ~HOST_ERROR_OFFLINE_T;
                          fsa[pos].start_event_handle = 0L;
                          fsa[pos].end_event_handle = 0L;
                          flag_changed = YES;
                       }
                       if (fsa[pos].host_status & HOST_ERROR_ACKNOWLEDGED_T)
                       {
                          fsa[pos].host_status &= ~HOST_ERROR_ACKNOWLEDGED_T;
                          fsa[pos].start_event_handle = 0L;
                          fsa[pos].end_event_handle = 0L;
                          flag_changed = YES;
                       }
#ifdef LOCK_DEBUG
                       unlock_region(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS), __FILE__, __LINE__);
#else
                       unlock_region(fsa_fd, (AFD_WORD_OFFSET + (pos * sizeof(struct filetransfer_status)) + LOCK_HS));
#endif
                       if (flag_changed == NO)
                       {
                          flags_unchangeable++;
                       }
                       else
                       {
                          flags_changed++;
                       }
                       event_action = EA_UNSET_ACK_OFFL;
                    }
            }
            else
            {
               not_enough_errors++;
            }
            if (event_action != 0)
            {
               if (reason_str == NULL)
               {
                  if (flag_changed == YES)
                  {
                     event_log(0L, EC_HOST, ET_MAN, event_action, "%s%c%s",
                               host_alias[i], SEPARATOR_CHAR, user);
                  }
               }
               else
               {
                  if ((flag_changed == YES) ||
                      (event_action != EA_UNSET_ACK_OFFL))
                  {
                     event_log(0L, EC_HOST, ET_MAN, event_action, "%s%c%s%c%s",
                               host_alias[i], SEPARATOR_CHAR, user,
                               SEPARATOR_CHAR, reason_str);
                  }
               }
            }
         }
      }
      else
      {
         pos = get_dir_position(fra, dir_alias[i], no_of_dirs);
         if (pos == INCORRECT)
         {
            (void)fprintf(stderr, "Failed to locate `%s' in FRA.",
                          dir_alias[i]);
         }
         else
         {
            int flag_changed = NO;

            if ((fra[pos].error_counter > 0) ||
                (acknowledge_type == UNSET_SELECT))
            {
               if (acknowledge_type == ACKNOWLEDGE_SELECT)
               {
                  if ((fra[pos].dir_flag & DIR_ERROR_ACKN) == 0)
                  {
                     fra[pos].dir_flag |= DIR_ERROR_ACKN;
                     flag_changed = YES;
                     flags_changed++;
                  }
                  else
                  {
                     flags_unchangeable++;
                  }
                  event_action = EA_ACKNOWLEDGE;
               }
               else if (acknowledge_type == OFFLINE_SELECT)
                    {
                       if ((fra[pos].dir_flag & DIR_ERROR_OFFLINE) == 0)
                       {
                          fra[pos].dir_flag |= DIR_ERROR_OFFLINE;
                          flag_changed = YES;
                          flags_changed++;
                       }
                       else
                       {
                          flags_unchangeable++;
                       }
                       event_action = EA_OFFLINE;
                    }
                    else /* Unset all the flags. */
                    {
                       if (fra[pos].dir_flag & DIR_ERROR_ACKN)
                       {
                          fra[pos].dir_flag &= ~DIR_ERROR_ACKN;
                          flag_changed = YES;
                       }
                       if (fra[pos].dir_flag & DIR_ERROR_OFFLINE)
                       {
                          fra[pos].dir_flag &= ~DIR_ERROR_OFFLINE;
                          flag_changed = YES;
                       }
                       if (fra[pos].dir_flag & DIR_ERROR_ACKN_T)
                       {
                          fra[pos].dir_flag &= ~DIR_ERROR_ACKN_T;
                          flag_changed = YES;
                       }
                       if (fra[pos].dir_flag & DIR_ERROR_OFFL_T)
                       {
                          fra[pos].dir_flag &= ~DIR_ERROR_OFFL_T;
                          flag_changed = YES;
                       }
                       if (flag_changed == NO)
                       {
                          flags_unchangeable++;
                       }
                       else
                       {
                          flags_changed++;
                       }
                       event_action = EA_UNSET_ACK_OFFL;
                    }
            }
            else
            {
               not_enough_errors++;
            }
            if (event_action != 0)
            {
               if (reason_str == NULL)
               {
                  if (flag_changed == YES)
                  {
                     event_log(0L, EC_DIR, ET_MAN, event_action, "%s%c%s",
                               dir_alias[i], SEPARATOR_CHAR, user);
                  }
               }
               else
               {
                  if ((flag_changed == YES) ||
                      (event_action != EA_UNSET_ACK_OFFL))
                  {
                     event_log(0L, EC_DIR, ET_MAN, event_action, "%s%c%s%c%s",
                               dir_alias[i], SEPARATOR_CHAR, user,
                               SEPARATOR_CHAR, reason_str);
                  }
               }
            }
         }
      }
   } /* for (i = 0; i < no_of_alias; i++) */

   if (flags_changed > 0)
   {
      if (acknowledge_type == UNSET_SELECT)
      {
         if (flags_unchangeable > 0)
         {
            (void)xrec(INFO_DIALOG,
                       "Unset acknowledge/offline for %d instances, %d already unset.",
                       flags_changed, flags_unchangeable);
         }
         else
         {
            (void)xrec(INFO_DIALOG,
                       "Unset acknowledge/offline for %d instances.",
                       flags_changed);
         }
      }
      else
      {
         if (flags_unchangeable > 0)
         {
            if (not_enough_errors > 0)
            {
               (void)xrec(INFO_DIALOG,
                          "Set acknowledge/offline for %d instances, %d already set. For %d there are not enough errors.",
                          flags_changed, flags_unchangeable, not_enough_errors);
            }
            else
            {
               (void)xrec(INFO_DIALOG,
                          "Set acknowledge/offline for %d instances, %d already set.",
                          flags_changed, flags_unchangeable);
            }
         }
         else
         {
            if (not_enough_errors > 0)
            {
               (void)xrec(INFO_DIALOG,
                          "Set acknowledge/offline for %d instances. For %d there are not enough errors.",
                          flags_changed, not_enough_errors);
            }
            else
            {
               (void)xrec(INFO_DIALOG,
                          "Set acknowledge/offline for %d instances.",
                          flags_changed);
            }
         }
      }
   }
   else if (flags_unchangeable > 0)
        {
           if (acknowledge_type == UNSET_SELECT)
           {
              (void)xrec(INFO_DIALOG,
                         "Acknowledge/offline for %d instances already unset.",
                         flags_unchangeable);
           }
           else
           {
              if (not_enough_errors > 0)
              {
                 (void)xrec(INFO_DIALOG,
                            "Acknowledge/offline for %d instances already set. For %d there are not enough errors.",
                            flags_unchangeable, not_enough_errors);
              }
              else
              {
                 if (reason_str == NULL)
                 {
                    (void)xrec(INFO_DIALOG,
                               "Acknowledge/offline for %d instances already set.",
                               flags_unchangeable);
                 }
              }
           }
        }
   else if (not_enough_errors > 0)
        {
           (void)xrec(INFO_DIALOG,
                      "Not enough errors for %d instances.", not_enough_errors);
        }
   else if ((reason_str == NULL) || (acknowledge_type == UNSET_SELECT))
        {
           (void)xrec(INFO_DIALOG, "No changes.");
        }

   return;
}


/*########################### radio_button() ############################*/
void
radio_button(Widget w, XtPointer client_data, XtPointer call_data)
{
#if SIZEOF_LONG == 4
   acknowledge_type = (int)client_data;
#else
   union intlong
         {
            int  ival[2];
            long lval;
         } il;
   int   byte_order = 1;

   il.lval = (long)client_data;
   if (*(char *)&byte_order == 1)
   {
      acknowledge_type = il.ival[0];
   }
   else
   {
      acknowledge_type = il.ival[1];
   }
#endif

   return;
}