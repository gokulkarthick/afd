/*
 *  sf_scp1.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 2001 Holger Kiehl <Holger.Kiehl@dwd.de>
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
 **   sf_scp1 - send files via the SCP1 protocol
 **
 ** SYNOPSIS
 **   sf_scp1 [options]
 **
 **   options
 **       --version               - Version
 **       -w directory            - the working directory of the
 **                                 AFD
 **
 ** DESCRIPTION
 **   sf_scp1 sends the given files to the defined recipient via the
 **   SCP1 protocol by using the ssh program.
 **
 **   In the message file will be the data it needs about the
 **   remote host in the following format:
 **       [destination]
 **       <scheme>://<user>:<password>@<host>:<port>/<url-path>
 **
 **       [options]
 **       <a list of FD options, terminated by a newline>
 **
 **   If the archive flag is set, each file will be archived after it
 **   has been send successful.
 **
 ** RETURN VALUES
 **   SUCCESS on normal exit and INCORRECT when an error has
 **   occurred.
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   04.05.2001 H.Kiehl Created
 **
 */
DESCR__E_M1

#include <stdio.h>                     /* fprintf(), sprintf()           */
#include <string.h>                    /* strcpy(), strcat(), strcmp(),  */
                                       /* strerror()                     */
#include <stdlib.h>                    /* malloc(), free(), abort()      */
#include <ctype.h>                     /* isdigit()                      */
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _OUTPUT_LOG
#include <sys/times.h>                 /* times(), struct tms            */
#endif
#include <fcntl.h>
#include <signal.h>                    /* signal()                       */
#include <unistd.h>                    /* unlink(), close()              */
#include <errno.h>
#include "fddefs.h"
#include "scp1defs.h"
#include "version.h"

/* Global variables */
int                        counter_fd = -1,     /* NOT USED */
                           exitflag = IS_FAULTY_VAR,
                           no_of_hosts,    /* This variable is not used */
                                           /* in this module.           */
                           trans_rule_pos, /* Not used [init_sf()]      */
                           user_rule_pos,  /* Not used [init_sf()]      */
                           fsa_id,
                           fsa_fd = -1,
                           sys_log_fd = STDERR_FILENO,
                           transfer_log_fd = STDERR_FILENO,
                           trans_db_log_fd = STDERR_FILENO,
                           amg_flag = NO,
                           timeout_flag;
#ifndef _NO_MMAP
off_t                      fsa_size;
#endif
off_t                      *file_size_buffer = NULL;
long                       transfer_timeout;
char                       host_deleted = NO,
                           initial_filename[MAX_FILENAME_LENGTH],
                           msg_str[MAX_RET_MSG_LENGTH],
                           *p_work_dir = NULL,
                           tr_hostname[MAX_HOSTNAME_LENGTH + 1],
                           line_buffer[4096],
                           *file_name_buffer = NULL;
struct filetransfer_status *fsa;
struct job                 db;
struct rule                *rule;
#ifdef _DELETE_LOG
struct delete_log          dl;
#endif

#ifdef _WITH_SCP1_SUPPORT
/* Local functions */
static void sf_scp1_exit(void),
            sig_bus(int),
            sig_segv(int),
            sig_pipe(int),
            sig_kill(int),
            sig_exit(int);
#endif


/*$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ main() $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$*/
int
main(int argc, char *argv[])
{
#ifdef _WITH_SCP1_SUPPORT
#ifdef _VERIFY_FSA
   unsigned int     ui_variable;
#endif
   int              j,
                    fd,
                    status,
                    no_of_bytes,
                    loops,
                    rest,
                    files_to_send,
                    files_send = 0,
#ifdef _BURST_MODE
                    total_files_send = 0,
                    burst_counter = 0,
#endif
                    blocksize;
   off_t            lock_offset;
#ifdef _OUTPUT_LOG
   int              ol_fd = -1;
   unsigned int     *ol_job_number = NULL;
   char             *ol_data = NULL,
                    *ol_file_name = NULL;
   unsigned short   *ol_archive_name_length,
                    *ol_file_name_length;
   off_t            *ol_file_size = NULL;
   size_t           ol_size,
                    ol_real_size;
   clock_t          end_time = 0,
                    start_time = 0,
                    *ol_transfer_time = NULL;
   struct tms       tmsdummy;
#endif
   off_t            *p_file_size_buffer;
   char             *p_file_name_buffer,
#ifdef _BURST_MODE
                    search_for_files = NO,
#endif
                    *buffer,
                    fullname[MAX_PATH_LENGTH],
                    file_path[MAX_PATH_LENGTH],
                    work_dir[MAX_PATH_LENGTH];
   struct stat      stat_buf;
   struct job       *p_db;
#ifdef SA_FULLDUMP
   struct sigaction sact;
#endif

   CHECK_FOR_VERSION(argc, argv);

#ifdef SA_FULLDUMP
   /*
    * When dumping core sure we do a FULL core dump!
    */
   sact.sa_handler = SIG_DFL;
   sact.sa_flags = SA_FULLDUMP;
   sigemptyset(&sact.sa_mask);
   if (sigaction(SIGSEGV, &sact, NULL) == -1)
   {
      system_log(ERROR_SIGN, __FILE__, __LINE__,
                 "sigaction() error : %s", strerror(errno));
      exit(INCORRECT);
   }
#endif

   /* Do some cleanups when we exit */
   if (atexit(sf_scp1_exit) != 0)
   {
      system_log(ERROR_SIGN, __FILE__, __LINE__,
                 "Could not register exit function : %s", strerror(errno));
      exit(INCORRECT);
   }

   /* Initialise variables */
   p_work_dir = work_dir;
   files_to_send = init_sf(argc, argv, file_path, SCP1_FLAG);
   p_db = &db;
   blocksize = fsa[db.fsa_pos].block_size;

   if ((signal(SIGINT, sig_kill) == SIG_ERR) ||
       (signal(SIGQUIT, sig_exit) == SIG_ERR) ||
       (signal(SIGTERM, sig_exit) == SIG_ERR) ||
       (signal(SIGSEGV, sig_segv) == SIG_ERR) ||
       (signal(SIGBUS, sig_bus) == SIG_ERR) ||
       (signal(SIGHUP, SIG_IGN) == SIG_ERR) ||
       (signal(SIGPIPE, sig_pipe) == SIG_ERR))
   {
      system_log(ERROR_SIGN, __FILE__, __LINE__,
                 "signal() error : %s", strerror(errno));
      exit(INCORRECT);
   }

#ifdef _OUTPUT_LOG
   if (db.output_log == YES)
   {
      output_log_ptrs(&ol_fd,                /* File descriptor to fifo */
                      &ol_job_number,
                      &ol_data,              /* Pointer to buffer       */
                      &ol_file_name,
                      &ol_file_name_length,
                      &ol_archive_name_length,
                      &ol_file_size,
                      &ol_size,
                      &ol_transfer_time,
                      db.host_alias,
                      SCP1);
   }
#endif

   timeout_flag = OFF;

   /* Now determine the real hostname. */
   if (db.toggle_host == YES)
   {
      if (fsa[db.fsa_pos].host_toggle == HOST_ONE)
      {
         (void)strcpy(db.hostname,
                      fsa[db.fsa_pos].real_hostname[HOST_TWO - 1]);
      }
      else
      {
         (void)strcpy(db.hostname,
                      fsa[db.fsa_pos].real_hostname[HOST_ONE - 1]);
      }
   }
   else
   {
      (void)strcpy(db.hostname,
                   fsa[db.fsa_pos].real_hostname[(int)(fsa[db.fsa_pos].host_toggle - 1)]);
   }

   /* Connect to remote SSH-server via local SSH-client. */
   if ((status = scp1_connect(db.hostname, db.port, db.user,
                              db.password, db.target_dir)) != SUCCESS)
   {
      msg_str[0] = '\0';
      trans_log(ERROR_SIGN, __FILE__, __LINE__,
                "SCP1 connection to %s at port %d failed (%d).",
                db.hostname, db.port, status);
      exit((timeout_flag == ON) ? TIMEOUT_ERROR : CONNECT_ERROR);
   }
   else
   {
      if (fsa[db.fsa_pos].debug == YES)
      {
         trans_db_log(INFO_SIGN, __FILE__, __LINE__,
                      "Connected to port %d.", db.port);
      }
   }

   /* Inform FSA that we have finished connecting */
   /* and will now start to transfer data.        */
   if (host_deleted == NO)
   {
      lock_offset = (char *)&fsa[db.fsa_pos] - (char *)fsa;
      rlock_region(fsa_fd, lock_offset);
      if (check_fsa() == YES)
      {
         if ((db.fsa_pos = get_host_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
         {
            host_deleted = YES;
         }
         else
         {
            lock_offset = (char *)&fsa[db.fsa_pos] - (char *)fsa;
            rlock_region(fsa_fd, lock_offset);
         }
      }
      if (host_deleted == NO)
      {
         fsa[db.fsa_pos].job_status[(int)db.job_no].connect_status = SCP1_ACTIVE;
         fsa[db.fsa_pos].job_status[(int)db.job_no].no_of_files = files_to_send;

         /* Number of connections */
         lock_region_w(fsa_fd, (char *)&fsa[db.fsa_pos].connections - (char *)fsa);
         fsa[db.fsa_pos].connections += 1;
         unlock_region(fsa_fd, (char *)&fsa[db.fsa_pos].connections - (char *)fsa);
         unlock_region(fsa_fd, lock_offset);
      }
   }

   /* Allocate buffer to read data from the source file. */
   if ((buffer = malloc(blocksize)) == NULL)
   {
      system_log(ERROR_SIGN, __FILE__, __LINE__,
                 "malloc() error : %s", strerror(errno));
      exit(ALLOC_ERROR);
   }

#ifdef _BURST_MODE
   do
   {
      if (search_for_files == YES)
      {
         off_t file_size_to_send = 0;

         lock_offset = (char *)&fsa[db.fsa_pos] - (char *)fsa;
         rlock_region(fsa_fd, lock_offset);
         if (check_fsa() == YES)
         {
            if ((db.fsa_pos = get_host_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
            {
               host_deleted = YES;
               lock_offset = -1;
            }
            else
            {
               lock_offset = (char *)&fsa[db.fsa_pos] - (char *)fsa;
               rlock_region(fsa_fd, lock_offset);
               lock_region_w(fsa_fd, (char *)&fsa[db.fsa_pos].job_status[(int)db.job_no].job_id - (char *)fsa);
            }
         }
         if ((files_to_send = get_file_names(file_path, &file_size_to_send)) < 1)
         {
            /*
             * With age limit it can happen that files_to_send is zero.
             * Though very unlikely.
             */
            system_log(DEBUG_SIGN, __FILE__, __LINE__,
                       "Hmmm. Burst counter = %d and files_to_send = %d [%s]. How is this possible? AAarrgghhhhh....",
                       fsa[db.fsa_pos].job_status[(int)db.job_no].burst_counter,
                       files_to_send, file_path);
            fsa[db.fsa_pos].job_status[(int)db.job_no].burst_counter = 0;
            if (lock_offset != -1)
            {
               unlock_region(fsa_fd, lock_offset);
            }
            break;
         }
         burst_counter = fsa[db.fsa_pos].job_status[(int)db.job_no].burst_counter;
         unlock_region(fsa_fd, (char *)&fsa[db.fsa_pos].job_status[(int)db.job_no].job_id - (char *)fsa);

         total_files_send += files_send;

         /* Tell user we are bursting */
         if (host_deleted == NO)
         {
            if (check_fsa() == YES)
            {
               if ((db.fsa_pos = get_host_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
               {
                  host_deleted = YES;
                  lock_offset = -1;
               }
               else
               {
                  lock_offset = (char *)&fsa[db.fsa_pos] - (char *)fsa;
                  rlock_region(fsa_fd, lock_offset);
               }
            }
            if (host_deleted == NO)
            {
               fsa[db.fsa_pos].job_status[(int)db.job_no].connect_status = SCP1_BURST_TRANSFER_ACTIVE;
               fsa[db.fsa_pos].job_status[(int)db.job_no].no_of_files = fsa[db.fsa_pos].job_status[(int)db.job_no].no_of_files_done + files_to_send;
               fsa[db.fsa_pos].job_status[(int)db.job_no].file_size = fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_done + file_size_to_send;
            }
            if (fsa[db.fsa_pos].debug == YES)
            {
               trans_db_log(INFO_SIGN, __FILE__, __LINE__, "Bursting.");
            }
         }

         if (lock_offset != -1)
         {
            unlock_region(fsa_fd, lock_offset);
         }
      }
#endif

      /* Send all files */
      p_file_name_buffer = file_name_buffer;
      p_file_size_buffer = file_size_buffer;
      for (files_send = 0; files_send < files_to_send; files_send++)
      {
         (void)sprintf(fullname, "%s/%s", file_path, p_file_name_buffer);

         if (*p_file_size_buffer > 0)
         {
            /* Write status to FSA? */
            if (host_deleted == NO)
            {
               if (check_fsa() == YES)
               {
                  if ((db.fsa_pos = get_host_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
                  {
                     host_deleted = YES;
                  }
               }
               if (host_deleted == NO)
               {
                  fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_in_use = *p_file_size_buffer;
                  (void)strcpy(fsa[db.fsa_pos].job_status[(int)db.job_no].file_name_in_use,
                               p_file_name_buffer);
               }
            }

            /* Send file in dot notation? */
            if ((db.lock == DOT) || (db.lock == DOT_VMS))
            {
               (void)strcpy(initial_filename, db.lock_notation);
               (void)strcat(initial_filename, p_file_name_buffer);
            }
            else
            {
               (void)strcpy(initial_filename, p_file_name_buffer);
            }

#ifdef _OUTPUT_LOG
            if (db.output_log == YES)
            {
               start_time = times(&tmsdummy);
            }
#endif

            /* Open file on remote site. */
            if ((status = scp1_open_file(initial_filename, *p_file_size_buffer,
                                         db.chmod)) == INCORRECT)
            {
               trans_log(ERROR_SIGN, __FILE__, __LINE__,
                         "Failed to open remote file <%s> (%d).",
                         initial_filename, status);
               msg_str[0] = '\0';
               trans_log(INFO_SIGN, NULL, 0, "%d Bytes send in %d file(s).",
                         fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_done,
                         fsa[db.fsa_pos].job_status[(int)db.job_no].no_of_files_done);
               scp1_quit();
               exit((timeout_flag == ON) ? TIMEOUT_ERROR : OPEN_REMOTE_ERROR);
            }
            else
            {
               if (fsa[db.fsa_pos].debug == YES)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__,
                               "Open remote file <%s>.", initial_filename);
               }
            }

            /* Open local file */
            if ((fd = open(fullname, O_RDONLY)) == -1)
            {
               trans_log(ERROR_SIGN, __FILE__, __LINE__,
                         "Failed to open local file <%s> : %s",
                         fullname, strerror(errno));
               msg_str[0] = '\0';
               trans_log(INFO_SIGN, NULL, 0, "%d Bytes send in %d file(s).",
                         fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_done,
                         fsa[db.fsa_pos].job_status[(int)db.job_no].no_of_files_done);
               scp1_quit();
               exit(OPEN_LOCAL_ERROR);
            }
            if (fsa[db.fsa_pos].debug == YES)
            {
               trans_db_log(INFO_SIGN, __FILE__, __LINE__,
                            "Open local file <%s>", fullname);
            }

            /* Read (local) and write (remote) file */
            no_of_bytes = 0;
            loops = *p_file_size_buffer / blocksize;
            rest = *p_file_size_buffer % blocksize;

            for (;;)
            {
               for (j = 0; j < loops; j++)
               {
#ifdef _SIMULATE_SLOW_TRANSFER
                  (void)sleep(2);
#endif
                  if ((status = read(fd, buffer, blocksize)) != blocksize)
                  {
                     trans_log(ERROR_SIGN, __FILE__, __LINE__,
                               "Could not read() local file <%s> : %s",
                               fullname, strerror(errno));
                     msg_str[0] = '\0';
                     trans_log(INFO_SIGN, NULL, 0,
                               "%d Bytes send in %d file(s).",
                               fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_done,
                               fsa[db.fsa_pos].job_status[(int)db.job_no].no_of_files_done);
                     scp1_quit();
                     exit(READ_LOCAL_ERROR);
                  }
                  if (scp1_write(buffer, blocksize) < 0)
                  {
                     trans_log(ERROR_SIGN, __FILE__, __LINE__,
                               "Failed to write block from file <%s> to remote port %d.",
                               p_file_name_buffer, db.port);
                     msg_str[0] = '\0';
                     trans_log(INFO_SIGN, NULL, 0,
                               "%d Bytes send in %d file(s).",
                               fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_done,
                               fsa[db.fsa_pos].job_status[(int)db.job_no].no_of_files_done);
                     scp1_quit();
                     exit((timeout_flag == ON) ? TIMEOUT_ERROR : WRITE_REMOTE_ERROR);
                  }

                  no_of_bytes += blocksize;

                  if (host_deleted == NO)
                  {
                     if (check_fsa() == YES)
                     {
                        if ((db.fsa_pos = get_host_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
                        {
                           host_deleted = YES;
                        }
                     }
                     if (host_deleted == NO)
                     {
                        fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_in_use_done = no_of_bytes;
                        fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_done += blocksize;
                        fsa[db.fsa_pos].job_status[(int)db.job_no].bytes_send += blocksize;
                     }
                  }
               } /* for (j = 0; j < loops; j++) */

               if (rest > 0)
               {
                  if ((status = read(fd, buffer, rest)) != rest)
                  {
                     trans_log(ERROR_SIGN, __FILE__, __LINE__,
                               "Could not read() local file <%s> : %s",
                               fullname, strerror(errno));
                     msg_str[0] = '\0';
                     trans_log(INFO_SIGN, NULL, 0,
                               "%d Bytes send in %d file(s).",
                               fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_done,
                               fsa[db.fsa_pos].job_status[(int)db.job_no].no_of_files_done);
                     scp1_quit();
                     exit(READ_LOCAL_ERROR);
                  }
                  if (scp1_write(buffer, rest) < 0)
                  {
                     trans_log(ERROR_SIGN, __FILE__, __LINE__,
                               "Failed to write rest of file <%s> to remote port %d.",
                               p_file_name_buffer, db.port);
                     msg_str[0] = '\0';
                     trans_log(INFO_SIGN, NULL, 0,
                               "%d Bytes send in %d file(s).",
                               fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_done,
                               fsa[db.fsa_pos].job_status[(int)db.job_no].no_of_files_done);
                     scp1_quit();
                     exit((timeout_flag == ON) ? TIMEOUT_ERROR : WRITE_REMOTE_ERROR);
                  }

                  no_of_bytes += rest;

                  if (host_deleted == NO)
                  {
                     if (check_fsa() == YES)
                     {
                        if ((db.fsa_pos = get_host_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
                        {
                           host_deleted = YES;
                        }
                     }
                     if (host_deleted == NO)
                     {
                        fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_in_use_done = no_of_bytes;
                        fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_done += rest;
                        fsa[db.fsa_pos].job_status[(int)db.job_no].bytes_send += rest;
                     }
                  }
               }

               /*
                * Since there are always some users sending files to the
                * AFD not in dot notation, lets check here if this is really
                * the EOF.
                * If not lets continue so long until we hopefully have reached
                * the EOF.
                * NOTE: This is NOT a fool proof way. There must be a better
                *       way!
                */
               if (fstat(fd, &stat_buf) == -1)
               {
                  msg_str[0] = '\0';
                  trans_log(DEBUG_SIGN, __FILE__, __LINE__,
                            "Hmmm. Failed to stat() <%s> : %s",
                            fullname, strerror(errno));
                  break;
               }
               else
               {
                  if (stat_buf.st_size > *p_file_size_buffer)
                  {
                     loops = (stat_buf.st_size - *p_file_size_buffer) / blocksize;
                     rest = (stat_buf.st_size - *p_file_size_buffer) % blocksize;
                     *p_file_size_buffer = stat_buf.st_size;

                     /*
                      * Give a warning in the system log, so some action
                      * can be taken against the originator.
                      */
                     system_log(WARN_SIGN, __FILE__, __LINE__,
                                "File <%s> for host %s was DEFINITELY NOT send in dot notation.",
                                p_file_name_buffer,
                                fsa[db.fsa_pos].host_dsp_name);
                  }
                  else
                  {
                     break;
                  }
               }
            } /* for (;;) */

#ifdef _OUTPUT_LOG
            if (db.output_log == YES)
            {
               end_time = times(&tmsdummy);
            }
#endif

            /* Close local file */
            if (close(fd) == -1)
            {
               msg_str[0] = '\0';
               trans_log(WARN_SIGN, __FILE__, __LINE__,
                         "Failed to close() local file <%s> : %s",
                         p_file_name_buffer, strerror(errno));
               /*
                * Since we usually do not send more then 100 files and
                * sf_scp1() will exit(), there is no point in stopping
                * the transmission.
                */
            }

            if ((status = scp1_close_file()) == INCORRECT)
            {
               trans_log(ERROR_SIGN, __FILE__, __LINE__,
                         "Failed to close remote file <%s>", initial_filename);
               msg_str[0] = '\0';
               trans_log(INFO_SIGN, NULL, 0, "%d Bytes send in %d file(s).",
                         fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_done,
                         fsa[db.fsa_pos].job_status[(int)db.job_no].no_of_files_done);
               scp1_quit();
               exit((timeout_flag == ON) ? TIMEOUT_ERROR : CLOSE_REMOTE_ERROR);
            }
            else
            {
               if (fsa[db.fsa_pos].debug == YES)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__,
                               "Closed data connection for file <%s>.",
                               initial_filename);
               }
            }
         }
         else
         {
            trans_log(WARN_SIGN, __FILE__, __LINE__,
                      "File <%s> is of zero length, ignoring.",
                      p_file_name_buffer);
         }

         /* Update FSA, one file transmitted. */
         if (host_deleted == NO)
         {
            lock_offset = (char *)&fsa[db.fsa_pos] - (char *)fsa;
            rlock_region(fsa_fd, lock_offset);

            /* Before we read from the FSA lets make */
            /* sure that it is NOT stale!            */
            if (check_fsa() == YES)
            {
               if ((db.fsa_pos = get_host_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
               {
                  host_deleted = YES;
               }
               else
               {
                  lock_offset = (char *)&fsa[db.fsa_pos] - (char *)fsa;
                  rlock_region(fsa_fd, lock_offset);
               }
            }
            if (host_deleted == NO)
            {
               fsa[db.fsa_pos].job_status[(int)db.job_no].file_name_in_use[0] = '\0';
               fsa[db.fsa_pos].job_status[(int)db.job_no].no_of_files_done++;
               fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_in_use = 0;
               fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_in_use_done = 0;

               /* Total file counter */
               lock_region_w(fsa_fd, (char *)&fsa[db.fsa_pos].total_file_counter - (char *)fsa);
               fsa[db.fsa_pos].total_file_counter -= 1;
#ifdef _VERIFY_FSA
               if (fsa[db.fsa_pos].total_file_counter < 0)
               {
                  system_log(DEBUG_SIGN, __FILE__, __LINE__,
                             "Total file counter for host %s less then zero. Correcting to %d.",
                             fsa[db.fsa_pos].host_dsp_name,
                             files_to_send - (files_send + 1));
                  fsa[db.fsa_pos].total_file_counter = files_to_send - (files_send + 1);
               }
#endif

               /* Total file size */
#ifdef _VERIFY_FSA
               ui_variable = fsa[db.fsa_pos].total_file_size;
#endif
               fsa[db.fsa_pos].total_file_size -= stat_buf.st_size;
#ifdef _VERIFY_FSA
               if (fsa[db.fsa_pos].total_file_size > ui_variable)
               {
                  int   k;
                  off_t *tmp_ptr = p_file_size_buffer;

                  tmp_ptr++;
                  fsa[db.fsa_pos].total_file_size = 0;
                  for (k = (files_send + 1); k < files_to_send; k++)
                  {
                     fsa[db.fsa_pos].total_file_size += *tmp_ptr;
                  }

                  system_log(DEBUG_SIGN, __FILE__, __LINE__,
                             "Total file size for host %s overflowed. Correcting to %lu.",
                             fsa[db.fsa_pos].host_dsp_name,
                             fsa[db.fsa_pos].total_file_size);
               }
               else if ((fsa[db.fsa_pos].total_file_counter == 0) &&
                        (fsa[db.fsa_pos].total_file_size > 0))
                    {
                       system_log(DEBUG_SIGN, __FILE__, __LINE__,
                                  "fc for host %s is zero but fs is not zero. Correcting.",
                                  fsa[db.fsa_pos].host_dsp_name);
                       fsa[db.fsa_pos].total_file_size = 0;
                    }
#endif
               unlock_region(fsa_fd, (char *)&fsa[db.fsa_pos].total_file_counter - (char *)fsa);

               /* File counter done */
               lock_region_w(fsa_fd, (char *)&fsa[db.fsa_pos].file_counter_done - (char *)fsa);
               fsa[db.fsa_pos].file_counter_done += 1;
               unlock_region(fsa_fd, (char *)&fsa[db.fsa_pos].file_counter_done - (char *)fsa);

               /* Number of bytes send */
               lock_region_w(fsa_fd, (char *)&fsa[db.fsa_pos].bytes_send - (char *)fsa);
               fsa[db.fsa_pos].bytes_send += stat_buf.st_size;
               unlock_region(fsa_fd, (char *)&fsa[db.fsa_pos].bytes_send - (char *)fsa);
               unlock_region(fsa_fd, lock_offset);
            }
         }

#ifdef _WITH_TRANS_EXEC
         if (db.special_flag & TRANS_EXEC)
         {
            trans_exec(file_path, fullname, p_file_name_buffer);
         }
#endif /* _WITH_TRANS_EXEC */

         /* Now archive file if necessary */
         if ((db.archive_time > 0) &&
             (p_db->archive_dir[0] != FAILED_TO_CREATE_ARCHIVE_DIR))
         {
            /*
             * By telling the function archive_file() that this
             * is the first time to archive a file for this job
             * (in struct p_db) it does not always have to check
             * whether the directory has been created or not. And
             * we ensure that we do not create duplicate names
             * when adding ARCHIVE_UNIT * db.archive_time to
             * msg_name.
             */
            if (archive_file(file_path, p_file_name_buffer, p_db) < 0)
            {
               if (fsa[db.fsa_pos].debug == YES)
               {
                  trans_db_log(ERROR_SIGN, __FILE__, __LINE__,
                               "Failed to archive file <%s>",
                               p_file_name_buffer);
               }

               /*
                * NOTE: We _MUST_ delete the file we just send,
                *       else the file directory will run full!
                */
               if (unlink(fullname) == -1)
               {
                  system_log(ERROR_SIGN, __FILE__, __LINE__,
                             "Could not unlink() local file <%s> after sending it successfully : %s",
                             fullname, strerror(errno));
               }

#ifdef _OUTPUT_LOG
               if (db.output_log == YES)
               {
                  (void)strcpy(ol_file_name, p_file_name_buffer);
                  *ol_file_name_length = (unsigned short)strlen(ol_file_name);
                  *ol_file_size = *p_file_size_buffer;
                  *ol_job_number = fsa[db.fsa_pos].job_status[(int)db.job_no].job_id;
                  *ol_transfer_time = end_time - start_time;
                  *ol_archive_name_length = 0;
                  ol_real_size = *ol_file_name_length + ol_size;
                  if (write(ol_fd, ol_data, ol_real_size) != ol_real_size)
                  {
                     system_log(ERROR_SIGN, __FILE__, __LINE__,
                                "write() error : %s", strerror(errno));
                  }
               }
#endif
            }
            else
            {
               if (fsa[db.fsa_pos].debug == YES)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__,
                               "Archived file <%s>", p_file_name_buffer);
               }

#ifdef _OUTPUT_LOG
               if (db.output_log == YES)
               {
                  (void)strcpy(ol_file_name, p_file_name_buffer);
                  *ol_file_name_length = (unsigned short)strlen(ol_file_name);
                  (void)strcpy(&ol_file_name[*ol_file_name_length + 1],
                               &db.archive_dir[db.archive_offset]);
                  *ol_file_size = *p_file_size_buffer;
                  *ol_job_number = fsa[db.fsa_pos].job_status[(int)db.job_no].job_id;
                  *ol_transfer_time = end_time - start_time;
                  *ol_archive_name_length = (unsigned short)strlen(&ol_file_name[*ol_file_name_length + 1]);
                  ol_real_size = *ol_file_name_length +
                                 *ol_archive_name_length + 1 + ol_size;
                  if (write(ol_fd, ol_data, ol_real_size) != ol_real_size)
                  {
                     system_log(ERROR_SIGN, __FILE__, __LINE__,
                                "write() error : %s", strerror(errno));
                  }
               }
#endif
            }
         }
         else
         {
            /* Delete the file we just have send */
            if (unlink(fullname) == -1)
            {
               system_log(ERROR_SIGN, __FILE__, __LINE__,
                          "Could not unlink() local file <%s> after sending it successfully : %s",
                          fullname, strerror(errno));
            }

#ifdef _OUTPUT_LOG
            if (db.output_log == YES)
            {
               (void)strcpy(ol_file_name, p_file_name_buffer);
               *ol_file_name_length = (unsigned short)strlen(ol_file_name);
               *ol_file_size = *p_file_size_buffer;
               *ol_job_number = fsa[db.fsa_pos].job_status[(int)db.job_no].job_id;
               *ol_transfer_time = end_time - start_time;
               *ol_archive_name_length = 0;
               ol_real_size = *ol_file_name_length + ol_size;
               if (write(ol_fd, ol_data, ol_real_size) != ol_real_size)
               {
                  system_log(ERROR_SIGN, __FILE__, __LINE__,
                             "write() error : %s", strerror(errno));
               }
            }
#endif
         }

         /*
          * After each successful transfer set error
          * counter to zero, so that other jobs can be
          * started.
          */
         if ((*p_file_size_buffer > 0) &&
             (fsa[db.fsa_pos].error_counter > 0))
         {
            int  fd,
                 j;
            char fd_wake_up_fifo[MAX_PATH_LENGTH];

            lock_region_w(fsa_fd, (char *)&fsa[db.fsa_pos].error_counter - (char *)fsa);
            fsa[db.fsa_pos].error_counter = 0;

            /*
             * Wake up FD!
             */
            (void)sprintf(fd_wake_up_fifo, "%s%s%s", p_work_dir,
                          FIFO_DIR, FD_WAKE_UP_FIFO);
            if ((fd = open(fd_wake_up_fifo, O_RDWR)) == -1)
            {
               system_log(WARN_SIGN, __FILE__, __LINE__,
                          "Failed to open() FIFO <%s> : %s",
                          fd_wake_up_fifo, strerror(errno));
            }
            else
            {
               if (write(fd, "", 1) != 1)
               {
                  system_log(WARN_SIGN, __FILE__, __LINE__,
                             "Failed to write() to FIFO <%s> : %s",
                             fd_wake_up_fifo, strerror(errno));
               }
               if (close(fd) == -1)
               {
                  system_log(DEBUG_SIGN, __FILE__, __LINE__,
                             "Failed to close() FIFO <%s> : %s",
                             fd_wake_up_fifo, strerror(errno));
               }
            }

            /*
             * Remove the error condition (NOT_WORKING) from all jobs
             * of this host.
             */
            for (j = 0; j < fsa[db.fsa_pos].allowed_transfers; j++)
            {
               if ((j != db.job_no) &&
                   (fsa[db.fsa_pos].job_status[j].connect_status == NOT_WORKING))
               {
                  fsa[db.fsa_pos].job_status[j].connect_status = DISCONNECT;
               }
            }
            unlock_region(fsa_fd, (char *)&fsa[db.fsa_pos].error_counter - (char *)fsa);

            /*
             * Since we have successfully transmitted a file, no need to
             * have the queue stopped anymore.
             */
            if (fsa[db.fsa_pos].host_status & AUTO_PAUSE_QUEUE_STAT)
            {
               fsa[db.fsa_pos].host_status ^= AUTO_PAUSE_QUEUE_STAT;
               system_log(INFO_SIGN, __FILE__, __LINE__,
                          "Starting queue for %s that was stopped by init_afd.",
                          fsa[db.fsa_pos].host_alias);
            }
         } /* if (fsa[db.fsa_pos].error_counter > 0) */

         p_file_name_buffer += MAX_FILENAME_LENGTH;
         p_file_size_buffer++;
      } /* for (files_send = 0; files_send < files_to_send; files_send++) */

#ifdef _BURST_MODE
      search_for_files = YES;
      lock_region_w(fsa_fd, (char *)&fsa[db.fsa_pos].job_status[(int)db.job_no].job_id - (char *)fsa);
   } while (fsa[db.fsa_pos].job_status[(int)db.job_no].burst_counter != burst_counter);

   fsa[db.fsa_pos].job_status[(int)db.job_no].connect_status = CLOSING_CONNECTION;
   fsa[db.fsa_pos].job_status[(int)db.job_no].burst_counter = 0;
   total_files_send += files_send;
#endif

   free(buffer);

   (void)sprintf(line_buffer, "%-*s[%d]: %lu Bytes send in %d file(s).",
                 MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                 fsa[db.fsa_pos].job_status[(int)db.job_no].file_size_done,
                 total_files_send);

#ifdef _BURST_MODE
   if (burst_counter == 1)
   {
      (void)strcat(line_buffer, " [BURST]");
   }
   else if (burst_counter > 1)
        {
           char tmp_buffer[12 + MAX_INT_LENGTH];

           (void)sprintf(tmp_buffer, " [BURST * %d]", burst_counter);
           (void)strcat(line_buffer, tmp_buffer);
        }
#endif
   (void)rec(transfer_log_fd, INFO_SIGN, "%s\n", line_buffer);

   /* Disconnect from remote port */
   scp1_quit();
   if ((fsa[db.fsa_pos].debug == YES) && (trans_db_log_fd != -1))
   {
      trans_db_log(INFO_SIGN, __FILE__, __LINE__,
                   "Disconnected from host %s.", db.hostname);
   }

   /*
    * Remove file directory, but only when all files have
    * been transmitted.
    */
   if ((files_to_send == files_send) || (files_to_send == 0))
   {
      if (rmdir(file_path) < 0)
      {
         system_log(ERROR_SIGN, __FILE__, __LINE__,
                    "Failed to remove directory <%s> : %s",
                    file_path, strerror(errno));
      }
   }
   else
   {
      system_log(WARN_SIGN, __FILE__, __LINE__,
                 "There are still %d files for <%s>. Will NOT remove this job!",
                 files_to_send - files_send, file_path);
   }
#endif /* _WITH_SCP1_SUPPORT */

   exitflag = 0;
   exit(TRANSFER_SUCCESS);
}


#ifdef _WITH_SCP1_SUPPORT
/*++++++++++++++++++++++++++++ sf_scp1_exit() +++++++++++++++++++++++++++*/
static void
sf_scp1_exit(void)
{
   int  fd;
   char sf_fin_fifo[MAX_PATH_LENGTH];

   reset_fsa((struct job *)&db, exitflag);

   if (file_name_buffer != NULL)
   {
      free(file_name_buffer);
   }
   if (file_size_buffer != NULL)
   {
      free(file_size_buffer);
   }

   (void)strcpy(sf_fin_fifo, p_work_dir);
   (void)strcat(sf_fin_fifo, FIFO_DIR);
   (void)strcat(sf_fin_fifo, SF_FIN_FIFO);
   if ((fd = open(sf_fin_fifo, O_RDWR)) == -1)
   {
      system_log(ERROR_SIGN, __FILE__, __LINE__,
                 "Could not open fifo <%s> : %s", sf_fin_fifo, strerror(errno));
   }
   else
   {
#ifdef _FIFO_DEBUG
      char  cmd[2];

      cmd[0] = ACKN; cmd[1] = '\0';
      show_fifo_data('W', "sf_fin", cmd, 1, __FILE__, __LINE__);
#endif
      /* Tell FD we are finished */
      if (write(fd, &db.my_pid, sizeof(pid_t)) != sizeof(pid_t))
      {
         system_log(WARN_SIGN, __FILE__, __LINE__,
                    "write() error : %s", strerror(errno));
      }
      (void)close(fd);
   }
   if (sys_log_fd != STDERR_FILENO)
   {
      (void)close(sys_log_fd);
   }

   return;
}


/*++++++++++++++++++++++++++++++ sig_pipe() +++++++++++++++++++++++++++++*/
static void
sig_pipe(int signo)
{
   reset_fsa((struct job *)&db, IS_FAULTY_VAR);
   msg_str[0] = '\0';
   trans_log(ERROR_SIGN, __FILE__, __LINE__,
             "Received SIGPIPE. Remote site has closed its socket.");

   exit(SIG_PIPE_ERROR);
}


/*++++++++++++++++++++++++++++++ sig_segv() +++++++++++++++++++++++++++++*/
static void
sig_segv(int signo)
{
   reset_fsa((struct job *)&db, IS_FAULTY_VAR);
   system_log(DEBUG_SIGN, __FILE__, __LINE__,
             "Aaarrrggh! Received SIGSEGV. Remove the programmer who wrote this!");
   abort();
}


/*++++++++++++++++++++++++++++++ sig_bus() ++++++++++++++++++++++++++++++*/
static void
sig_bus(int signo)
{
   reset_fsa((struct job *)&db, IS_FAULTY_VAR);
   system_log(DEBUG_SIGN, __FILE__, __LINE__, "Uuurrrggh! Received SIGBUS.");
   abort();
}


/*++++++++++++++++++++++++++++++ sig_kill() +++++++++++++++++++++++++++++*/
static void
sig_kill(int signo)
{
   reset_fsa((struct job *)&db, IS_FAULTY_VAR);
   exit(GOT_KILLED);
}


/*++++++++++++++++++++++++++++++ sig_exit() +++++++++++++++++++++++++++++*/
static void
sig_exit(int signo)
{
   reset_fsa((struct job *)&db, IS_FAULTY_VAR);
   exit(INCORRECT);
}
#endif /* _WITH_SCP1_SUPPORT */
