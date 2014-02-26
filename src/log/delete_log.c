/*
 *  delete_log.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1998 - 2013 Holger Kiehl <Holger.Kiehl@dwd.de>
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
 **   delete_log - logs all file names deleted by the AFD.
 **
 ** SYNOPSIS
 **   delete_log [--version][-w <working directory>]
 **
 ** DESCRIPTION
 **   This function reads from the fifo DELETE_LOG_FIFO any file name
 **   that was deleted by any process of the AFD. The data in the fifo
 **   has the following structure:
 **       <FS><JID><DID><CT><SJC><UN><HN>\0<FNL><FN>\0<UPN>\0
 **         |   |    |    |   |    |   |     |    |     |
 **         |   |    |    |   |    |   |     |    |     +-> A \0 terminated string of
 **         |   |    |    |   |    |   |     |    |         the user or process that
 **         |   |    |    |   |    |   |     |    |         deleted the file.
 **         |   |    |    |   |    |   |     |    +-------> \0 terminated string of
 **         |   |    |    |   |    |   |     |              the File Name.
 **         |   |    |    |   |    |   |     +------------> Unsigned char holding the
 **         |   |    |    |   |    |   |                    File Name Length.
 **         |   |    |    |   |    |   +------------------> \0 terminated string of
 **         |   |    |    |   |    |                        the Host Name and reason.
 **         |   |    |    |   |    +----------------------> Unsigned int
 **         |   |    |    |   |                             for Unique Number.
 **         |   |    |    |   +---------------------------> Unsigned integer for
 **         |   |    |    |                                 Split Job Counter.
 **         |   |    |    +-------------------------------> Input time of
 **         |   |    |                                      type time_t.
 **         |   |    +------------------------------------> Unsigned integer holding
 **         |   |                                           the directory ID.
 **         |   +-----------------------------------------> Unsigned integer holding
 **         |                                               the job ID.
 **         +---------------------------------------------> File size of type off_t.
 **
 **   This data is then written to the delete log file in the following
 **   format:
 **
 **   426f52c4   btx      001|dat.txt|5eb7|697d0f61|3ab56ea2|426f44b4_23ed0_0|sf_ftp[|>10]
 **      |        |        |     |     |     |         |          |             |      |
 **      |        |     +--+  +--+  +--+  +--+    +----+    +-----+    +--------+ +----+
 **      |        |     |     |     |     |       |         |          |          |
 **   Deletion  Host Delete  File  File  Job Directory   Unique  User/process  Optional
 **    time     name reason  name  size   ID    ID         ID    that deleted additional
 **                                                                  file      reason
 **
 ** RETURN VALUES
 **   SUCCESS on normal exit and INCORRECT when an error has occurred.
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   14.01.1998 H.Kiehl Created
 **   07.01.2001 H.Kiehl Build in some checks when fifo buffer overflows.
 **   14.06.2001 H.Kiehl Removed the above unnecessary checks.
 **   13.04.2002 H.Kiehl Added SEPARATOR_CHAR.
 **   26.02.2008 H.Kiehl Have both job ID and directory ID and expand
 **                      delete reason to 3 characters.
 **   26.03.2008 H.Kiehl Added unique ID to simplify searching.
 **   29.01.2009 H.Kiehl Changed unique_number from unsigned short to
 **                      unsigned int.
 **
 */
DESCR__E_M1

#include <stdio.h>           /* fopen(), fflush()                        */
#include <string.h>          /* strcpy(), strcat(), strerror(), memcpy() */
#include <stdlib.h>          /* malloc()                                 */
#include <time.h>            /* time()                                   */
#include <sys/types.h>       /* fdset                                    */
#include <sys/stat.h>
#include <sys/time.h>        /* struct timeval                           */
#include <unistd.h>          /* fpathconf(), unlink()                    */
#include <fcntl.h>           /* O_RDWR, open()                           */
#include <signal.h>          /* signal()                                 */
#include <errno.h>
#include "logdefs.h"
#include "version.h"


/* External global variables. */
int        sys_log_fd = STDERR_FILENO;
char       *iobuf = NULL,
           *p_work_dir = NULL;
const char *sys_log_name = SYSTEM_LOG_FIFO;


/*$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ main() $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$*/
int
main(int argc, char *argv[])
{
#ifdef _DELETE_LOG
   FILE           *delete_file;
   int            bytes_buffered = 0,
                  log_number = 0,
                  n,
                  length,
                  max_delete_log_files = MAX_DELETE_LOG_FILES,
                  no_of_buffered_writes = 0,
                  check_size,
                  status,
                  log_fd;
#ifdef WITHOUT_FIFO_RW_SUPPORT
   int            writefd;
#endif
   off_t          *file_size;
   time_t         *input_time,
                  next_file_time,
                  now;
   unsigned int   *dir_id,
                  *job_id,
                  *split_job_counter,
                  *unique_number;
   long           fifo_size;
   char           *p_end,
                  *fifo_buffer,
                  *p_host_name,
                  *p_file_name,
                  *work_dir,
                  current_log_file[MAX_PATH_LENGTH],
                  log_file[MAX_PATH_LENGTH];
   unsigned char  *file_name_length;
   fd_set         rset;
   struct timeval timeout;
   struct stat    stat_buf;

   CHECK_FOR_VERSION(argc, argv);

   if (get_afd_path(&argc, argv, log_file) < 0)
   {
      exit(INCORRECT);
   }
   if ((work_dir = malloc((strlen(log_file) + 1))) == NULL)
   {
      system_log(ERROR_SIGN, __FILE__, __LINE__,
                 "Failed to malloc() memory : %s",
                 strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }
   (void)strcpy(work_dir, log_file);
   p_work_dir = work_dir;

   /* Create and open fifos that we need. */
   (void)strcat(log_file, FIFO_DIR);
   (void)strcat(log_file, DELETE_LOG_FIFO);
#ifdef WITHOUT_FIFO_RW_SUPPORT
   if (open_fifo_rw(log_file, &log_fd, &writefd) == -1)
#else
   if ((log_fd = open(log_file, O_RDWR)) == -1)
#endif
   {
      if (errno == ENOENT)
      {
         if (make_fifo(log_file) == SUCCESS)
         {
#ifdef WITHOUT_FIFO_RW_SUPPORT
            if (open_fifo_rw(log_file, &log_fd, &writefd) == -1)
#else
            if ((log_fd = open(log_file, O_RDWR)) == -1)
#endif
            {
               system_log(ERROR_SIGN, __FILE__, __LINE__,
                          "Failed to open() fifo %s : %s",
                          log_file, strerror(errno));
               exit(INCORRECT);
            }
         }
         else
         {
            system_log(ERROR_SIGN, __FILE__, __LINE__,
                       "Failed to create fifo %s.", log_file);
            exit(INCORRECT);
         }
      }
      else
      {
         system_log(ERROR_SIGN, __FILE__, __LINE__,
                    "Failed to open() fifo %s : %s",
                    log_file, strerror(errno));
         exit(INCORRECT);
      }
   }

   /*
    * Lets determine the largest offset so the 'structure'
    * is aligned correctly.
    */
   n = sizeof(clock_t);
   if (sizeof(off_t) > n)
   {
      n = sizeof(off_t);
   }
   if (sizeof(time_t) > n)
   {
      n = sizeof(time_t);
   }
   if (sizeof(unsigned int) > n)
   {
      n = sizeof(unsigned int);
   }
   
   /*
    * Determine the size of the fifo buffer. Then create a buffer
    * large enough to hold the data from a fifo.
    */
   if ((fifo_size = fpathconf(log_fd, _PC_PIPE_BUF)) < 0)
   {
      /* If we cannot determine the size of the fifo set default value. */
      fifo_size = DEFAULT_FIFO_SIZE;
   }
   if (fifo_size < (n + n + n + n + n + sizeof(unsigned int) +
                    MAX_HOSTNAME_LENGTH + 4 + 1 + MAX_FILENAME_LENGTH +
                    MAX_FILENAME_LENGTH))
   {
      system_log(DEBUG_SIGN, __FILE__, __LINE__,
                 "Fifo is NOT large enough to ensure atomic writes!");
      fifo_size = n + n + n + n + n + sizeof(unsigned int) +
                  MAX_HOSTNAME_LENGTH + 4 + 1 + MAX_FILENAME_LENGTH +
                  MAX_FILENAME_LENGTH;
   }

   /* Now lets allocate memory for the fifo buffer. */
   if ((fifo_buffer = malloc((size_t)fifo_size)) == NULL)
   {
      system_log(ERROR_SIGN, __FILE__, __LINE__,
                 "Could not allocate memory for the fifo buffer : %s",
                 strerror(errno));
      exit(INCORRECT);
   }

   /* Get the maximum number of logfiles we keep for history. */
   get_max_log_values(&max_delete_log_files, MAX_DELETE_LOG_FILES_DEF,
                      MAX_DELETE_LOG_FILES, NULL, NULL, 0);

   /*
    * Set umask so that all log files have the permission 644.
    * If we do not set this here fopen() will create files with
    * permission 666 according to POSIX.1.
    */
#ifdef GROUP_CAN_WRITE
   (void)umask(S_IWOTH);
#else
   (void)umask(S_IWGRP | S_IWOTH);
#endif

   /*
    * Lets open the delete file name buffer. If it does not yet exists
    * create it.
    */
   get_log_number(&log_number,
                  (max_delete_log_files - 1),
                  DELETE_BUFFER_FILE,
                  DELETE_BUFFER_FILE_LENGTH,
                  NULL);
#ifdef HAVE_SNPRINTF
   (void)snprintf(current_log_file, MAX_PATH_LENGTH, "%s%s/%s0",
#else
   (void)sprintf(current_log_file, "%s%s/%s0",
#endif
                 work_dir, LOG_DIR, DELETE_BUFFER_FILE);
#ifdef HAVE_SNPRINTF
   p_end = log_file + snprintf(log_file, MAX_PATH_LENGTH, "%s%s/%s",
#else
   p_end = log_file + sprintf(log_file, "%s%s/%s",
#endif
                              work_dir, LOG_DIR, DELETE_BUFFER_FILE);

   /* Calculate time when we have to start a new file. */
   next_file_time = (time(NULL) / SWITCH_FILE_TIME) * SWITCH_FILE_TIME +
                    SWITCH_FILE_TIME;

   /* Is current log file already too old? */
   if (stat(current_log_file, &stat_buf) == 0)
   {
      if (stat_buf.st_mtime < (next_file_time - SWITCH_FILE_TIME))
      {
         if (log_number < (max_delete_log_files - 1))
         {
            log_number++;
         }
         if (max_delete_log_files > 1)
         {
            reshuffel_log_files(log_number, log_file, p_end, 0, 0);
         }
         else
         {
            if (unlink(current_log_file) == -1)
            {
               system_log(WARN_SIGN, __FILE__, __LINE__,
                          "Failed to unlink() current log file `%s' : %s",
                          current_log_file, strerror(errno));
            }
         }
      }
   }

#ifdef WITH_LOG_CACHE
   delete_file = open_log_file(current_log_file, NULL, NULL, NULL);
#else
   delete_file = open_log_file(current_log_file);
#endif

   /* Position pointers in fifo so that we only need to read */
   /* the data as they are in the fifo.                      */
   file_size = (off_t *)fifo_buffer;
   job_id = (unsigned int *)(fifo_buffer + n);
   dir_id = (unsigned int *)(fifo_buffer + n + n);
   input_time = (time_t *)(fifo_buffer + n + n + n);
   split_job_counter = (unsigned int *)(fifo_buffer + n + n + n + n);
   unique_number = (unsigned int *)(fifo_buffer + n + n + n + n + n);
   p_host_name = (char *)(fifo_buffer + n + n + n + n + n +
                          sizeof(unsigned int));
   file_name_length = (unsigned char *)(fifo_buffer + n + n + n + n + n +
                                        sizeof(unsigned int) +
                                        MAX_HOSTNAME_LENGTH + 4 + 1);
   p_file_name = (char *)(fifo_buffer + n + n + n + n + n +
                          sizeof(unsigned int) + 1 +
                          MAX_HOSTNAME_LENGTH + 4 + 1);
   check_size = n + n + n + n + n + sizeof(unsigned int) +
                MAX_HOSTNAME_LENGTH + 4 + sizeof(unsigned char) + 1 + 1 + 1;

   /* Ignore any SIGHUP signal. */
   if (signal(SIGHUP, SIG_IGN) == SIG_ERR)
   {
      system_log(DEBUG_SIGN, __FILE__, __LINE__,
                 "signal() error : %s", strerror(errno));
   }

   /*
    * Now lets wait for data to be written to the delete log.
    */
   FD_ZERO(&rset);
   for (;;)
   {
      /* Initialise descriptor set and timeout. */
      FD_SET(log_fd, &rset);
      timeout.tv_usec = 0L;
      timeout.tv_sec = 3L;

      /* Wait for message x seconds and then continue. */
      status = select(log_fd + 1, &rset, NULL, NULL, &timeout);

      if (status == 0)
      {
         if (no_of_buffered_writes > 0)
         {
            (void)fflush(delete_file);
            no_of_buffered_writes = 0;
         }

         /* Check if we have to create a new log file. */
         if (time(&now) > next_file_time)
         {
            if (log_number < (max_delete_log_files - 1))
            {
               log_number++;
            }
            if (fclose(delete_file) == EOF)
            {
               system_log(ERROR_SIGN, __FILE__, __LINE__,
                          "fclose() error : %s", strerror(errno));
            }
            if (max_delete_log_files > 1)
            {
               reshuffel_log_files(log_number, log_file, p_end, 0, 0);
            }
            else
            {
               if (unlink(current_log_file) == -1)
               {
                  system_log(WARN_SIGN, __FILE__, __LINE__,
                             "Failed to unlink() current log file `%s' : %s",
                             current_log_file, strerror(errno));
               }
            }
#ifdef WITH_LOG_CACHE
            delete_file = open_log_file(current_log_file, NULL, NULL, NULL);
#else
            delete_file = open_log_file(current_log_file);
#endif
            next_file_time = (now / SWITCH_FILE_TIME) *
                             SWITCH_FILE_TIME + SWITCH_FILE_TIME;
         }
      }
      else if (FD_ISSET(log_fd, &rset))
           {
              /*
               * It is accurate enough to look at the time once only,
               * even though we are writting in a loop to the delete
               * file.
               */
              now = time(NULL);

              /*
               * Aaaah. Some new data has arrived. Lets write this
               * data to the delete log. The data in the 
               * fifo always has the following format:
               *
               *   <file size><job ID><dir ID><input time><split job counter>
               *   <unique name><host name + reason><file name length>
               *   <file name><user/process[+ add. reason]>
               */
              if ((n = read(log_fd, &fifo_buffer[bytes_buffered],
                            fifo_size - bytes_buffered)) > 0)
              {
                 n += bytes_buffered;
                 bytes_buffered = 0;
                 do
                 {
                    if ((n < (check_size - 2)) ||
                        (n < (check_size + *file_name_length - 1)))
                    {
                       length = n;
                       bytes_buffered = n;
                    }
                    else
                    {
                       length = check_size + *file_name_length +
                                strlen(&p_file_name[*file_name_length + 1]);
                       if (n < length)
                       {
                          length = n;
                          bytes_buffered = n;
                       }
                       else
                       {
                          if (*input_time == 0L)
                          {
                             (void)fprintf(delete_file,
#if SIZEOF_OFF_T == 4
# if SIZEOF_TIME_T == 4
                                           "%-*lx %s%c%s%c%lx%c%x%c%x%c%c%s\n",
# else
                                           "%-*llx %s%c%s%c%lx%c%x%c%x%c%c%s\n",
# endif
#else
# if SIZEOF_TIME_T == 4
                                           "%-*lx %s%c%s%c%llx%c%x%c%x%c%c%s\n",
# else
                                           "%-*llx %s%c%s%c%llx%c%x%c%x%c%c%s\n",
# endif
#endif
                                           LOG_DATE_LENGTH, (pri_time_t)now,
                                           p_host_name,
                                           SEPARATOR_CHAR,
                                           p_file_name,
                                           SEPARATOR_CHAR,
                                           (pri_off_t)*file_size,
                                           SEPARATOR_CHAR,
                                           *job_id,
                                           SEPARATOR_CHAR,
                                           *dir_id,
                                           SEPARATOR_CHAR,
                                           SEPARATOR_CHAR,
                                           &p_file_name[*file_name_length + 1]);
                          }
                          else
                          {
                             (void)fprintf(delete_file,
#if SIZEOF_OFF_T == 4
# if SIZEOF_TIME_T == 4
                                           "%-*lx %s%c%s%c%lx%c%x%c%x%c%lx_%x_%x%c%s\n",
# else
                                           "%-*llx %s%c%s%c%lx%c%x%c%x%c%llx_%x_%x%c%s\n",
# endif
#else
# if SIZEOF_TIME_T == 4
                                           "%-*lx %s%c%s%c%llx%c%x%c%x%c%lx_%x_%x%c%s\n",
# else
                                           "%-*llx %s%c%s%c%llx%c%x%c%x%c%llx_%x_%x%c%s\n",
# endif
#endif
                                           LOG_DATE_LENGTH, (pri_time_t)now,
                                           p_host_name,
                                           SEPARATOR_CHAR,
                                           p_file_name,
                                           SEPARATOR_CHAR,
                                           (pri_off_t)*file_size,
                                           SEPARATOR_CHAR,
                                           *job_id,
                                           SEPARATOR_CHAR,
                                           *dir_id,
                                           SEPARATOR_CHAR,
                                           (pri_time_t)*input_time,
                                           *unique_number,
                                           *split_job_counter,
                                           SEPARATOR_CHAR,
                                           &p_file_name[*file_name_length + 1]);
                          }
                       }
                    }
                    n -= length;
                    if (n > 0)
                    {
                       (void)memmove(fifo_buffer, &fifo_buffer[length], n);
                    }
                    no_of_buffered_writes++;
                 } while (n > 0);

                 if (no_of_buffered_writes > BUFFERED_WRITES_BEFORE_FLUSH_SLOW)
                 {
                    (void)fflush(delete_file);
                    no_of_buffered_writes = 0;
                 }
              }
              else if (n < 0)
                   {
                      system_log(FATAL_SIGN, __FILE__, __LINE__,
                                 "read() error (%d) : %s", n, strerror(errno));
                      exit(INCORRECT);
                   }

              /*
               * Since we can receive a constant stream of data
               * on the fifo, we might never get that select() returns 0.
               * Thus we always have to check if it is time to create
               * a new log file.
               */
              if (now > next_file_time)
              {
                 if (log_number < (max_delete_log_files - 1))
                 {
                    log_number++;
                 }
                 if (fclose(delete_file) == EOF)
                 {
                    system_log(ERROR_SIGN, __FILE__, __LINE__,
                               "fclose() error : %s", strerror(errno));
                 }
                 if (max_delete_log_files > 1)
                 {
                    reshuffel_log_files(log_number, log_file, p_end, 0, 0);
                 }
                 else
                 {
                    if (unlink(current_log_file) == -1)
                    {
                       system_log(WARN_SIGN, __FILE__, __LINE__,
                                  "Failed to unlink() current log file `%s' : %s",
                                  current_log_file, strerror(errno));
                    }
                 }
#ifdef WITH_LOG_CACHE
                 delete_file = open_log_file(current_log_file, NULL, NULL, NULL);
#else
                 delete_file = open_log_file(current_log_file);
#endif
                 next_file_time = (now / SWITCH_FILE_TIME) * SWITCH_FILE_TIME +
                                  SWITCH_FILE_TIME;
              }
           }
           else
           {
              system_log(ERROR_SIGN, __FILE__, __LINE__,
                         "Select error : %s", strerror(errno));
              exit(INCORRECT);
           }
   } /* for (;;) */

#endif /* _DELETE_LOG */
   /* Should never come to this point. */
   exit(SUCCESS);
}
