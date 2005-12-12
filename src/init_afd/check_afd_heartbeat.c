/*
 *  check_afd_heartbeat.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 2002 - 2005 Holger Kiehl <Holger.Kiehl@dwd.de>
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
 **   check_afd_heartbeat - Checks if the heartbeat of process init_afd
 **                         is still working.
 **
 ** SYNOPSIS
 **   int check_afd_heartbeat(long wait_time, int remove_process)
 **
 ** DESCRIPTION
 **   This function checks if the heartbeat of init_afd is still
 **   going.
 **
 ** RETURN VALUES
 **   Returns 1 if another AFD is active, otherwise it returns 0.
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   16.06.2002 H.Kiehl Created
 **
 */
DESCR__E_M3

#include <stdio.h>               /* NULL                                 */
#include <string.h>              /* strerror()                           */
#include <stdlib.h>              /* malloc(), free()                     */
#include <sys/types.h>
#ifdef HAVE_MMAP
#include <sys/mman.h>            /* mmap(), munmap()                     */
#endif
#include <sys/stat.h>
#include <signal.h>              /* kill()                               */
#include <unistd.h>              /* sleep(), unlink()                    */
#include <fcntl.h>               /* O_RDWR                               */
#include <errno.h>

/* External global variables */
extern char afd_active_file[];

/* Local functions */
static void kill_jobs(off_t);


/*######################## check_afd_heartbeat() ########################*/
int
check_afd_heartbeat(long wait_time, int remove_process)
{
   int         afd_active = 0;
   struct stat stat_buf;

   if ((stat(afd_active_file, &stat_buf) == 0) &&
       (stat_buf.st_size > (((NO_OF_PROCESS + 1) * sizeof(pid_t)) + sizeof(unsigned int))))
   {
      int afd_active_fd;

      if ((afd_active_fd = open(afd_active_file, O_RDWR)) == -1)
      {
         system_log(ERROR_SIGN, __FILE__, __LINE__, "Failed to open() %s : %s",
                    afd_active_file, strerror(errno));
      }
      else
      {
         char *ptr;

#ifdef HAVE_MMAP
         if ((ptr = mmap(0, stat_buf.st_size, (PROT_READ | PROT_WRITE),
                         MAP_SHARED, afd_active_fd, 0)) == (caddr_t) -1)
#else
         if ((ptr = mmap_emu(0, stat_buf.st_size,
                             (PROT_READ | PROT_WRITE),
                             MAP_SHARED, afd_active_file, 0)) == (caddr_t) -1) 
#endif
         {
            system_log(ERROR_SIGN, __FILE__, __LINE__,
                       "Failed to mmap() to %s : %s",
                       afd_active_file, strerror(errno));
         }
         else
         {
            unsigned int current_value,
                         *heartbeat;
            long         elapsed_time = 0L;

            heartbeat = (unsigned int *)(ptr + ((NO_OF_PROCESS + 1) * sizeof(pid_t)));
            current_value = *heartbeat;

            wait_time = wait_time * 1000000L;
            while (elapsed_time < wait_time)
            {
               if (current_value != *heartbeat)
               {
                  afd_active = 1;
                  break;
               }
               my_usleep(100000L);
               elapsed_time += 100000L;
            }
#ifdef HAVE_MMAP
            if (munmap(ptr, stat_buf.st_size) == -1)
            {
               system_log(ERROR_SIGN, __FILE__, __LINE__,
                          "Failed to munmap() from %s : %s",
                          afd_active_file, strerror(errno));
            }
#else
            if (munmap_emu(ptr) == -1)
            {
               system_log(ERROR_SIGN, __FILE__, __LINE__,
                          "Failed to munmap_emu() from %s", afd_active_file);
            }
#endif
            if ((afd_active == 0) && (remove_process == YES))
            {
               kill_jobs(stat_buf.st_size);
            }
         }
         if (close(afd_active_fd) == -1)
         {
            system_log(DEBUG_SIGN, __FILE__, __LINE__,
                       "Failed to close() %s : %s",
                       afd_active_file, strerror(errno));
         }
      }
   }

   return(afd_active);
}



/*++++++++++++++++++++++++++++ kill_jobs() ++++++++++++++++++++++++++++++*/
static void
kill_jobs(off_t st_size)
{
   char *ptr,
        *buffer;
   int  i,
        process_left,
        read_fd;

   if ((read_fd = open(afd_active_file, O_RDWR)) < 0)
   {
      system_log(FATAL_SIGN, __FILE__, __LINE__,
                 "Failed to open %s : %s", afd_active_file, strerror(errno));
      exit(-10);
   }

   if ((buffer = malloc(st_size)) == NULL)
   {
      system_log(FATAL_SIGN, __FILE__, __LINE__,
                 "malloc() error : %s", strerror(errno));
      exit(-11);
   }

   if (read(read_fd, buffer, st_size) != st_size)
   {
      system_log(FATAL_SIGN, __FILE__, __LINE__,
                 "read() error : %s", strerror(errno));
      exit(-12);
   }

   /* Try to send kill signal to all running process. */
   ptr = buffer;
   for (i = 0; i <= NO_OF_PROCESS; i++)
   {
      if (*(pid_t *)ptr > 0)
      {
         (void)kill(*(pid_t *)ptr, SIGINT);
      }
      ptr += sizeof(pid_t);
   }
   (void)sleep(1);

   /* Check if they are all gone. */
   process_left = 0;
   ptr = buffer;
   for (i = 0; i <= NO_OF_PROCESS; i++)
   {
      if (*(pid_t *)ptr > 0)
      {
         if (kill(*(pid_t *)ptr, 0) == 0)
         {
            process_left++;
         }
      }
      ptr += sizeof(pid_t);
   }
   if (process_left > 0)
   {
      /* Lets wait a little longer. */
      (void)sleep(12L);

      /* Now kill am the hard way. */
      ptr = buffer;
      for (i = 0; i <= NO_OF_PROCESS; i++)
      {
         if (*(pid_t *)ptr > 0)
         {
            (void)kill(*(pid_t *)ptr, SIGKILL);
         }
         ptr += sizeof(pid_t);
      }
   }

   if (close(read_fd) == -1)
   {
      system_log(DEBUG_SIGN, __FILE__, __LINE__,
                 "close() error : %s", strerror(errno));
   }
   free(buffer);

   (void)unlink(afd_active_file);

   return;
}