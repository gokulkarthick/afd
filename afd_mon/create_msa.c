/*
 *  create_msa.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1998 - 2000 Holger Kiehl <Holger.Kiehl@dwd.de>
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
 **   create_msa - creates the MSA of the AFD_MON
 **
 ** SYNOPSIS
 **   void create_msa(void)
 **
 ** DESCRIPTION
 **   This function creates the MSA (Monitor Status Area), to
 **   which all monitor process will map. The MSA has the following
 **   structure:
 **
 **      <int no_of_hosts><struct mon_status_area msa[no_of_afds]>
 **
 **   A detailed description of the structure mon_status_area can
 **   be found in mondefs.h. The signed integer variable no_of_afds
 **   contains the number of AFD's that are to be monitored. This
 **   variable can have the value STALE (-1), which will tell all other
 **   process to unmap from this area and map to the new area.
 **
 ** RETURN VALUES
 **   None. Will exit with incorrect if any of the system call will
 **   fail.
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   29.08.1998 H.Kiehl Created
 **   13.09.2000 H.Kiehl Addition of top number of process.
 **
 */
DESCR__E_M3

#include <stdio.h>
#include <string.h>                 /* strlen(), strcmp(), strcpy(),     */
                                    /* strerror()                        */
#include <stdlib.h>                 /* calloc(), free()                  */
#include <time.h>                   /* time()                            */
#include <sys/types.h>
#include <sys/stat.h>
#ifndef _NO_MMAP
#include <sys/mman.h>               /* mmap(), munmap()                  */
#endif
#include <unistd.h>                 /* read(), write(), close(), lseek() */
#include <errno.h>
#include <fcntl.h>
#include <errno.h>
#include "mondefs.h"


/* External global variables */
extern char                   *p_work_dir;
extern int                    msa_fd,
                              msa_id,
                              no_of_afds,  /* The number of remote/  */
                                           /* local AFD's to be      */
                                           /* monitored.             */
                              sys_log_fd;
extern off_t                  msa_size;
extern struct mon_status_area *msa;


/*############################ create_msa() #############################*/
void
create_msa(void)
{
   int                    i,
                          k,
                          fd,
                          old_msa_fd = -1,
                          old_msa_id,
                          old_no_of_afds = -1;
   off_t                  old_msa_size = -1;
   char                   *ptr = NULL,
                          new_msa_stat[MAX_PATH_LENGTH],
                          old_msa_stat[MAX_PATH_LENGTH],
                          msa_id_file[MAX_PATH_LENGTH];
   struct mon_status_area *old_msa = NULL;
   struct mon_list        *ml = NULL;
   struct flock           wlock = {F_WRLCK, SEEK_SET, 0, 1},
                          ulock = {F_UNLCK, SEEK_SET, 0, 1};
   struct stat            stat_buf;

   msa_size = -1;

   /* Read AFD_MON_DB file. */
   eval_afd_mon_db(&ml);

   /* Initialise all pathnames and file descriptors */
   (void)strcpy(msa_id_file, p_work_dir);
   (void)strcat(msa_id_file, FIFO_DIR);
   (void)strcpy(old_msa_stat, msa_id_file);
   (void)strcat(old_msa_stat, MON_STATUS_FILE);
   (void)strcat(msa_id_file, MSA_ID_FILE);

   /*
    * First just try open the msa_id_file. If this fails create
    * the file and initialise old_msa_id with -1.
    */
   if ((fd = open(msa_id_file, O_RDWR)) > -1)
   {
      /*
       * Lock MSA ID file.
       */
      if (fcntl(fd, F_SETLKW, &wlock) < 0)
      {
         /* Is lock already set or are we setting it again? */
         if ((errno != EACCES) && (errno != EAGAIN))
         {
            (void)rec(sys_log_fd, FATAL_SIGN,
                      "Could not set write lock for %s : %s (%s %d)\n",
                      msa_id_file, strerror(errno), __FILE__, __LINE__);
            exit(INCORRECT);
         }
      }

      /* Read the MSA file ID */
      if (read(fd, &old_msa_id, sizeof(int)) < 0)
      {
         (void)rec(sys_log_fd, FATAL_SIGN,
                   "Could not read the value of the MSA file ID : %s (%s %d)\n",
                   strerror(errno), __FILE__, __LINE__);
         exit(INCORRECT);
      }
   }
   else
   {
      if ((fd = open(msa_id_file, (O_RDWR | O_CREAT | O_TRUNC),
                     (S_IRUSR | S_IWUSR))) < 0)
      {
         (void)rec(sys_log_fd, FATAL_SIGN,
                   "Could not open %s : %s (%s %d)\n",
                   msa_id_file, strerror(errno), __FILE__, __LINE__);
         exit(INCORRECT);
      }
      old_msa_id = -1;
   }

   /*
    * Mark memory mapped region as old, so no process puts
    * any new information into the region after we
    * have copied it into the new region.
    */
   if (old_msa_id > -1)
   {
      /* Attach to old region */
      ptr = old_msa_stat + strlen(old_msa_stat);
      (void)sprintf(ptr, ".%d", old_msa_id);

      /* Get the size of the old MSA file. */
      if (stat(old_msa_stat, &stat_buf) < 0)
      {
         (void)rec(sys_log_fd, ERROR_SIGN,
                   "Failed to stat() %s : %s (%s %d)\n",
                   old_msa_stat, strerror(errno), __FILE__, __LINE__);
         old_msa_id = -1;
      }
      else
      {
         if (stat_buf.st_size > 0)
         {
            if ((old_msa_fd = open(old_msa_stat, O_RDWR)) < 0)
            {
               (void)rec(sys_log_fd, ERROR_SIGN,
                         "Failed to open() %s : %s (%s %d)\n",
                         old_msa_stat, strerror(errno), __FILE__, __LINE__);
               old_msa_id = old_msa_fd = -1;
            }
            else
            {
#ifdef _NO_MMAP
               if ((ptr = mmap_emu(0, stat_buf.st_size,
                                   (PROT_READ | PROT_WRITE),
                                   MAP_SHARED, old_msa_stat, 0)) == (caddr_t) -1)
#else
               if ((ptr = mmap(0, stat_buf.st_size, (PROT_READ | PROT_WRITE),
                               MAP_SHARED, old_msa_fd, 0)) == (caddr_t) -1)
#endif
               {
                  (void)rec(sys_log_fd, ERROR_SIGN,
                            "mmap() error : %s (%s %d)\n",
                            strerror(errno), __FILE__, __LINE__);
                  old_msa_id = -1;
               }
               else
               {
                  if (*(int *)ptr == STALE)
                  {
                     (void)rec(sys_log_fd, WARN_SIGN,
                               "MSA in %s is stale! Ignoring this MSA. (%s %d)\n",
                               old_msa_stat, __FILE__, __LINE__);
                     old_msa_id = -1;
                  }
                  else if (stat_buf.st_size != (AFD_WORD_OFFSET + (*(int *)ptr * sizeof(struct mon_status_area))))
                       {
                          (void)rec(sys_log_fd, WARN_SIGN,
                                    "Size of the MSA is not what it should be. Assuming that the structure has changed and will thus not use the old one. (%s %d)\n",
                                    __FILE__, __LINE__);
                          old_msa_id = -1;
#ifdef _NO_MMAP
                          if (munmap_emu(ptr) == -1)
#else
                          if (munmap(ptr, stat_buf.st_size) == -1)
#endif
                          {
                             (void)rec(sys_log_fd, ERROR_SIGN,
                                       "Failed to munmap() %s : %s (%s %d)\n",
                                       old_msa_stat, strerror(errno), __FILE__, __LINE__);
                          }
                       }
                       else
                       {
                          old_msa_size = stat_buf.st_size;
                       }

                  /*
                   * We actually could remove the old file now. Better
                   * do it when we are done with it.
                   */
               }

               /*
                * Do NOT close the old file! Else some file system
                * optimisers (like fsr in Irix 5.x) move the contents
                * of the memory mapped file!
                */
            }
         }
         else
         {
            old_msa_id = -1;
         }
      }

      if (old_msa_id != -1)
      {
         old_no_of_afds = *(int *)ptr;

         /* Now mark it as stale */
         *(int *)ptr = STALE;

         /* Move pointer to correct position so */
         /* we can extract the relevant data.   */
         ptr += AFD_WORD_OFFSET;

         old_msa = (struct mon_status_area *)ptr;
      }
   }

   /*
    * Create the new mmap region.
    */
   /* First calculate the new size */
   msa_size = AFD_WORD_OFFSET +
              (no_of_afds * sizeof(struct mon_status_area));

   if ((old_msa_id + 1) > -1)
   {
      msa_id = old_msa_id + 1;
   }
   else
   {
      msa_id = 0;
   }
   (void)sprintf(new_msa_stat, "%s%s%s.%d",
                 p_work_dir, FIFO_DIR, MON_STATUS_FILE, msa_id);

   /* Now map the new MSA region to a file */
   if ((msa_fd = open(new_msa_stat, (O_RDWR | O_CREAT | O_TRUNC),
                      FILE_MODE)) == -1)
   {
      (void)rec(sys_log_fd, FATAL_SIGN,
                "Failed to open() %s : %s (%s %d)\n",
                new_msa_stat, strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }

   if (lseek(msa_fd, msa_size - 1, SEEK_SET) == -1)
   {
      (void)rec(sys_log_fd, FATAL_SIGN,
                "Failed to lseek() in %s : %s (%s %d)\n",
                new_msa_stat, strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }
   if (write(msa_fd, "", 1) != 1)
   {
      (void)rec(sys_log_fd, FATAL_SIGN, "write() error : %s (%s %d)\n",
                strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }
#ifdef _NO_MMAP
   if ((ptr = mmap_emu(0, msa_size, (PROT_READ | PROT_WRITE), MAP_SHARED,
                       new_msa_stat, 0)) == (caddr_t) -1)
#else
   if ((ptr = mmap(0, msa_size, (PROT_READ | PROT_WRITE), MAP_SHARED,
                   msa_fd, 0)) == (caddr_t) -1)
#endif
   {
      (void)rec(sys_log_fd, FATAL_SIGN, "mmap() error : %s (%s %d)\n",
                strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }

   /* Write number of AFD's to new memory mapped region */
   *(int*)ptr = no_of_afds;

   /* Reposition msa pointer after no_of_afds */
   ptr += AFD_WORD_OFFSET;
   msa = (struct mon_status_area *)ptr;

   /*
    * Copy all the old and new data into the new mapped region.
    */
   if (old_msa_id < 0)
   {
      /*
       * There is NO old MSA.
       */
      for (i = 0; i < no_of_afds; i++)
      {
         (void)strcpy(msa[i].afd_alias, ml[i].afd_alias);
         (void)strcpy(msa[i].hostname, ml[i].hostname);
         (void)strcpy(msa[i].convert_username[0], ml[i].convert_username[0]);
         (void)strcpy(msa[i].convert_username[1], ml[i].convert_username[1]);
         (void)memset(msa[i].log_history, DEFAULT_BG,
                      (NO_OF_LOG_HISTORY * MAX_LOG_HISTORY));
         msa[i].r_work_dir[0]      = '\0';
         msa[i].afd_version[0]     = '\0';
         msa[i].poll_interval      = ml[i].poll_interval;
         msa[i].port               = ml[i].port;
         msa[i].amg                = 0;
         msa[i].fd                 = 0;
         msa[i].archive_watch      = 0;
         msa[i].jobs_in_queue      = 0;
         msa[i].no_of_transfers    = 0;
         msa[i].top_not_time       = 0L;
         (void)memset(msa[i].top_no_of_transfers, 0, (STORAGE_TIME * sizeof(int)));
         msa[i].max_connections    = MAX_DEFAULT_CONNECTIONS;
         msa[i].sys_log_ec         = 0;
         (void)memset(msa[i].sys_log_fifo, 0, LOG_FIFO_SIZE);
         msa[i].host_error_counter = 0;
         msa[i].no_of_hosts        = 0;
         msa[i].fc                 = 0;
         msa[i].fs                 = 0;
         msa[i].tr                 = 0;
         (void)memset(msa[i].top_tr, 0, (STORAGE_TIME * sizeof(unsigned int)));
         msa[i].top_tr_time        = 0L;
         msa[i].fr                 = 0;
         (void)memset(msa[i].top_fr, 0, (STORAGE_TIME * sizeof(unsigned int)));
         msa[i].top_fr_time        = 0L;
         msa[i].ec                 = 0;
         msa[i].last_data_time     = 0;
         msa[i].connect_status     = DISCONNECTED;
      } /* for (i = 0; i < no_of_afds; i++) */
   }
   else /* There is an old database file. */
   {
      int  afd_pos,
           no_of_gotchas = 0;
      char *gotcha = NULL;

      /*
       * The gotcha array is used to find AFD's that are in the
       * old MSA but not in the AFD_MON_CONFIG file.
       */
      if ((gotcha = malloc(old_no_of_afds)) == NULL)
      {
         (void)rec(sys_log_fd, FATAL_SIGN, "malloc() error : %s (%s %d)\n",
                   strerror(errno), __FILE__, __LINE__);
         exit(INCORRECT);
      }
      (void)memset(gotcha, NO, old_no_of_afds);

      for (i = 0; i < no_of_afds; i++)
      {
         (void)strcpy(msa[i].afd_alias, ml[i].afd_alias);
         (void)strcpy(msa[i].hostname, ml[i].hostname);
         (void)strcpy(msa[i].convert_username[0], ml[i].convert_username[0]);
         (void)strcpy(msa[i].convert_username[1], ml[i].convert_username[1]);
         msa[i].r_work_dir[0]  = '\0';
         msa[i].afd_version[0] = '\0';
         msa[i].poll_interval  = ml[i].poll_interval;
         msa[i].port           = ml[i].port;

         /*
          * Search in the old MSA for this AFD. If it is there use
          * the values from the old MSA or else initialise them with
          * defaults. When we find an old entry, remember this so we
          * can later check if there are entries in the old MSA but
          * there are no corresponding entries in the AFD_MON_CONFIG.
          * This will then have to be updated in the AFD_MON_CONFIG file.
          */
         afd_pos = INCORRECT;
         for (k = 0; k < old_no_of_afds; k++)
         {
            if (gotcha[k] != YES)
            {
               if (strcmp(old_msa[k].afd_alias, ml[i].afd_alias) == 0)
               {
                  afd_pos = k;
                  break;
               }
            }
         }

         if (afd_pos != INCORRECT)
         {
            no_of_gotchas++;
            gotcha[afd_pos] = YES;

            (void)strcpy(msa[i].r_work_dir, old_msa[afd_pos].r_work_dir);
            (void)strcpy(msa[i].afd_version, old_msa[afd_pos].afd_version);
            (void)memcpy(msa[i].log_history, old_msa[afd_pos].log_history,
                         (NO_OF_LOG_HISTORY * MAX_LOG_HISTORY));
            msa[i].amg                = old_msa[afd_pos].amg;
            msa[i].fd                 = old_msa[afd_pos].fd;
            msa[i].archive_watch      = old_msa[afd_pos].archive_watch;
            msa[i].jobs_in_queue      = old_msa[afd_pos].jobs_in_queue;
            msa[i].no_of_transfers    = old_msa[afd_pos].no_of_transfers;
            msa[i].top_not_time       = old_msa[afd_pos].top_not_time;
            (void)memcpy(msa[i].top_no_of_transfers,
                         old_msa[afd_pos].top_no_of_transfers,
                         (STORAGE_TIME * sizeof(int)));
            msa[i].sys_log_ec         = old_msa[afd_pos].sys_log_ec;
            (void)memcpy(msa[i].sys_log_fifo, old_msa[afd_pos].sys_log_fifo,
                         LOG_FIFO_SIZE);
            msa[i].host_error_counter = old_msa[afd_pos].host_error_counter;
            msa[i].no_of_hosts        = old_msa[afd_pos].no_of_hosts;
            msa[i].max_connections    = old_msa[afd_pos].max_connections;
            msa[i].fc                 = old_msa[afd_pos].fc;
            msa[i].fs                 = old_msa[afd_pos].fs;
            msa[i].tr                 = old_msa[afd_pos].tr;
            msa[i].top_tr_time        = old_msa[afd_pos].top_tr_time;
            (void)memcpy(msa[i].top_tr, old_msa[afd_pos].top_tr,
                         (STORAGE_TIME * sizeof(unsigned int)));
            msa[i].fr                 = old_msa[afd_pos].fr;
            msa[i].top_fr_time        = old_msa[afd_pos].top_fr_time;
            (void)memcpy(msa[i].top_fr, old_msa[afd_pos].top_fr,
                         (STORAGE_TIME * sizeof(unsigned int)));
            msa[i].ec                 = old_msa[afd_pos].ec;
            msa[i].last_data_time     = old_msa[afd_pos].last_data_time;
            msa[i].connect_status     = old_msa[afd_pos].connect_status;
         }
         else /* This AFD is not in the old MSA, therefor it is new. */
         {
            (void)strcpy(msa[i].afd_alias, ml[i].afd_alias);
            (void)strcpy(msa[i].hostname, ml[i].hostname);
            (void)strcpy(msa[i].convert_username[0], ml[i].convert_username[0]);
            (void)strcpy(msa[i].convert_username[1], ml[i].convert_username[1]);
            (void)memset(msa[i].log_history, DEFAULT_BG,
                         (NO_OF_LOG_HISTORY * MAX_LOG_HISTORY));
            msa[i].r_work_dir[0]      = '\0';
            msa[i].afd_version[0]     = '\0';
            msa[i].poll_interval      = ml[i].poll_interval;
            msa[i].port               = ml[i].port;
            msa[i].amg                = 0;
            msa[i].fd                 = 0;
            msa[i].archive_watch      = 0;
            msa[i].jobs_in_queue      = 0;
            msa[i].no_of_transfers    = 0;
            msa[i].top_not_time       = 0L;
            (void)memset(msa[i].top_no_of_transfers, 0,
                         (STORAGE_TIME * sizeof(int)));
            msa[i].max_connections    = MAX_DEFAULT_CONNECTIONS;
            msa[i].sys_log_ec         = 0;
            (void)memset(msa[i].sys_log_fifo, 0, LOG_FIFO_SIZE);
            msa[i].host_error_counter = 0;
            msa[i].no_of_hosts        = 0;
            msa[i].fc                 = 0;
            msa[i].fs                 = 0;
            msa[i].tr                 = 0;
            msa[i].top_tr_time        = 0L;
            (void)memset(msa[i].top_tr, 0,
                         (STORAGE_TIME * sizeof(unsigned int)));
            msa[i].fr                 = 0;
            msa[i].top_fr_time        = 0L;
            (void)memset(msa[i].top_fr, 0,
                         (STORAGE_TIME * sizeof(unsigned int)));
            msa[i].ec                 = 0;
            msa[i].last_data_time     = 0;
            msa[i].connect_status     = DISCONNECTED;
         }
      } /* for (i = 0; i < no_of_afds; i++) */
   }

   /* Reposition msa pointer after no_of_afds */
   ptr = (char *)msa;
   ptr -= AFD_WORD_OFFSET;
   if (msa_size > 0)
   {
#ifdef _NO_MMAP
      if (msync_emu(ptr) == -1)
      {
         (void)rec(sys_log_fd, ERROR_SIGN, "msync_emu() error (%s %d)\n",
                   __FILE__, __LINE__);
      }
      if (munmap_emu(ptr) == -1)
#else
      if (munmap(ptr, msa_size) == -1)
#endif
      {
         (void)rec(sys_log_fd, ERROR_SIGN,
                   "Failed to munmap() %s : %s (%s %d)\n",
                   new_msa_stat, strerror(errno), __FILE__, __LINE__);
      }
   }

   /*
    * Unmap from old memory mapped region.
    */
   if (old_msa != NULL)
   {
      ptr = (char *)old_msa;
      ptr -= AFD_WORD_OFFSET;

      /* Don't forget to unmap old MSA file. */
      if (old_msa_size > 0)
      {
#ifdef _NO_MMAP
         if (munmap_emu(ptr) == -1)
#else
         if (munmap(ptr, old_msa_size) == -1)
#endif
         {
            (void)rec(sys_log_fd, ERROR_SIGN,
                      "Failed to munmap() %s : %s (%s %d)\n",
                      old_msa_stat, strerror(errno), __FILE__, __LINE__);
         }
         old_msa = NULL;
      }
   }

   /* Remove the old MSA file if there was one. */
   if (old_msa_size > -1)
   {
      if (remove(old_msa_stat) < 0)
      {
         (void)rec(sys_log_fd, WARN_SIGN,
                   "Failed to remove() %s : %s (%s %d)\n",
                   old_msa_stat, strerror(errno), __FILE__, __LINE__);
      }
   }

   /*
    * Copy the new msa_id into the locked MSA_ID_FILE file, unlock
    * and close the file.
    */
   /* Go to beginning in file */
   if (lseek(fd, 0, SEEK_SET) < 0)
   {
      (void)rec(sys_log_fd, ERROR_SIGN,
                "Could not seek() to beginning of %s : %s (%s %d)\n",
                msa_id_file, strerror(errno), __FILE__, __LINE__);
   }

   /* Write new value into MSA_ID_FILE file */
   if (write(fd, &msa_id, sizeof(int)) != sizeof(int))
   {
      (void)rec(sys_log_fd, FATAL_SIGN,
                "Could not write value to MSA ID file : %s (%s %d)\n",
                strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }

   /* Unlock file which holds the msa_id */
   if (fcntl(fd, F_SETLKW, &ulock) < 0)
   {
      (void)rec(sys_log_fd, FATAL_SIGN,
                "Could not unset write lock : %s (%s %d)\n",
                strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }

   /* Close the MSA ID file */
   if (close(fd) == -1)
   {
      (void)rec(sys_log_fd, DEBUG_SIGN, "close() error : %s (%s %d)\n",
                strerror(errno), __FILE__, __LINE__);
   }

   /* Close file with new MSA */
   if (close(msa_fd) == -1)
   {
      (void)rec(sys_log_fd, DEBUG_SIGN, "close() error : %s (%s %d)\n",
                strerror(errno), __FILE__, __LINE__);
   }
   msa_fd = -1;

   /* Close old MSA file */
   if (old_msa_fd != -1)
   {
      if (close(old_msa_fd) == -1)
      {
         (void)rec(sys_log_fd, DEBUG_SIGN, "close() error : %s (%s %d)\n",
                   strerror(errno), __FILE__, __LINE__);
      }
   }

   /* Free structure mon_list, it's no longer needed. */
   if (ml != NULL)
   {
      free(ml);
   }

   return;
}
