/*
 *  dir_spy.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1998 - 2004 Deutscher Wetterdienst (DWD),
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

DESCR__S_M1
/*
 ** NAME
 **   dir_spy - shows all directory names currently stored
 **
 ** SYNOPSIS
 **   dir_spy [-w <AFD work dir>] [--version]
 **
 ** DESCRIPTION
 **
 ** RETURN VALUES
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   03.02.1998 H.Kiehl Created
 **   05.01.2004 H.Kiehl Also show the original directory name as stored
 **                      in DIR_CONFIG.
 **
 */
DESCR__E_M1

#include <stdio.h>
#include <string.h>
#include <stdlib.h>                 /* exit()                            */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>                  /* open()                            */
#include <unistd.h>                 /* fstat(), lseek(), write()         */
#ifdef HAVE_MMAP
#include <sys/mman.h>               /* mmap()                            */
#endif
#include <errno.h>
#include "version.h"

/* Global variables */
int        sys_log_fd = STDERR_FILENO;
char       *p_work_dir = NULL;
const char *sys_log_name = SYSTEM_LOG_FIFO;


/*$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ main() $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$*/
int
main(int argc, char *argv[])
{
   int                 fd,
                       i,
                       *no_of_dir_names;
   char                file[MAX_PATH_LENGTH],
                       *ptr,
                       work_dir[MAX_PATH_LENGTH];
   struct stat         stat_buf;
   struct dir_name_buf *dnb;

   CHECK_FOR_VERSION(argc, argv);

   /* First get working directory for the AFD */
   if (get_afd_path(&argc, argv, work_dir) < 0) 
   {
      exit(INCORRECT);
   }

   (void)sprintf(file, "%s%s%s", work_dir, FIFO_DIR, DIR_NAME_FILE);
   if ((fd = open(file, O_RDONLY)) == -1)
   {
      (void)fprintf(stderr, "Failed to open() `%s' : %s (%s %d)\n",
                    file, strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }

   if (fstat(fd, &stat_buf) == -1)
   {
      (void)fprintf(stderr, "Failed to fstat() `%s' : %s (%s %d)\n",
                    file, strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }

#ifdef HAVE_MMAP
   if ((ptr = mmap(0, stat_buf.st_size, PROT_READ,
                   MAP_SHARED, fd, 0)) == (caddr_t)-1)
#else
   if ((ptr = mmap_emu(0, stat_buf.st_size, PROT_READ,
                       MAP_SHARED, file, 0)) == (caddr_t)-1)
#endif
   {
      (void)fprintf(stderr, "Failed to mmap() `%s' : %s (%s %d)\n",
                    file, strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }
   if (close(fd) == -1)
   {
      (void)fprintf(stderr, "Failed to close() `%s' : %s (%s %d)\n",
                    file, strerror(errno), __FILE__, __LINE__);
   }
   no_of_dir_names = (int *)ptr;
   ptr += AFD_WORD_OFFSET;
   dnb = (struct dir_name_buf *)ptr;

   if (*no_of_dir_names > 0)
   {
      (void)fprintf(stdout, "No of directories : %d\n", *no_of_dir_names);
      (void)fprintf(stdout, "Pos   Dir-ID     Dir-name [| Original name]\n");
      for (i = 0; i < *no_of_dir_names; i++)
      {
         if (CHECK_STRCMP(dnb[i].dir_name, dnb[i].orig_dir_name) == 0)
         {
            (void)fprintf(stdout, "%-5d %-10x %s\n",
                          i, dnb[i].dir_id, dnb[i].dir_name);
         }
         else
         {
            (void)fprintf(stdout, "%-5d %-10x %s | %s\n",
                          i, dnb[i].dir_id, dnb[i].dir_name,
                          dnb[i].orig_dir_name);
         }
      }
   }
   else
   {
      (void)fprintf(stdout, "No directories.\n");
   }

   ptr -= AFD_WORD_OFFSET;
#ifdef HAVE_MMAP
   if (munmap(ptr, stat_buf.st_size) == -1)
#else
   if (munmap_emu(ptr) == -1)
#endif
   {
      (void)fprintf(stderr, "Failed to munmap() `%s' : %s (%s %d)\n",
                    file, strerror(errno), __FILE__, __LINE__);
   }

   exit(INCORRECT);
}
