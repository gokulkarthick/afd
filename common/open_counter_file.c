/*
 *  open_counter_file.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1996 - 2001 Holger Kiehl <Holger.Kiehl@dwd.de>
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
 **   open_counter_file - opens the AFD counter file
 **
 ** SYNOPSIS
 **   int open_counter_file(char *file_name)
 **
 ** DESCRIPTION
 **   This function simply opens the AFD counter files. This counter
 **   is used by the AFD to create unique file names.
 **
 ** RETURN VALUES
 **   Returns the file descriptor of the open file or INCORRECT when
 **   it fails to do so.
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   11.08.1996 H.Kiehl Created
 **   21.08.1998 H.Kiehl Allow to open different counter files and
 **                      if not available create and initialize it.
 **
 */
DESCR__E_M3

#include <string.h>           /* strcpy(), strcat(), strerror()          */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>           /* lseek(), write()                        */
#include <fcntl.h>
#include <errno.h>

/* External global variables */
extern char *p_work_dir;


/*########################### open_counter_file() #######################*/
int
open_counter_file(char *file_name)
{
   int  fd;
   char counter_file[MAX_PATH_LENGTH];

   (void)strcpy(counter_file, p_work_dir);
   (void)strcat(counter_file, FIFO_DIR);
   (void)strcat(counter_file, file_name);
   if ((fd = coe_open(counter_file, O_RDWR)) == -1)
   {
      if (errno == ENOENT)
      {
         if ((fd = coe_open(counter_file, O_RDWR | O_CREAT,
                            S_IRUSR | S_IWUSR)) == -1)
         {
            system_log(ERROR_SIGN, __FILE__, __LINE__,
                       "Could not open() %s : %s",
                       counter_file, strerror(errno));
         }
         else
         {
            int status = 0;

            /* Initialise counter file with zero */
            if (write(fd, &status, sizeof(int)) != sizeof(int))
            {
               system_log(FATAL_SIGN, __FILE__, __LINE__,
                          "Could not initialise %s : %s",
                          counter_file, strerror(errno));
               (void)close(fd);
            }
            else
            {
               /* Position descriptor to start of file. */
               if (lseek(fd, 0, SEEK_SET) == -1)
               {
                  system_log(FATAL_SIGN, __FILE__, __LINE__,
                             "Could not lseek() to start in %s : %s",
                             counter_file, strerror(errno));
                  (void)close(fd);
               }
               else
               {
                  return(fd);
               }
            }
         }
      }
      else
      {
         system_log(ERROR_SIGN, __FILE__, __LINE__,
                    "Could not open %s : %s", counter_file, strerror(errno));
      }
   }
   else
   {
      return(fd);
   }

   return(INCORRECT);
}
