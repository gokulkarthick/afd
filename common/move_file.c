/*
 *  move_file.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1995 - 2001 Deutscher Wetterdienst (DWD),
 *                            Holger Kiehl <Holger.Kiehl@dwd.de>
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
 **   move_file - moves or copies and deletes a file from one location
 **               to another
 **
 ** SYNOPSIS
 **   int move_file(char *from, char *to)
 **
 ** DESCRIPTION
 **   The move_file() function tries to move file 'from' to file 'to'.
 **   If this fails it will try to copy the file with copy_file().
 **
 ** RETURN VALUES
 **   SUCCESS   - when moving/copying file succesfully
 **   INCORRECT - when failing to move/copy the file 'to'
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   05.10.1995 H.Kiehl Created
 **
 */
DESCR__E_M3

#include <stdio.h>      /* rename()                                      */
#include <string.h>     /* strerror()                                    */
#include <unistd.h>     /* unlink()                                      */
#include <errno.h>


/*############################ move_file() ##############################*/
int
move_file(char *from, char *to)
{
   if (rename(from, to) < 0)
   {
      if (errno == EXDEV)
      {
         /* Reset errno or else we might get this error */
         /* in a readdir() loop where we check if errno */
         /* is not zero.                                */
         errno = 0;

         /* If renaming fails we assume it is because the file is */
         /* not in the same file system. Thus we have to copy it. */
         if (copy_file(from, to) < 0)
         {
            system_log(ERROR_SIGN, __FILE__, __LINE__,
                       "Failed to copy %s to %s", from, to);
            return(INCORRECT);
         }

         /* Remove the source file */
         if (unlink(from) < 0)
         {
            /* Include some better error correction here. Since */
            /* if we do not delete the file successful we will  */
            /* have a continuous loop (when it is an instant    */
            /* job) trying to copy a file we cannot delete.     */
            system_log(ERROR_SIGN, __FILE__, __LINE__,
                       "Could not delete file %s : %s", from, strerror(errno));

            return(2);
         }
      }
      else
      {
         system_log(ERROR_SIGN, __FILE__, __LINE__,
                    "Error when renaming %s to %s : %s",
                    from, to, strerror(errno));
         return(INCORRECT);
      }
   }

   return(SUCCESS);
}
