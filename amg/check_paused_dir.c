/*
 *  check_paused_dir.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1996 - 2001 Deutscher Wetterdienst (DWD),
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
 **   check_paused_dir - checks if there is a directory with files
 **                      for a specific host
 **
 ** SYNOPSIS
 **   char *check_paused_dir(struct directory_entry *p_de,
 **                          int                    *nfg,
 **                          int                    *dest_count)
 **
 ** DESCRIPTION
 **   This function checks the user directory for any paused
 **   directories. A paused directory consists of the hostname
 **   starting with a dot.
 **
 ** RETURN VALUES
 **   The first hostname it finds in this directory. If no host is
 **   found NULL is returned.
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   06.03.1996 H.Kiehl Created
 **   04.01.2001 H.Kiehl Don't check duplicate directories.
 **   29.05.2001 H.Kiehl Delete queue when host is disabled.
 **
 */
DESCR__E_M3

#include <stdio.h>                 /* NULL                               */
#include <sys/types.h>
#include <sys/stat.h>              /* stat(), S_ISREG()                  */
#include <unistd.h>
#include "amgdefs.h"

/* External global variables. */
extern int                        sys_log_fd;
extern struct instant_db          *db;
extern struct filetransfer_status *fsa;


/*######################### check_paused_dir() ##########################*/
char *
check_paused_dir(struct directory_entry *p_de,
                 int                    *nfg,
                 int                    *dest_count)
{
   register int i, j;

   for (i = *nfg; i < p_de->nfg; i++)
   {
      for (j = *dest_count; j < p_de->fme[i].dest_count; j++)
      {
         /* Is queue stopped? (ie PAUSE_QUEUE_STAT, AUTO_PAUSE_QUEUE_STAT */
         /* or AUTO_PAUSE_QUEUE_LOCK_STAT is set)                         */
         if (((fsa[db[p_de->fme[i].pos[j]].position].host_status < 2) ||
              ((fsa[db[p_de->fme[i].pos[j]].position].special_flag & HOST_DISABLED) &&
               (fsa[db[p_de->fme[i].pos[j]].position].host_status >= 2))) &&
             (db[p_de->fme[i].pos[j]].dup_paused_dir == NO))
         {
            struct stat stat_buf;

            if (stat(db[p_de->fme[i].pos[j]].paused_dir, &stat_buf) == 0)
            {
               if (S_ISDIR(stat_buf.st_mode))
               {
                  if (fsa[db[p_de->fme[i].pos[j]].position].special_flag & HOST_DISABLED)
                  {
                     if (remove_dir(db[p_de->fme[i].pos[j]].paused_dir) < 0)
                     {
                        (void)rec(sys_log_fd, WARN_SIGN,
                                  "Failed to remove %s (%s %d)\n",
                                  db[p_de->fme[i].pos[j]].paused_dir,
                                  __FILE__, __LINE__);
                     }
                     return(NULL);
                  }
                  else
                  {
                     *nfg = i;
                     *dest_count = j + 1;
                     return(db[p_de->fme[i].pos[j]].host_alias);
                  }
               }
            }
         }
      }
      *dest_count = 0;
   }

   return(NULL);
}
