/*
 *  sf_ftp.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1995 - 1999 Deutscher Wetterdienst (DWD),
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
 **   sf_ftp - send files via FTP
 **
 ** SYNOPSIS
 **   sf_ftp [options] -m <message-file>
 **
 **   options
 **       --version               - Version
 **       -w directory            - the working directory of the
 **                                 AFD
 **       -j <process number>     - the process number under which this
 **                                 job is to be displayed
 **       -f                      - error message
 **       -t                      - toggle host
 **
 ** DESCRIPTION
 **   sf_ftp sends the given files to the defined recipient via FTP
 **   It does so by using it's own FTP-client.
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
 **   25.10.1995 H.Kiehl Created
 **   22.01.1997 H.Kiehl Include support for output logging.
 **   03.02.1997 H.Kiehl Appending file when only send partly and an
 **                      error occurred.
 **   04.03.1997 H.Kiehl Ignoring error when closing remote file if
 **                      file size is not larger than zero.
 **   08.05.1997 H.Kiehl Logging archive directory.
 **   29.08.1997 H.Kiehl Support for 'real' host names.
 **   12.04.1998 H.Kiehl Added DOS binary mode.
 *    26.04.1999 H.Kiehl Added option "no login".
 **
 */
DESCR__E_M1

#include <stdio.h>                     /* fprintf(), sprintf()           */
#include <string.h>                    /* strcpy(), strcat(), strcmp(),  */
                                       /* strerror()                     */
#include <stdlib.h>                    /* malloc(), free(), abort()      */
#include <ctype.h>                     /* isdigit()                      */
#ifdef _RADAR_CHECK
#include <time.h>
#endif /* _RADAR_CHECK */
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _OUTPUT_LOG
#include <sys/times.h>                 /* times(), struct tms            */
#endif
#include <fcntl.h>
#include <signal.h>                    /* signal()                       */
#include <unistd.h>                    /* remove(), close(), getpid()    */
#include <errno.h>
#include "fddefs.h"
#include "ftpdefs.h"
#include "version.h"
#ifdef _WITH_EUMETSAT_HEADERS
#include "eumetsat_header_defs.h"
#endif

/* Global variables */
int                        counter_fd,
                           no_of_hosts,   /* This variable is not used   */
                                          /* in this module.             */
                           rule_pos,
                           fsa_id,
                           fsa_fd = -1,
                           sys_log_fd = STDERR_FILENO,
                           transfer_log_fd = STDERR_FILENO,
                           trans_db_log_fd = STDERR_FILENO,
                           amg_flag = NO,
                           timeout_flag,
                           sigpipe_flag;
#ifndef _NO_MMAP
off_t                      fsa_size;
#endif
off_t                      *file_size_buffer = NULL;
long                       ftp_timeout;
char                       host_deleted = NO,
                           err_msg_dir[MAX_PATH_LENGTH],
                           *p_work_dir,
                           msg_str[MAX_RET_MSG_LENGTH],
                           tr_hostname[MAX_HOSTNAME_LENGTH + 1],
                           line_buffer[4096],
                           *file_name_buffer = NULL;
struct filetransfer_status *fsa;
struct job                 db;
struct rule                *rule;
#ifdef _DELETE_LOG
struct delete_log          dl;
#endif

/* Local functions */
static void sf_ftp_exit(void),
            sig_bus(int),
            sig_segv(int),
            sig_pipe(int),
            sig_kill(int),
            sig_exit(int);

/* #define _DEBUG_APPEND 1 */
/* #define _SIMULATE_SLOW_TRANSFER 1 */


/*$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ main() $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$*/
int
main(int argc, char *argv[])
{
#ifdef _VERIFY_FSA
   unsigned int     ui_variable;
#endif
   int              j,
                    fd,
                    status,
                    no_of_bytes,
                    loops,
                    rest,
                    append_file_number = -1,
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
   unsigned short   *ol_file_name_length;
   off_t            *ol_file_size = NULL;
   size_t           ol_size,
                    ol_real_size;
   clock_t          end_time,
                    start_time = 0,
                    *ol_transfer_time = NULL;
   struct tms       tmsdummy;
#endif
   off_t            *p_file_size_buffer,
                    append_offset = 0;
   char             *ptr,
                    *ascii_buffer = NULL,
                    *p_file_name_buffer,
#ifdef _BURST_MODE
                    search_for_files = NO,
#endif
                    append_count = 0,
                    *buffer,
                    initial_filename[MAX_FILENAME_LENGTH],
                    final_filename[MAX_FILENAME_LENGTH],
                    remote_filename[MAX_PATH_LENGTH],
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
      (void)rec(sys_log_fd, FATAL_SIGN, "sigaction() error : %s (%s %d)\n",
                strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }
#endif

   /* Do some cleanups when we exit */
   if (atexit(sf_ftp_exit) != 0)
   {
      (void)rec(sys_log_fd, FATAL_SIGN,
                "Could not register exit function : %s (%s %d)\n",
                strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }

   /* Initialise variables */
   p_work_dir = work_dir;
   init_sf(argc, argv, file_path, &blocksize, &files_to_send, FTP);
   p_db = &db;
   msg_str[0] = '\0';

   if ((signal(SIGINT, sig_kill) == SIG_ERR) ||
       (signal(SIGQUIT, sig_exit) == SIG_ERR) ||
       (signal(SIGTERM, sig_exit) == SIG_ERR) ||
       (signal(SIGSEGV, sig_segv) == SIG_ERR) ||
       (signal(SIGBUS, sig_bus) == SIG_ERR) ||
       (signal(SIGHUP, SIG_IGN) == SIG_ERR) ||
       (signal(SIGPIPE, sig_pipe) == SIG_ERR))
   {
      (void)rec(sys_log_fd, FATAL_SIGN, "signal() error : %s (%s %d)\n",
                strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }

   /* Set FTP timeout value */
   ftp_timeout = fsa[(int)db.position].transfer_timeout;

   /*
    * In ASCII-mode an extra buffer is needed to convert LF's
    * to CRLF. By creating this buffer the function ftp_write()
    * knows it has to send the data in ASCII-mode.
    */
   if ((db.transfer_mode == 'A') || (db.transfer_mode == 'D'))
   {
      if (db.transfer_mode == 'D')
      {
         db.transfer_mode = 'I';
      }
      if ((ascii_buffer = (char *)malloc((blocksize * 2))) == NULL)
      {
         (void)rec(sys_log_fd, ERROR_SIGN, "malloc() error : %s (%s %d)\n",
                   strerror(errno), __FILE__, __LINE__);
         exit(ALLOC_ERROR);
      }
   }

#ifdef _OUTPUT_LOG
   if (db.output_log == YES)
   {
      output_log_ptrs(&ol_fd,                /* File descriptor to fifo */
                      &ol_job_number,
                      &ol_data,              /* Pointer to buffer       */
                      &ol_file_name,
                      &ol_file_name_length,
                      &ol_file_size,
                      &ol_size,
                      &ol_transfer_time,
                      db.host_alias,
                      FTP);
   }
#endif

   sigpipe_flag = timeout_flag = OFF;

   /* Now determine the real hostname. */
   if (db.toggle_host == YES)
   {
      if (fsa[(int)db.position].host_toggle == HOST_ONE)
      {
         (void)strcpy(db.hostname,
                      fsa[(int)db.position].real_hostname[HOST_TWO - 1]);
      }
      else
      {
         (void)strcpy(db.hostname,
                      fsa[(int)db.position].real_hostname[HOST_ONE - 1]);
      }
   }
   else
   {
      (void)strcpy(db.hostname,
                   fsa[(int)db.position].real_hostname[(int)(fsa[(int)db.position].host_toggle - 1)]);
   }

   /* Connect to remote FTP-server */
   if ((status = ftp_connect(db.hostname, db.port)) != SUCCESS)
   {
      if (fsa[(int)db.position].debug == YES)
      {
         if (timeout_flag == OFF)
         {
            (void)rec(trans_db_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Could not connect to %s (%d). (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.hostname, status,
                      __FILE__, __LINE__);
            if (status != INCORRECT)
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, msg_str);
            }
         }
         else
         {
            (void)rec(trans_db_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Could not connect to %s due to timeout. (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.hostname, __FILE__, __LINE__);
         }
      }
      if (timeout_flag == OFF)
      {
         if (status != INCORRECT)
         {
            (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }

         /*
          * The ftp_connect() function wrote to the log file if status
          * is INCORRECT. Thus it is not necessary to do it here again.
          */
      }
      else
      {
         (void)rec(transfer_log_fd, ERROR_SIGN,
                   "%-*s[%d]: Failed to connect due to timeout. #%d (%s %d)\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname,
                   (int)db.job_no, db.job_id, __FILE__, __LINE__);
      }
      reset_fsa(p_db, YES, NO_OF_FILES_VAR | CONNECT_STATUS_VAR);
      exit(CONNECT_ERROR);
   }
   else
   {
      if (fsa[(int)db.position].debug == YES)
      {
         (void)rec(trans_db_log_fd, INFO_SIGN,
                   "%-*s[%d]: Connected. (%s %d)\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname,
                   (int)db.job_no, __FILE__, __LINE__);
         (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no, msg_str);
      }
   }

   if (db.special_flag & SECURE_FTP)
   {
      char password[128];

      /* Send 'anonymous' as user name */
      if ((status = ftp_user("anonymous")) != SUCCESS)
      {
         if (fsa[(int)db.position].debug == YES)
         {
            if (timeout_flag == OFF)
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to send user anonymous (%d). (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, status, __FILE__, __LINE__);
               (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, msg_str);
            }
            else
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to send user anonymous due to timeout. (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, __FILE__, __LINE__);
            }
         }
         if (timeout_flag == OFF)
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to send user anonymous (%d). #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, status,
                      db.job_id, __FILE__, __LINE__);
            (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
         else
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to send user anonymous due to timeout. #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.job_id, __FILE__, __LINE__);
         }
         (void)ftp_quit();
         reset_fsa(p_db, YES, NO_OF_FILES_VAR | CONNECT_STATUS_VAR);
         exit(USER_ERROR);
      }
      else
      {
         if (fsa[(int)db.position].debug == YES)
         {
            (void)rec(trans_db_log_fd, INFO_SIGN,
                      "%-*s[%d]: Entered user name anonymous. (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, __FILE__, __LINE__);
            (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
      }

      /* Send password user@hostname */
      get_user(password);
      if ((status = ftp_pass(password)) != SUCCESS)
      {
         if (fsa[(int)db.position].debug == YES)
         {
            if (timeout_flag == OFF)
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to send password for user anonymous (%d). (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, status, __FILE__, __LINE__);
               (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, msg_str);
            }
            else
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to send password for user anonymous due to timeout. (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, __FILE__, __LINE__);
            }
         }
         if (timeout_flag == OFF)
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to send password for user anonymous (%d). #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, status,
                      db.job_id, __FILE__, __LINE__);
            (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no, msg_str);
         }
         else
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to send password for user anonymous due to timeout. #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.job_id, __FILE__, __LINE__);
         }
         (void)ftp_quit();
         reset_fsa(p_db, YES, NO_OF_FILES_VAR | CONNECT_STATUS_VAR);
         exit(PASSWORD_ERROR);
      }
      else
      {
         if (fsa[(int)db.position].debug == YES)
         {
            (void)rec(trans_db_log_fd, INFO_SIGN,
                      "%-*s[%d]: Logged in as anonymous. (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, __FILE__, __LINE__);
            (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no, msg_str);
         }
      }
   } /* if (db.special_flag & SECURE_FTP) */

   if ((db.special_flag & NO_LOGIN) == 0)
   {
      /* Send user name */
      if (((status = ftp_user(db.user)) != SUCCESS) && (status != 230))
      {
         if (fsa[(int)db.position].debug == YES)
         {
            if (timeout_flag == OFF)
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to send user %s (%d). (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, db.user, status, __FILE__, __LINE__);
               (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, msg_str);
            }
            else
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to send user %s due to timeout. (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, db.user, __FILE__, __LINE__);
            }
         }
         if (timeout_flag == OFF)
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to send user %s (%d). #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.user, status,
                      db.job_id, __FILE__, __LINE__);
            (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no, msg_str);
         }
         else
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to send user %s due to timeout. #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.user,
                      db.job_id, __FILE__, __LINE__);
         }
         (void)ftp_quit();
         reset_fsa(p_db, YES, NO_OF_FILES_VAR | CONNECT_STATUS_VAR);
         exit(USER_ERROR);
      }
      else
      {
         if (fsa[(int)db.position].debug == YES)
         {
            (void)rec(trans_db_log_fd, INFO_SIGN,
                      "%-*s[%d]: Entered user name %s. (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.user, __FILE__, __LINE__);
            (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no, msg_str);
         }
      }

      /* Send password */
      if (status != 230)
      {
         if ((status = ftp_pass(db.password)) != SUCCESS)
         {
            if (fsa[(int)db.position].debug == YES)
            {
               if (timeout_flag == OFF)
               {
                  (void)rec(trans_db_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to send password for user %s (%d). (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, db.user, status, __FILE__, __LINE__);
                  (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, msg_str);
               }
               else
               {
                  (void)rec(trans_db_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to send password for user %s due to timeout. (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, db.user, __FILE__, __LINE__);
               }
            }
            if (timeout_flag == OFF)
            {
               (void)rec(transfer_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to send password for user %s (%d). #%d (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, db.user, status,
                         db.job_id, __FILE__, __LINE__);
               (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no, msg_str);
            }
            else
            {
               (void)rec(transfer_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to send password for user %s due to timeout. #%d (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, db.user,
                         db.job_id, __FILE__, __LINE__);
            }
            (void)ftp_quit();
            reset_fsa(p_db, YES, NO_OF_FILES_VAR | CONNECT_STATUS_VAR);
            exit(PASSWORD_ERROR);
         }
         else
         {
            if (fsa[(int)db.position].debug == YES)
            {
               (void)rec(trans_db_log_fd, INFO_SIGN,
                         "%-*s[%d]: Logged in as %s. (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, db.user, __FILE__, __LINE__);
               (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no, msg_str);
            }
         }
      } /* if (status != 230) */
   } /* if (!db.special_flag & NO_LOGIN) */

#ifdef _CHECK_BEFORE_EXIT
#endif

   /* Set transfer mode */
   if ((status = ftp_type(db.transfer_mode)) != SUCCESS)
   {
      if (fsa[(int)db.position].debug == YES)
      {
         if (timeout_flag == OFF)
         {
            (void)rec(trans_db_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to set transfer mode to %c (%d). (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.transfer_mode,
                      status, __FILE__, __LINE__);
            (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
         else
         {
            (void)rec(trans_db_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to set transfer mode to %c due to timeout. (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.transfer_mode, __FILE__, __LINE__);
         }
      }
      if (timeout_flag == OFF)
      {
         (void)rec(transfer_log_fd, ERROR_SIGN,
                   "%-*s[%d]: Failed to set transfer mode to %c (%d). #%d (%s %d)\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname,
                   (int)db.job_no, db.transfer_mode,
                   status, db.job_id, __FILE__, __LINE__);
         (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname,
                   (int)db.job_no, msg_str);
      }
      else
      {
         (void)rec(transfer_log_fd, ERROR_SIGN,
                   "%-*s[%d]: Failed to set transfer mode to %c due to timeout. #%d (%s %d)\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname,
                   (int)db.job_no, db.transfer_mode,
                   db.job_id, __FILE__, __LINE__);
      }
      (void)ftp_quit();
      reset_fsa(p_db, YES, NO_OF_FILES_VAR | CONNECT_STATUS_VAR);
      exit(TYPE_ERROR);
   }
   else
   {
      if (fsa[(int)db.position].debug == YES)
      {
         (void)rec(trans_db_log_fd, INFO_SIGN,
                   "%-*s[%d]: Changed transfer mode to %c. (%s %d)\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname,
                   (int)db.job_no, db.transfer_mode, __FILE__, __LINE__);
         (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no, msg_str);
      }
   }

   /* Change directory if necessary */
   if (db.target_dir[0] != '\0')
   {
      if ((status = ftp_cd(db.target_dir)) != SUCCESS)
      {
         if (fsa[(int)db.position].debug == YES)
         {
            if (timeout_flag == OFF)
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to change directory to %s (%d). (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, db.target_dir,
                         status, __FILE__, __LINE__);
               (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, msg_str);
            }
            else
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to change directory to %s due to timeout. (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, db.target_dir, __FILE__, __LINE__);
            }
         }
         if (timeout_flag == OFF)
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to change directory to %s (%d). #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.target_dir,
                      status, db.job_id, __FILE__, __LINE__);
            (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
         else
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to change directory to %s due to timeout. #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.target_dir,
                      db.job_id, __FILE__, __LINE__);
         }
         (void)ftp_quit();
         reset_fsa(p_db, YES, NO_OF_FILES_VAR | CONNECT_STATUS_VAR);
         exit(CHDIR_ERROR);
      }
      else
      {
         if (fsa[(int)db.position].debug == YES)
         {
            (void)rec(trans_db_log_fd, INFO_SIGN,
                      "%-*s[%d]: Changed directory to %s. (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, db.target_dir, __FILE__, __LINE__);
            (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
      }
   } /* if (db.target_dir[0] != '\0') */

   /* Inform FSA that we have finished connecting and */
   /* will now start to transfer data.                */
   if (host_deleted == NO)
   {
      lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
      rlock_region(fsa_fd, lock_offset);
      if (check_fsa() == YES)
      {
         if ((db.position = get_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
         {
            host_deleted = YES;
         }
         else
         {
            lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
            rlock_region(fsa_fd, lock_offset);
         }
      }
      if (host_deleted == NO)
      {
         fsa[(int)db.position].job_status[(int)db.job_no].connect_status = TRANSFER_ACTIVE;
         fsa[(int)db.position].job_status[(int)db.job_no].no_of_files = files_to_send;

         /* Number of connections */
         lock_region_w(fsa_fd, (char *)&fsa[(int)db.position].connections - (char *)fsa);
         fsa[(int)db.position].connections += 1;
         unlock_region(fsa_fd, (char *)&fsa[(int)db.position].connections - (char *)fsa);
         unlock_region(fsa_fd, lock_offset);
      }
   }

   /* If we send a lock file, do it now. */
   if (db.lock == LOCKFILE)
   {
      /* Create lock file on remote host */
      if ((status = ftp_data(LOCK_FILENAME, 0)) != SUCCESS)
      {
         if (fsa[(int)db.position].debug == YES)
         {
            if (timeout_flag == OFF)
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to send lock file %s (%d). (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, LOCK_FILENAME,
                         status, __FILE__, __LINE__);
               (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, msg_str);
            }
            else
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to send lock file %s due to timeout. (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, LOCK_FILENAME, __FILE__, __LINE__);
            }
         }
         if (timeout_flag == OFF)
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to send lock file %s (%d). #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, LOCK_FILENAME,
                      status, db.job_id, __FILE__, __LINE__);
            (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
         else
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to send lock file %s due to timeout. #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, LOCK_FILENAME,
                      db.job_id, __FILE__, __LINE__);
         }
         (void)ftp_quit();
         reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                              NO_OF_FILES_VAR | NO_OF_FILES_DONE_VAR |
                              FILE_SIZE_DONE_VAR |
                              FILE_SIZE_IN_USE_VAR |
                              FILE_SIZE_IN_USE_DONE_VAR));
         exit(WRITE_LOCK_ERROR);
      }
      else
      {
         if (fsa[(int)db.position].debug == YES)
         {
            (void)rec(trans_db_log_fd, INFO_SIGN,
                      "%-*s[%d]: Created lock file to %s. (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, LOCK_FILENAME, __FILE__, __LINE__);
            (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
      }

      /* Close remote lock file */
      if (ftp_close_data() != SUCCESS)
      {
         if (fsa[(int)db.position].debug == YES)
         {
            if (timeout_flag == OFF)
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to close lock file %s. (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, LOCK_FILENAME, __FILE__, __LINE__);
               (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, msg_str);
            }
            else
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to close lock file %s due to timeout. (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, LOCK_FILENAME, __FILE__, __LINE__);
            }
         }
         if (timeout_flag == OFF)
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to close lock file %s. #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, LOCK_FILENAME,
                      db.job_id, __FILE__, __LINE__);
            (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
         else
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to close lock file %s due to timeout. #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, LOCK_FILENAME,
                      db.job_id, __FILE__, __LINE__);
         }
         (void)ftp_quit();
         reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                              NO_OF_FILES_VAR |
                              NO_OF_FILES_DONE_VAR |
                              FILE_SIZE_DONE_VAR |
                              FILE_SIZE_IN_USE_VAR |
                              FILE_SIZE_IN_USE_DONE_VAR));
         exit(CLOSE_REMOTE_ERROR);
      }
   }

   /* Allocate buffer to read data from the source file. */
   if ((buffer = malloc(blocksize + 4)) == NULL)
   {
      (void)rec(sys_log_fd, ERROR_SIGN, "malloc() error : %s (%s %d)\n",
                strerror(errno), __FILE__, __LINE__);
      exit(ALLOC_ERROR);
   }

#ifdef _BURST_MODE
   do
   {
      if (search_for_files == YES)
      {
         off_t file_size_to_send = 0;

         lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
         rlock_region(fsa_fd, lock_offset);
         if (check_fsa() == YES)
         {
            if ((db.position = get_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
            {
               host_deleted = YES;
               lock_offset = -1;
            }
            else
            {
               lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
               rlock_region(fsa_fd, lock_offset);
               lock_region_w(fsa_fd, (char *)&fsa[(int)db.position].job_status[(int)db.job_no].job_id - (char *)fsa);
            }
         }
         if ((files_to_send = get_file_names(file_path, &file_size_to_send)) < 1)
         {
            /*
             * With age limit it can happen that files_to_send is zero.
             * Though very unlikely.
             */
            (void)rec(sys_log_fd, DEBUG_SIGN,
                      "Hmmm. Burst counter = %d and files_to_send = %d [%s]. How is this possible? AAarrgghhhhh.... (%s %d)\n",
                      fsa[(int)db.position].job_status[(int)db.job_no].burst_counter,
                      files_to_send, file_path,
                      __FILE__, __LINE__);
            fsa[(int)db.position].job_status[(int)db.job_no].burst_counter = 0;
            if (lock_offset != -1)
            {
               unlock_region(fsa_fd, lock_offset);
            }
            break;
         }
         burst_counter = fsa[(int)db.position].job_status[(int)db.job_no].burst_counter;
         unlock_region(fsa_fd, (char *)&fsa[(int)db.position].job_status[(int)db.job_no].job_id - (char *)fsa);

         total_files_send += files_send;

         /* Tell user we are bursting */
         if (host_deleted == NO)
         {
            if (check_fsa() == YES)
            {
               if ((db.position = get_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
               {
                  host_deleted = YES;
                  lock_offset = -1;
               }
               else
               {
                  lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                  rlock_region(fsa_fd, lock_offset);
               }
            }
            if (host_deleted == NO)
            {
               fsa[(int)db.position].job_status[(int)db.job_no].connect_status = FTP_BURST_TRANSFER_ACTIVE;
               fsa[(int)db.position].job_status[(int)db.job_no].no_of_files = fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done + files_to_send;
               fsa[(int)db.position].job_status[(int)db.job_no].file_size = fsa[(int)db.position].job_status[(int)db.job_no].file_size_done + file_size_to_send;
            }
            if (fsa[(int)db.position].debug == YES)
            {
               (void)rec(trans_db_log_fd, INFO_SIGN,
                         "%-*s[%d]: Bursting. (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, __FILE__, __LINE__);
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
         /* Write status to FSA? */
         if (host_deleted == NO)
         {
#ifdef _SAVE_FSA_WRITE
            lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
            rlock_region(fsa_fd, lock_offset);
#endif
            if (check_fsa() == YES)
            {
               if ((db.position = get_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
               {
                  host_deleted = YES;
               }
#ifdef _SAVE_FSA_WRITE
               else
               {
                  lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                  rlock_region(fsa_fd, lock_offset);
               }
#endif
            }
            if (host_deleted == NO)
            {
               if (fsa[(int)db.position].active_transfers > 1)
               {
                  int file_is_duplicate = NO;

                  lock_region_w(fsa_fd, (char *)&fsa[(int)db.position].job_status[(int)db.job_no].file_name_in_use[0] - (char *)fsa);

                  /*
                   * Check if this file is not currently being transfered!
                   */
                  for (j = 0; j < fsa[(int)db.position].allowed_transfers; j++)
                  {
                     if ((j != db.job_no) &&
                         (fsa[(int)db.position].job_status[j].job_id == fsa[(int)db.position].job_status[(int)db.job_no].job_id) &&
                         (strcmp(fsa[(int)db.position].job_status[j].file_name_in_use, p_file_name_buffer) == 0))
                     {
#ifdef _DELETE_LOG
                        int    prog_name_length;
                        size_t dl_real_size;

                        (void)strcpy(dl.file_name, p_file_name_buffer);
                        (void)sprintf(dl.host_name, "%-*s %x",
                                      MAX_HOSTNAME_LENGTH,
                                      fsa[(int)db.position].host_dsp_name,
                                      OTHER_DEL);
                        *dl.file_size = *p_file_size_buffer;
                        *dl.job_number = db.job_id;
                        *dl.file_name_length = strlen(p_file_name_buffer);
                        prog_name_length = sprintf((dl.file_name + *dl.file_name_length + 1),
                                                   "%s Duplicate file",
                                                   SEND_FILE_FTP);
                        dl_real_size = *dl.file_name_length + dl.size + prog_name_length;
                        if (write(dl.fd, dl.data, dl_real_size) != dl_real_size)
                        {
                           (void)rec(sys_log_fd, ERROR_SIGN,
                                     "write() error : %s (%s %d)\n",
                                     strerror(errno), __FILE__, __LINE__);
                        }
#endif /* _DELETE_LOG */
                        (void)sprintf(fullname, "%s/%s", file_path, p_file_name_buffer);
                        if (remove(fullname) == -1)
                        {
                           (void)rec(sys_log_fd, WARN_SIGN,
                                     "Failed to remove() duplicate file %s : %s (%s %d)\n",
                                     fullname, strerror(errno),
                                     __FILE__, __LINE__);
                        }
                        if (fsa[(int)db.position].debug == YES)
                        {
                           (void)rec(trans_db_log_fd, WARN_SIGN,
                                     "%-*s[%d]: File %s is currently transmitted by job %d. Will NOT transmit file. (%s %d)\n",
                                     MAX_HOSTNAME_LENGTH, tr_hostname,
                                     (int)db.job_no, p_file_name_buffer,
                                     j, __FILE__, __LINE__);
                        }
                        (void)rec(transfer_log_fd, WARN_SIGN,
                                  "%-*s[%d]: Trying to send duplicate file %s. Will NOT send file again! (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, p_file_name_buffer,
                                  __FILE__, __LINE__);

                        fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done++;

                        /* Total file counter */
                        lock_region_w(fsa_fd, (char *)&fsa[(int)db.position].total_file_counter - (char *)fsa);
                        fsa[(int)db.position].total_file_counter -= 1;
#ifdef _VERIFY_FSA
                        if (fsa[(int)db.position].total_file_counter < 0)
                        {
                           (void)rec(sys_log_fd, INFO_SIGN,
                                     "Total file counter for host %s less then zero. Correcting. (%s %d)\n",
                                     fsa[(int)db.position].host_dsp_name,
                                     __FILE__, __LINE__);
                           fsa[(int)db.position].total_file_counter = 0;
                        }
#endif

                        /* Total file size */
#ifdef _VERIFY_FSA
                        ui_variable = fsa[(int)db.position].total_file_size;
#endif
                        fsa[(int)db.position].total_file_size -= *p_file_size_buffer;
#ifdef _VERIFY_FSA
                        if (fsa[(int)db.position].total_file_size > ui_variable)
                        {
                           (void)rec(sys_log_fd, INFO_SIGN,
                                     "Total file size for host %s overflowed. Correcting. (%s %d)\n",
                                     fsa[(int)db.position].host_dsp_name, __FILE__, __LINE__);
                           fsa[(int)db.position].total_file_size = 0;
                        }
                        else if ((fsa[(int)db.position].total_file_counter == 0) &&
                                 (fsa[(int)db.position].total_file_size > 0))
                             {
                                (void)rec(sys_log_fd, INFO_SIGN,
                                          "fc for host %s is zero but fs is not zero. Correcting. (%s %d)\n",
                                          fsa[(int)db.position].host_dsp_name, __FILE__, __LINE__);
                                fsa[(int)db.position].total_file_size = 0;
                             }
#endif
                        unlock_region(fsa_fd, (char *)&fsa[(int)db.position].total_file_counter - (char *)fsa);

                        file_is_duplicate = YES;
                        p_file_name_buffer += MAX_FILENAME_LENGTH;
                        p_file_size_buffer++;
                        break;
                     }
                  } /* for (j = 0; j < allowed_transfers; j++) */

                  if (file_is_duplicate == NO)
                  {
                     fsa[(int)db.position].job_status[(int)db.job_no].file_size_in_use = *p_file_size_buffer;
                     (void)strcpy(fsa[(int)db.position].job_status[(int)db.job_no].file_name_in_use,
                                  p_file_name_buffer);
                     unlock_region(fsa_fd, (char *)&fsa[(int)db.position].job_status[(int)db.job_no].file_name_in_use[0] - (char *)fsa);
                  }
                  else
                  {
                     unlock_region(fsa_fd, (char *)&fsa[(int)db.position].job_status[(int)db.job_no].file_name_in_use[0] - (char *)fsa);
                     continue;
                  }
               }
               else
               {
                  fsa[(int)db.position].job_status[(int)db.job_no].file_size_in_use = *p_file_size_buffer;
                  (void)strcpy(fsa[(int)db.position].job_status[(int)db.job_no].file_name_in_use,
                               p_file_name_buffer);
               }
#ifdef _SAVE_FSA_WRITE
               unlock_region(fsa_fd, lock_offset);
#endif
            }
         }

         (void)strcpy(final_filename, p_file_name_buffer);
         (void)sprintf(fullname, "%s/%s", file_path, final_filename);

         /* Send file in dot notation? */
         if ((db.lock == DOT) || (db.lock == DOT_VMS))
         {
            (void)strcpy(initial_filename, db.lock_notation);
            (void)strcat(initial_filename, final_filename);
         }
         else
         {
            (void)strcpy(initial_filename, final_filename);
         }

         /*
          * Check if the file has not already been partly
          * transmitted. If so, lets first get the size of the
          * remote file, to append it.
          */
         append_offset = 0;
         append_file_number = -1;
         if ((fsa[(int)db.position].file_size_offset > -1) &&
             (db.no_of_restart_files > 0))
         {
            int ii;

            for (ii = 0; ii < db.no_of_restart_files; ii++)
            {
               if (strcmp(db.restart_file[ii], initial_filename) == 0)
               {
                  append_file_number = ii;
                  break;
               }
            }
            if (append_file_number != -1)
            {
               if ((status = ftp_list(initial_filename, line_buffer)) != SUCCESS)
               {
                  if (fsa[(int)db.position].debug == YES)
                  {
                     if (timeout_flag == OFF)
                     {
                        (void)rec(trans_db_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to list remote file %s (%d). (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename,
                                  status, __FILE__, __LINE__);
                        (void)rec(trans_db_log_fd, INFO_SIGN,
                                  "%-*s[%d]: %s\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, msg_str);
                     }
                     else
                     {
                        (void)rec(trans_db_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to list remote file %s due to timeout. (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename,
                                  __FILE__, __LINE__);
                        timeout_flag = OFF;
                     }
                  }
               }
               else
               {
                  int  space_count = 0;
                  char *p_end_line,
                       *ptr = line_buffer;

                  /*
                   * Cut out remote file size, from ls command.
                   */
                  p_end_line = line_buffer + strlen(line_buffer);
                  do
                  {
                     while ((*ptr != ' ') && (*ptr != '\t') &&
                            (ptr < p_end_line))
                     {
                        ptr++;
                     }
                     if ((*ptr == ' ') || (*ptr == '\t'))
                     {
                        space_count++;
                        while (((*ptr == ' ') || (*ptr == '\t')) &&
                               (ptr < p_end_line))
                        {
                           ptr++;
                        }
                     }
                     else
                     {
                        (void)rec(sys_log_fd, WARN_SIGN,
                                  "The <file size offset> for host %s is to large! (%s %d)\n",
                                  tr_hostname, __FILE__, __LINE__);
                        space_count = -1;
                        break;
                     }
                  } while (space_count != fsa[(int)db.position].file_size_offset);

                  if ((space_count > -1) && (space_count == fsa[(int)db.position].file_size_offset))
                  {
                     char *p_end = ptr;

                     while ((isdigit(*p_end) != 0) && (p_end < p_end_line))
                     {
                        p_end++;
                     }
                     *p_end = '\0';
                     append_offset = atoi(ptr);
                  }
               }
            } /* if (append_file_number != -1) */
         }

         if ((append_offset < *p_file_size_buffer) ||
             (*p_file_size_buffer == 0))
         {
            /* Open file on remote site */
            if ((status = ftp_data(initial_filename, append_offset)) != SUCCESS)
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  if (timeout_flag == OFF)
                  {
                     (void)rec(trans_db_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to open remote file %s (%d). (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, initial_filename,
                               status, __FILE__, __LINE__);
                     (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, msg_str);
                  }
                  else
                  {
                     (void)rec(trans_db_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to open remote file %s due to timeout. (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, initial_filename,
                               __FILE__, __LINE__);
                  }
               }
               (void)rec(transfer_log_fd, INFO_SIGN,
                         "%-*s[%d]: %d Bytes send in %d file(s).\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                         fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                         fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
               if (timeout_flag == OFF)
               {
                  (void)rec(transfer_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to open remote file %s (%d). #%d (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, initial_filename,
                            status, db.job_id, __FILE__, __LINE__);

                  /*
                   * If another error, ie not a remote error, has occurred
                   * it is of no interest what the remote sever has to say.
                   */
                  if (status != -1)
                  {
                     (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, msg_str);
                  }
               }
               else
               {
                  (void)rec(transfer_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to open remote file %s due to timeout. #%d (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, initial_filename,
                            db.job_id, __FILE__, __LINE__);
               }
               (void)ftp_quit();
               reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                    NO_OF_FILES_VAR |
                                    NO_OF_FILES_DONE_VAR |
                                    FILE_SIZE_DONE_VAR |
                                    FILE_SIZE_IN_USE_VAR |
                                    FILE_SIZE_IN_USE_DONE_VAR |
                                    FILE_NAME_IN_USE_VAR));
               exit(OPEN_REMOTE_ERROR);
            }
            else
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  (void)rec(trans_db_log_fd, INFO_SIGN,
                            "%-*s[%d]: Open remote file %s (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, initial_filename,
                            __FILE__, __LINE__);
                  (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, msg_str);
               }
            }

            /* Open local file */
            if ((fd = open(fullname, O_RDONLY)) == -1)
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  (void)rec(trans_db_log_fd, INFO_SIGN,
                            "%-*s[%d]: Failed to open local file %s (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, fullname, __FILE__, __LINE__);
               }
               (void)rec(transfer_log_fd, INFO_SIGN,
                         "%-*s[%d]: %d Bytes send in %d file(s).\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                         fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                         fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
               (void)ftp_close_data();
               (void)ftp_quit();
               reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                    NO_OF_FILES_VAR |
                                    NO_OF_FILES_DONE_VAR |
                                    FILE_SIZE_DONE_VAR |
                                    FILE_SIZE_IN_USE_VAR |
                                    FILE_SIZE_IN_USE_DONE_VAR |
                                    FILE_NAME_IN_USE_VAR));
               exit(OPEN_LOCAL_ERROR);
            }
            if (fsa[(int)db.position].debug == YES)
            {
               (void)rec(trans_db_log_fd, INFO_SIGN,
                         "%-*s[%d]: Open local file %s (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, fullname, __FILE__, __LINE__);
            }
            if (append_offset > 0)
            {
               if ((*p_file_size_buffer - append_offset) > 0)
               {
                  if (lseek(fd, append_offset, SEEK_SET) < 0)
                  {
                     append_offset = 0;
                     (void)rec(transfer_log_fd, WARN_SIGN,
                               "%-*s[%d]: Failed to seek() in %s (Ignoring append): %s (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, fullname,
                               strerror(errno), __FILE__, __LINE__);
                     if (fsa[(int)db.position].debug == YES)
                     {
                        (void)rec(trans_db_log_fd, WARN_SIGN,
                                  "%-*s[%d]: Failed to seek() in %s (Ignoring append): %s (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, fullname,
                                  strerror(errno), __FILE__, __LINE__);
                     }
                  }
                  else
                  {
                     append_count++;
                     if (fsa[(int)db.position].debug == YES)
                     {
                        (void)rec(trans_db_log_fd, INFO_SIGN,
                                  "%-*s[%d]: Appending file %s at %d. (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, fullname, append_offset,
                                  __FILE__, __LINE__);
                     }
                  }
               }
               else
               {
                  append_offset = 0;
               }
            }

#ifdef _OUTPUT_LOG
            if (db.output_log == YES)
            {
               start_time = times(&tmsdummy);
            }
#endif

#ifdef _WITH_EUMETSAT_HEADERS
            if ((db.special_flag & ADD_EUMETSAT_HEADER) &&
                (append_offset == 0) &&
                (db.special_ptr != NULL))
            {
               if (fstat(fd, &stat_buf) == -1)
               {
                  (void)rec(transfer_log_fd, DEBUG_SIGN,
                            "Hmmm. Failed to stat() %s : %s (%s %d)\n",
                            fullname, strerror(errno),
                            __FILE__, __LINE__);
               }
               else
               {
                  size_t header_length;
                  char   *p_header;

                  if ((p_header = create_eumetsat_header(db.special_ptr,
                                                         (unsigned char)db.special_ptr[4],
                                                         *p_file_size_buffer,
                                                         stat_buf.st_mtime,
                                                         &header_length)) != NULL)
                  {
                     if ((status = ftp_write(p_header, NULL, header_length)) != SUCCESS)
                     {
                        /*
                         * It could be that we have received a SIGPIPE
                         * signal. If this is the case there might be data
                         * from the remote site on the control connection.
                         * Try to read this data into the global variable
                         * 'msg_str'.
                         */
                        if (sigpipe_flag == ON)
                        {
                           (void)ftp_get_reply();
                        }
                        if (fsa[(int)db.position].debug == YES)
                        {
                           if (timeout_flag == OFF)
                           {
                              (void)rec(trans_db_log_fd, ERROR_SIGN,
                                        "%-*s[%d]: Failed to write to remote file %s (%s %d)\n",
                                        MAX_HOSTNAME_LENGTH, tr_hostname,
                                        (int)db.job_no, initial_filename, __FILE__, __LINE__);
                              (void)rec(trans_db_log_fd, INFO_SIGN,
                                        "%-*s[%d]: %s\n",
                                        MAX_HOSTNAME_LENGTH, tr_hostname,
                                        (int)db.job_no, msg_str);
                           }
                           else
                           {
                              (void)rec(trans_db_log_fd, ERROR_SIGN,
                                        "%-*s[%d]: Failed to write to remote file %s due to timeout. (%s %d)\n",
                                        MAX_HOSTNAME_LENGTH, tr_hostname,
                                        (int)db.job_no, initial_filename,
                                        __FILE__, __LINE__);
                           }
                        }
                        (void)rec(transfer_log_fd, INFO_SIGN,
                                  "%-*s[%d]: %d Bytes send in %d file(s).\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                                  fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                                  fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
                        if (timeout_flag == OFF)
                        {
                           (void)rec(transfer_log_fd, ERROR_SIGN,
                                     "%-*s[%d]: Failed to write to remote file %s #%d (%s %d)\n",
                                     MAX_HOSTNAME_LENGTH, tr_hostname,
                                     (int)db.job_no, initial_filename,
                                     db.job_id, __FILE__, __LINE__);
                           (void)rec(transfer_log_fd, ERROR_SIGN,
                                     "%-*s[%d]: %s\n",
                                     MAX_HOSTNAME_LENGTH, tr_hostname,
                                     (int)db.job_no, msg_str);
                           (void)ftp_close_data();
                        }
                        else
                        {
                           (void)rec(transfer_log_fd, ERROR_SIGN,
                                     "%-*s[%d]: Failed to write to remote file %s due to timeout. #%d (%s %d)\n",
                                     MAX_HOSTNAME_LENGTH, tr_hostname,
                                     (int)db.job_no, initial_filename,
                                     db.job_id, __FILE__, __LINE__);
                        }
                        if (status == EPIPE)
                        {
                           /*
                            * When pipe is broken no nead to send a QUIT
                            * to the remote side since the connection has
                            * already been closed by the remote side.
                            */
                           (void)rec(transfer_log_fd, DEBUG_SIGN,
                                     "%-*s[%d]: Hmm. Pipe is broken. Will NOT send a QUIT. (%s %d)\n",
                                     MAX_HOSTNAME_LENGTH, tr_hostname,
                                     (int)db.job_no, __FILE__, __LINE__);
                        }
                        else
                        {
                           (void)ftp_quit();
                        }
                        reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                             NO_OF_FILES_VAR |
                                             NO_OF_FILES_DONE_VAR |
                                             FILE_SIZE_DONE_VAR |
                                             FILE_SIZE_IN_USE_VAR |
                                             FILE_SIZE_IN_USE_DONE_VAR |
                                             FILE_NAME_IN_USE_VAR));
                        exit(WRITE_REMOTE_ERROR);
                     }
                     if (host_deleted == NO)
                     {
#ifdef _SAVE_FSA_WRITE
                        lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                        rlock_region(fsa_fd, lock_offset);
#endif
                        if (check_fsa() == YES)
                        {
                           if ((db.position = get_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
                           {
                              host_deleted = YES;
                           }
#ifdef _SAVE_FSA_WRITE
                           else
                           {
                              lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                              rlock_region(fsa_fd, lock_offset);
                           }
#endif
                        }
                        if (host_deleted == NO)
                        {
                           fsa[(int)db.position].job_status[(int)db.job_no].file_size_done += header_length;
                           fsa[(int)db.position].job_status[(int)db.job_no].bytes_send += header_length;
#ifdef _SAVE_FSA_WRITE
                           unlock_region(fsa_fd, lock_offset);
#endif
                        }
                     }
                     free(p_header);
                  }
               }
            }
#endif
            if ((db.special_flag & FILE_NAME_IS_HEADER) &&
                (append_offset == 0))
            {
               int  header_length;
               char *ptr = p_file_name_buffer;

               buffer[0] = 1; /* SOH */
               buffer[1] = '\015'; /* CR */
               buffer[2] = '\015'; /* CR */
               buffer[3] = '\012'; /* LF */
               header_length = 4;

               for (;;)
               {
                  while ((*ptr != '_') && (*ptr != '-') &&
                         (*ptr != ' ') && (*ptr != '\0'))
                  {
                     buffer[header_length] = *ptr;
                     header_length++; ptr++;
                  }
                  if (*ptr == '\0')
                  {
                     break;
                  }
                  else
                  {
                     buffer[header_length] = ' ';
                     header_length++; ptr++;
                  }
               }
               buffer[header_length] = '\015'; /* CR */
               buffer[header_length + 1] = '\015'; /* CR */
               buffer[header_length + 2] = '\012'; /* LF */
               header_length += 3;

               if ((status = ftp_write(buffer, ascii_buffer, header_length)) != SUCCESS)
               {
                  /*
                   * It could be that we have received a SIGPIPE
                   * signal. If this is the case there might be data
                   * from the remote site on the control connection.
                   * Try to read this data into the global variable
                   * 'msg_str'.
                   */
                  if (sigpipe_flag == ON)
                  {
                     (void)ftp_get_reply();
                  }
                  if (fsa[(int)db.position].debug == YES)
                  {
                     if (timeout_flag == OFF)
                     {
                        (void)rec(trans_db_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to write to remote file %s (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename, __FILE__, __LINE__);
                        (void)rec(trans_db_log_fd, INFO_SIGN,
                                  "%-*s[%d]: %s\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, msg_str);
                     }
                     else
                     {
                        (void)rec(trans_db_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to write to remote file %s due to timeout. (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename,
                                  __FILE__, __LINE__);
                     }
                  }
                  (void)rec(transfer_log_fd, INFO_SIGN,
                            "%-*s[%d]: %d Bytes send in %d file(s).\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                            fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                            fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
                  if (timeout_flag == OFF)
                  {
                     (void)rec(transfer_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to write to remote file %s #%d (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, initial_filename,
                               db.job_id, __FILE__, __LINE__);
                     (void)rec(transfer_log_fd, ERROR_SIGN,
                               "%-*s[%d]: %s\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, msg_str);
                     (void)ftp_close_data();
                  }
                  else
                  {
                     (void)rec(transfer_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to write to remote file %s due to timeout. #%d (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, initial_filename,
                               db.job_id, __FILE__, __LINE__);
                  }
                  if (status == EPIPE)
                  {
                     /*
                      * When pipe is broken no nead to send a QUIT
                      * to the remote side since the connection has
                      * already been closed by the remote side.
                      */
                     (void)rec(transfer_log_fd, DEBUG_SIGN,
                               "%-*s[%d]: Hmm. Pipe is broken. Will NOT send a QUIT. (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, __FILE__, __LINE__);
                  }
                  else
                  {
                     (void)ftp_quit();
                  }
                  reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                       NO_OF_FILES_VAR |
                                       NO_OF_FILES_DONE_VAR |
                                       FILE_SIZE_DONE_VAR |
                                       FILE_SIZE_IN_USE_VAR |
                                       FILE_SIZE_IN_USE_DONE_VAR |
                                       FILE_NAME_IN_USE_VAR));
                  exit(WRITE_REMOTE_ERROR);
               }
               if (host_deleted == NO)
               {
#ifdef _SAVE_FSA_WRITE
                  lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                  rlock_region(fsa_fd, lock_offset);
#endif
                  if (check_fsa() == YES)
                  {
                     if ((db.position = get_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
                     {
                        host_deleted = YES;
                     }
#ifdef _SAVE_FSA_WRITE
                     else
                     {
                        lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                        rlock_region(fsa_fd, lock_offset);
                     }
#endif
                  }
                  if (host_deleted == NO)
                  {
                     fsa[(int)db.position].job_status[(int)db.job_no].file_size_done += header_length;
                     fsa[(int)db.position].job_status[(int)db.job_no].bytes_send += header_length;
#ifdef _SAVE_FSA_WRITE
                     unlock_region(fsa_fd, lock_offset);
#endif
                  }
               }
            }

            /* Read (local) and write (remote) file */
            no_of_bytes = 0;
            loops = (*p_file_size_buffer - append_offset) / blocksize;
            rest = (*p_file_size_buffer - append_offset) % blocksize;

            for (;;)
            {
               for (j = 0; j < loops; j++)
               {
#ifdef _SIMULATE_SLOW_TRANSFER
                  (void)sleep(2);
#endif
                  if (read(fd, buffer, blocksize) != blocksize)
                  {
                     if (fsa[(int)db.position].debug == YES)
                     {
                        (void)rec(trans_db_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Could not read local file %s : %s (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, fullname,
                                  strerror(errno), __FILE__, __LINE__);
                     }
                     (void)rec(transfer_log_fd, INFO_SIGN,
                               "%-*s[%d]: %d Bytes send in %d file(s).\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                               fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                               fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
                     (void)ftp_close_data();
                     (void)ftp_quit();
                     reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                          NO_OF_FILES_VAR |
                                          FILE_SIZE_DONE_VAR |
                                          FILE_SIZE_IN_USE_VAR |
                                          FILE_SIZE_IN_USE_DONE_VAR |
                                          FILE_NAME_IN_USE_VAR));
                     if ((fsa[(int)db.position].file_size_offset > -1) &&
                         (append_offset == 0) &&
                         ((j * blocksize) > MAX_SEND_BEFORE_APPEND))
                     {
                        log_append(db.job_id, initial_filename);
                     }
                     exit(READ_LOCAL_ERROR);
                  }
#ifdef _DEBUG_APPEND
                  if (((status = ftp_write(buffer, ascii_buffer, blocksize)) != SUCCESS) ||
                      (fsa[(int)db.position].job_status[(int)db.job_no].file_size_done > MAX_SEND_BEFORE_APPEND))
#else
                  if ((status = ftp_write(buffer, ascii_buffer, blocksize)) != SUCCESS)
#endif
                  {
                     /*
                      * It could be that we have received a SIGPIPE
                      * signal. If this is the case there might be data
                      * from the remote site on the control connection.
                      * Try to read this data into the global variable
                      * 'msg_str'.
                      */
                     if (sigpipe_flag == ON)
                     {
                        (void)ftp_get_reply();
                     }
                     if (fsa[(int)db.position].debug == YES)
                     {
                        if (timeout_flag == OFF)
                        {
                           (void)rec(trans_db_log_fd, ERROR_SIGN,
                                     "%-*s[%d]: Failed to write to remote file %s (%s %d)\n",
                                     MAX_HOSTNAME_LENGTH, tr_hostname,
                                     (int)db.job_no, initial_filename, __FILE__, __LINE__);
                           (void)rec(trans_db_log_fd, INFO_SIGN,
                                     "%-*s[%d]: %s\n",
                                     MAX_HOSTNAME_LENGTH, tr_hostname,
                                     (int)db.job_no, msg_str);
                        }
                        else
                        {
                           (void)rec(trans_db_log_fd, ERROR_SIGN,
                                     "%-*s[%d]: Failed to write to remote file %s due to timeout. (%s %d)\n",
                                     MAX_HOSTNAME_LENGTH, tr_hostname,
                                     (int)db.job_no, initial_filename,
                                     __FILE__, __LINE__);
                        }
                     }
                     (void)rec(transfer_log_fd, INFO_SIGN,
                               "%-*s[%d]: %d Bytes send in %d file(s).\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                               fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                               fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
                     if (timeout_flag == OFF)
                     {
                        (void)rec(transfer_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to write to remote file %s #%d (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename,
                                  db.job_id, __FILE__, __LINE__);
                        (void)rec(transfer_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: %s\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, msg_str);
                        (void)ftp_close_data();
                     }
                     else
                     {
                        (void)rec(transfer_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to write to remote file %s due to timeout. #%d (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename,
                                  db.job_id, __FILE__, __LINE__);
                     }
                     if (status == EPIPE)
                     {
                        /*
                         * When pipe is broken no nead to send a QUIT
                         * to the remote side since the connection has
                         * already been closed by the remote side.
                         */
                        (void)rec(transfer_log_fd, DEBUG_SIGN,
                                  "%-*s[%d]: Hmm. Pipe is broken. Will NOT send a QUIT. (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, __FILE__, __LINE__);
                     }
                     else
                     {
                        (void)ftp_quit();
                     }
                     reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                          NO_OF_FILES_VAR |
                                          NO_OF_FILES_DONE_VAR |
                                          FILE_SIZE_DONE_VAR |
                                          FILE_SIZE_IN_USE_VAR |
                                          FILE_SIZE_IN_USE_DONE_VAR |
                                          FILE_NAME_IN_USE_VAR));
                     if ((fsa[(int)db.position].file_size_offset > -1) &&
                         (append_offset == 0) &&
                         ((j * blocksize) > MAX_SEND_BEFORE_APPEND))
                     {
                        log_append(db.job_id, initial_filename);
                     }
                     exit(WRITE_REMOTE_ERROR);
                  }

                  no_of_bytes += blocksize;

                  if (host_deleted == NO)
                  {
#ifdef _SAVE_FSA_WRITE
                     lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                     rlock_region(fsa_fd, lock_offset);
#endif
                     if (check_fsa() == YES)
                     {
                        if ((db.position = get_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
                        {
                           host_deleted = YES;
                        }
#ifdef _SAVE_FSA_WRITE
                        else
                        {
                           lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                           rlock_region(fsa_fd, lock_offset);
                        }
#endif
                     }
                     if (host_deleted == NO)
                     {
                        fsa[(int)db.position].job_status[(int)db.job_no].file_size_in_use_done = no_of_bytes;
                        fsa[(int)db.position].job_status[(int)db.job_no].file_size_done += blocksize;
                        fsa[(int)db.position].job_status[(int)db.job_no].bytes_send += blocksize;
#ifdef _SAVE_FSA_WRITE
                        unlock_region(fsa_fd, lock_offset);
#endif
                     }
                  }
               } /* for (j = 0; j < loops; j++) */
               if (rest > 0)
               {
                  int end_length = 0;

                  if (read(fd, buffer, rest) != rest)
                  {
                     if (fsa[(int)db.position].debug == YES)
                     {
                        (void)rec(trans_db_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Could not read local file %s : %s (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, fullname,
                                  strerror(errno), __FILE__, __LINE__);
                     }
                     (void)rec(transfer_log_fd, INFO_SIGN,
                               "%-*s[%d]: %d Bytes send in %d file(s).\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                               fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                               fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
                     (void)ftp_close_data();
                     (void)ftp_quit();
                     reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                          NO_OF_FILES_VAR |
                                          NO_OF_FILES_DONE_VAR |
                                          FILE_SIZE_DONE_VAR |
                                          FILE_SIZE_IN_USE_VAR |
                                          FILE_SIZE_IN_USE_DONE_VAR |
                                          FILE_NAME_IN_USE_VAR));
                     if ((fsa[(int)db.position].file_size_offset > -1) &&
                         (append_offset == 0) &&
                         ((loops * blocksize) > MAX_SEND_BEFORE_APPEND))
                     {
                        log_append(db.job_id, initial_filename);
                     }
                     exit(READ_LOCAL_ERROR);
                  }
                  if (db.special_flag & FILE_NAME_IS_HEADER)
                  {
                     buffer[rest] = '\015';
                     buffer[rest + 1] = '\015';
                     buffer[rest + 2] = '\012';
                     buffer[rest + 3] = 3;  /* ETX */
                     end_length = 4;
                  }
                  if ((status = ftp_write(buffer, ascii_buffer, rest + end_length)) != SUCCESS)
                  {
                     /*
                      * It could be that we have received a SIGPIPE
                      * signal. If this is the case there might be data
                      * from the remote site on the control connection.
                      * Try to read this data into the global variable
                      * 'msg_str'.
                      */
                     if (sigpipe_flag == ON)
                     {
                        (void)ftp_get_reply();
                     }
                     if (fsa[(int)db.position].debug == YES)
                     {
                        if (timeout_flag == OFF)
                        {
                           (void)rec(trans_db_log_fd, ERROR_SIGN,
                                     "%-*s[%d]: Failed to write to remote file %s (%s %d)\n",
                                     MAX_HOSTNAME_LENGTH, tr_hostname,
                                     (int)db.job_no, initial_filename,
                                     __FILE__, __LINE__);
                           (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                                     MAX_HOSTNAME_LENGTH, tr_hostname,
                                     (int)db.job_no, msg_str);
                        }
                        else
                        {
                           (void)rec(trans_db_log_fd, ERROR_SIGN,
                                     "%-*s[%d]: Failed to write to remote file %s due to timeout. (%s %d)\n",
                                     MAX_HOSTNAME_LENGTH, tr_hostname,
                                     (int)db.job_no, initial_filename,
                                     __FILE__, __LINE__);
                        }
                     }
                     (void)rec(transfer_log_fd, INFO_SIGN,
                               "%-*s[%d]: %d Bytes send in %d file(s).\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                               fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                               fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
                     if (timeout_flag == OFF)
                     {
                        (void)rec(transfer_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to write to remote file %s #%d (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename,
                                  db.job_id, __FILE__, __LINE__);
                        (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, msg_str);
                        (void)ftp_close_data();
                     }
                     else
                     {
                        (void)rec(transfer_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to write to remote file %s due to timeout. #%d (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename,
                                  db.job_id, __FILE__, __LINE__);
                     }
                     if (status == EPIPE)
                     {
                        /*
                         * When pipe is broken no nead to send a QUIT
                         * to the remote side since the connection has
                         * already been closed by the remote side.
                         */
                        (void)rec(transfer_log_fd, DEBUG_SIGN,
                                  "%-*s[%d]: Hmm. Pipe is broken. Will NOT send a QUIT. (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, __FILE__, __LINE__);
                     }
                     else
                     {
                        (void)ftp_quit();
                     }
                     reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                          NO_OF_FILES_VAR |
                                          NO_OF_FILES_DONE_VAR |
                                          FILE_SIZE_DONE_VAR |
                                          FILE_SIZE_IN_USE_VAR |
                                          FILE_SIZE_IN_USE_DONE_VAR |
                                          FILE_NAME_IN_USE_VAR));
                     if ((fsa[(int)db.position].file_size_offset > -1) &&
                         (append_offset == 0) &&
                         ((loops * blocksize) > MAX_SEND_BEFORE_APPEND))
                     {
                        log_append(db.job_id, initial_filename);
                     }
                     exit(WRITE_REMOTE_ERROR);
                  }

                  no_of_bytes += rest + end_length;

                  if (host_deleted == NO)
                  {
#ifdef _SAVE_FSA_WRITE
                     lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                     rlock_region(fsa_fd, lock_offset);
#endif
                     if (check_fsa() == YES)
                     {
                        if ((db.position = get_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
                        {
                           host_deleted = YES;
                        }
#ifdef _SAVE_FSA_WRITE
                        else
                        {
                           lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                           rlock_region(fsa_fd, lock_offset);
                        }
#endif
                     }
                     if (host_deleted == NO)
                     {
                        fsa[(int)db.position].job_status[(int)db.job_no].file_size_in_use_done = no_of_bytes;
                        fsa[(int)db.position].job_status[(int)db.job_no].file_size_done += rest + end_length;
                        fsa[(int)db.position].job_status[(int)db.job_no].bytes_send += rest + end_length;
#ifdef _SAVE_FSA_WRITE
                        unlock_region(fsa_fd, lock_offset);
#endif
                     }
                  }
               } /* if (rest > 0) */
               else if ((rest == 0) &&
                        (db.special_flag & FILE_NAME_IS_HEADER))
                    {
                       buffer[0] = '\015';
                       buffer[1] = '\015';
                       buffer[2] = '\012';
                       buffer[3] = 3;  /* ETX */
                       if ((status = ftp_write(buffer, ascii_buffer, 4)) != SUCCESS)
                       {
                          /*
                           * It could be that we have received a SIGPIPE
                           * signal. If this is the case there might be data
                           * from the remote site on the control connection.
                           * Try to read this data into the global variable
                           * 'msg_str'.
                           */
                          if (sigpipe_flag == ON)
                          {
                             (void)ftp_get_reply();
                          }
                          if (fsa[(int)db.position].debug == YES)
                          {
                             if (timeout_flag == OFF)
                             {
                                (void)rec(trans_db_log_fd, ERROR_SIGN,
                                          "%-*s[%d]: Failed to write to remote file %s (%s %d)\n",
                                          MAX_HOSTNAME_LENGTH, tr_hostname,
                                          (int)db.job_no, initial_filename,
                                          __FILE__, __LINE__);
                                (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                                          MAX_HOSTNAME_LENGTH, tr_hostname,
                                          (int)db.job_no, msg_str);
                             }
                             else
                             {
                                (void)rec(trans_db_log_fd, ERROR_SIGN,
                                          "%-*s[%d]: Failed to write to remote file %s due to timeout. (%s %d)\n",
                                          MAX_HOSTNAME_LENGTH, tr_hostname,
                                          (int)db.job_no, initial_filename,
                                          __FILE__, __LINE__);
                             }
                          }
                          (void)rec(transfer_log_fd, INFO_SIGN,
                                    "%-*s[%d]: %d Bytes send in %d file(s).\n",
                                    MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                                    fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                                    fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
                          if (timeout_flag == OFF)
                          {
                             (void)rec(transfer_log_fd, ERROR_SIGN,
                                       "%-*s[%d]: Failed to write to remote file %s #%d (%s %d)\n",
                                       MAX_HOSTNAME_LENGTH, tr_hostname,
                                       (int)db.job_no, initial_filename,
                                       db.job_id, __FILE__, __LINE__);
                             (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                                       MAX_HOSTNAME_LENGTH, tr_hostname,
                                       (int)db.job_no, msg_str);
                             (void)ftp_close_data();
                          }
                          else
                          {
                             (void)rec(transfer_log_fd, ERROR_SIGN,
                                       "%-*s[%d]: Failed to write to remote file %s due to timeout. #%d (%s %d)\n",
                                       MAX_HOSTNAME_LENGTH, tr_hostname,
                                       (int)db.job_no, initial_filename,
                                       db.job_id, __FILE__, __LINE__);
                          }
                          if (status == EPIPE)
                          {
                             /*
                              * When pipe is broken no nead to send a QUIT
                              * to the remote side since the connection has
                              * already been closed by the remote side.
                              */
                             (void)rec(transfer_log_fd, DEBUG_SIGN,
                                       "%-*s[%d]: Hmm. Pipe is broken. Will NOT send a QUIT. (%s %d)\n",
                                       MAX_HOSTNAME_LENGTH, tr_hostname,
                                       (int)db.job_no, __FILE__, __LINE__);
                          }
                          else
                          {
                             (void)ftp_quit();
                          }
                          reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                               NO_OF_FILES_VAR |
                                               NO_OF_FILES_DONE_VAR |
                                               FILE_SIZE_DONE_VAR |
                                               FILE_SIZE_IN_USE_VAR |
                                               FILE_SIZE_IN_USE_DONE_VAR |
                                               FILE_NAME_IN_USE_VAR));
                          if ((fsa[(int)db.position].file_size_offset > -1) &&
                              (append_offset == 0) &&
                              (((loops * blocksize) + rest) > MAX_SEND_BEFORE_APPEND))
                          {
                             log_append(db.job_id, initial_filename);
                          }
                          exit(WRITE_REMOTE_ERROR);
                       }

                       if (host_deleted == NO)
                       {
#ifdef _SAVE_FSA_WRITE
                          lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                          rlock_region(fsa_fd, lock_offset);
#endif
                          if (check_fsa() == YES)
                          {
                             if ((db.position = get_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
                             {
                                host_deleted = YES;
                             }
#ifdef _SAVE_FSA_WRITE
                             else
                             {
                                lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                                rlock_region(fsa_fd, lock_offset);
                             }
#endif
                          }
                          if (host_deleted == NO)
                          {
                             fsa[(int)db.position].job_status[(int)db.job_no].file_size_done += 4;
                             fsa[(int)db.position].job_status[(int)db.job_no].bytes_send += 4;
#ifdef _SAVE_FSA_WRITE
                             unlock_region(fsa_fd, lock_offset);
#endif
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
                  (void)rec(transfer_log_fd, DEBUG_SIGN,
                            "Hmmm. Failed to stat() %s : %s (%s %d)\n",
                            fullname, strerror(errno),
                            __FILE__, __LINE__);
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
                     (void)rec(sys_log_fd, WARN_SIGN,
                               "File %s for host %s was DEFINITELY NOT send in dot notation. (%s %d)\n",
                               final_filename, fsa[(int)db.position].host_dsp_name,
                               __FILE__, __LINE__);
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
               (void)rec(transfer_log_fd, WARN_SIGN,
                         "%-*s[%d]: Failed to close() local file %s : %s (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                         final_filename, strerror(errno), __FILE__, __LINE__);
               /*
                * Since we usually do not send more then 100 files and
                * sf_ftp() will exit(), there is no point in stopping
                * the transmission.
                */
            }

            /* Close remote file */
            if ((status = ftp_close_data()) != SUCCESS)
            {
               /*
                * Closing files that have zero length is not possible
                * on some systems. So if this is the case lets not count
                * this as an error. Just ignore it, but send a message in
                * the transfer log, so the user sees that he is trying
                * to send files with zero length.
                */
               if ((*p_file_size_buffer > 0) || (timeout_flag == ON))
               {
                  if (fsa[(int)db.position].debug == YES)
                  {
                     if (timeout_flag == OFF)
                     {
                        (void)rec(trans_db_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to close remote file %s (%d). (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename,
                                  status, __FILE__, __LINE__);
                        (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  db.job_no, msg_str);
                     }
                     else
                     {
                        (void)rec(trans_db_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to close remote file %s due to timeout. (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename,
                                  __FILE__, __LINE__);
                     }
                  }
                  (void)rec(transfer_log_fd, INFO_SIGN,
                            "%-*s[%d]: %d Bytes send in %d file(s).\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                            fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                            fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
                  if (timeout_flag == OFF)
                  {
                     (void)rec(transfer_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to close remote file %s (%d). #%d (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, initial_filename,
                               status, db.job_id, __FILE__, __LINE__);
                     (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               db.job_no, msg_str);
                  }
                  else
                  {
                     (void)rec(transfer_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to close remote file %s due to timeout. #%d (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, initial_filename,
                               db.job_id, __FILE__, __LINE__);
                  }
                  (void)ftp_quit();
                  reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                       NO_OF_FILES_VAR |
                                       NO_OF_FILES_DONE_VAR |
                                       FILE_SIZE_DONE_VAR |
                                       FILE_SIZE_IN_USE_VAR |
                                       FILE_SIZE_IN_USE_DONE_VAR |
                                       FILE_NAME_IN_USE_VAR));
                  if ((fsa[(int)db.position].file_size_offset > -1) &&
                      (append_offset == 0) &&
                      (*p_file_size_buffer > MAX_SEND_BEFORE_APPEND))
                  {                                                  
                     log_append(db.job_id, initial_filename);
                  }
                  exit(CLOSE_REMOTE_ERROR);
               }
               else
               {
                  if (fsa[(int)db.position].debug == YES)
                  {
                     (void)rec(trans_db_log_fd, WARN_SIGN,
                               "%-*s[%d]: Failed to close remote file %s (%d). Ignoring since file size is %d. (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, initial_filename, status,
                               *p_file_size_buffer, __FILE__, __LINE__);
                     (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, msg_str);
                  }
                  (void)rec(transfer_log_fd, WARN_SIGN,
                            "%-*s[%d]: Failed to close remote file %s. Ignoring since file size is %d. (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, initial_filename,
                            *p_file_size_buffer, __FILE__, __LINE__);
               }
            }
            else
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  (void)rec(trans_db_log_fd, INFO_SIGN,
                            "%-*s[%d]: Closed remote file %s (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, initial_filename,
                            __FILE__, __LINE__);
                  (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, msg_str);
               }
            }

            if (fsa[(int)db.position].debug == YES)
            {
               if ((status = ftp_list(initial_filename, line_buffer)) != SUCCESS)
               {
                  if (fsa[(int)db.position].debug == YES)
                  {
                     if (timeout_flag == OFF)
                     {
                        (void)rec(trans_db_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to list remote file %s (%d). (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename,
                                  status, __FILE__, __LINE__);
                        (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, msg_str);
                     }
                     else
                     {
                        (void)rec(trans_db_log_fd, ERROR_SIGN,
                                  "%-*s[%d]: Failed to list remote file %s due to timeout. (%s %d)\n",
                                  MAX_HOSTNAME_LENGTH, tr_hostname,
                                  (int)db.job_no, initial_filename,
                                  __FILE__, __LINE__);
                        timeout_flag = OFF;
                     }
                  }
               }
               else
               {
                  (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, line_buffer);
                  (void)rec(trans_db_log_fd, INFO_SIGN,
                            "%-*s[%d]: Local file size of %s is %d (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, final_filename,
                            (int)stat_buf.st_size, __FILE__, __LINE__);
               }
            }
         } /* if (append_offset < p_file_size_buffer) */

         /* If we used dot notation, don't forget to rename */
         if ((db.lock == DOT) || (db.lock == DOT_VMS) ||
             (db.trans_rename_rule[0] != '\0'))
         {
            remote_filename[0] = '\0';
            if (db.lock == DOT_VMS)
            {
               (void)strcat(final_filename, DOT_NOTATION);
            }
            if (db.trans_rename_rule[0] != '\0')
            {
               register int k;

               for (k = 0; k < rule[rule_pos].no_of_rules; k++)
               {
                  if (filter(rule[rule_pos].filter[k], final_filename) == 0)
                  {
                     change_name(final_filename,
                                 rule[rule_pos].filter[k],
                                 rule[rule_pos].rename_to[k],
                                 remote_filename);
                     break;
                  }
               }
            }
            if (remote_filename[0] == '\0')
            {
               (void)strcpy(remote_filename, final_filename);
            }
            if ((status = ftp_move(initial_filename, remote_filename)) != SUCCESS)
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  if (timeout_flag == OFF)
                  {
                     (void)rec(trans_db_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to move remote file %s to %s (%d). (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, initial_filename,
                               remote_filename, status, __FILE__, __LINE__);
                     (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, msg_str);
                  }
                  else
                  {
                     (void)rec(trans_db_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to move remote file %s to %s due to timeout. (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, initial_filename,
                               remote_filename, __FILE__, __LINE__);
                  }
               }
               (void)rec(transfer_log_fd, INFO_SIGN,
                         "%-*s[%d]: %d Bytes send in %d file(s).\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                         fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                         fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
               if (timeout_flag == OFF)
               {
                  (void)rec(transfer_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to move remote file %s to %s (%d). #%d (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, initial_filename,
                            remote_filename, status, db.job_id,
                            __FILE__, __LINE__);
                  (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            db.job_no, msg_str);
               }
               else
               {
                  (void)rec(transfer_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to move remote file %s to %s due to timeout. #%d (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, initial_filename,
                            remote_filename, db.job_id, __FILE__, __LINE__);
               }
               (void)ftp_quit();
               reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                    NO_OF_FILES_VAR |
                                    NO_OF_FILES_DONE_VAR |
                                    FILE_SIZE_DONE_VAR |
                                    FILE_SIZE_IN_USE_VAR |
                                    FILE_SIZE_IN_USE_DONE_VAR |
                                    FILE_NAME_IN_USE_VAR));
               exit(MOVE_REMOTE_ERROR);
            }
            else
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  (void)rec(trans_db_log_fd, INFO_SIGN,
                            "%-*s[%d]: Renamed remote file %s to %s (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, initial_filename,
                            remote_filename, __FILE__, __LINE__);
                  (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, msg_str);
               }
            }
            if (db.lock == DOT_VMS)
            {
               /* Remove dot at end of name */
               ptr = final_filename + strlen(final_filename) - 1;
               *ptr = '\0';
            }
         }

#ifdef _WITH_READY_FILES
         if ((db.lock == READY_A_FILE) || (db.lock == READY_B_FILE))
         {
            int  rdy_length;
            char file_type,
                 ready_file_name[MAX_FILENAME_LENGTH],
                 ready_file_buffer[MAX_PATH_LENGTH + 25];

            /* Generate the name for the ready file */
            (void)sprintf(ready_file_name, "%s_rdy", final_filename);

            /* Open ready file on remote site */
            if ((status = ftp_data(ready_file_name, append_offset)) != SUCCESS)
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  if (timeout_flag == OFF)
                  {
                     (void)rec(trans_db_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to open remote ready file %s (%d). (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, ready_file_name, status,
                               __FILE__, __LINE__);
                     (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, msg_str);
                  }
                  else
                  {
                     (void)rec(trans_db_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to open remote ready file %s due to timeout. (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, ready_file_name,
                               __FILE__, __LINE__);
                  }
               }
               (void)rec(transfer_log_fd, INFO_SIGN,
                         "%-*s[%d]: %d Bytes send in %d file(s).\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                         fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                         fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
               if (timeout_flag == OFF)
               {
                  (void)rec(transfer_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to open remote ready file %s (%d). #%d (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, ready_file_name,
                            status, db.job_id, __FILE__, __LINE__);

                  /*
                   * If another error, ie not a remote error, has occurred
                   * it is of no interest what the remote sever has to say.
                   */
                  if (status != -1)
                  {
                     (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, msg_str);
                  }
               }
               else
               {
                  (void)rec(transfer_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to open remote ready file %s due to timeout. #%d (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, ready_file_name,
                            db.job_id, __FILE__, __LINE__);
               }
               (void)ftp_quit();
               reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                    NO_OF_FILES_VAR |
                                    NO_OF_FILES_DONE_VAR |
                                    FILE_SIZE_DONE_VAR |
                                    FILE_SIZE_IN_USE_VAR |
                                    FILE_SIZE_IN_USE_DONE_VAR |
                                    FILE_NAME_IN_USE_VAR));
               exit(OPEN_REMOTE_ERROR);
            }
            else
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  (void)rec(trans_db_log_fd, INFO_SIGN,
                            "%-*s[%d]: Open remote ready file %s (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, ready_file_name,
                            __FILE__, __LINE__);
                  (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, msg_str);
               }
            }

            /* Create contents for ready file */
            if (db.lock == READY_A_FILE)
            {
               file_type = 'A';
            }
            else
            {
               file_type = 'B';
            }
            rdy_length = sprintf(ready_file_buffer,
                                 "%s %c U\n$$end_of_ready_file\n",
                                 initial_filename, file_type);

            /* Write remote ready file in one go. */
            if ((status = ftp_write(ready_file_buffer, NULL, rdy_length)) != SUCCESS)
            {
               /*
                * It could be that we have received a SIGPIPE
                * signal. If this is the case there might be data
                * from the remote site on the control connection.
                * Try to read this data into the global variable
                * 'msg_str'.
                */
               if (sigpipe_flag == ON)
               {
                  (void)ftp_get_reply();
               }
               if (fsa[(int)db.position].debug == YES)
               {
                  if (timeout_flag == OFF)
                  {
                     (void)rec(trans_db_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to write to remote ready file %s (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, ready_file_name,
                               __FILE__, __LINE__);
                     (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, msg_str);
                  }
                  else
                  {
                     (void)rec(trans_db_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to write to remote ready file %s due to timeout. (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, ready_file_name,
                               __FILE__, __LINE__);
                  }
               }
               (void)rec(transfer_log_fd, INFO_SIGN,
                         "%-*s[%d]: %d Bytes send in %d file(s).\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                         fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                         fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
               if (timeout_flag == OFF)
               {
                  (void)rec(transfer_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to write to remote ready file %s #%d (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, ready_file_name,
                            db.job_id, __FILE__, __LINE__);
                  (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, msg_str);
               }
               else
               {
                  (void)rec(transfer_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to write to remote ready file %s due to timeout. #%d (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, ready_file_name,
                            db.job_id, __FILE__, __LINE__);
               }
               if (status == EPIPE)
               {
                  /*
                   * When pipe is broken no nead to send a QUIT
                   * to the remote side since the connection has
                   * already been closed by the remote side.
                   */
                  (void)rec(transfer_log_fd, DEBUG_SIGN,
                            "%-*s[%d]: Hmm. Pipe is broken. Will NOT send a QUIT. (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, __FILE__, __LINE__);
               }
               else
               {
                  (void)ftp_quit();
               }
               reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                    NO_OF_FILES_VAR |
                                    NO_OF_FILES_DONE_VAR |
                                    FILE_SIZE_DONE_VAR |
                                    FILE_SIZE_IN_USE_VAR |
                                    FILE_SIZE_IN_USE_DONE_VAR |
                                    FILE_NAME_IN_USE_VAR));
               exit(WRITE_REMOTE_ERROR);
            }

            /* Close remote ready file */
            if ((status = ftp_close_data()) != SUCCESS)
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  if (timeout_flag == OFF)
                  {
                     (void)rec(trans_db_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to close remote ready file %s (%d). (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, ready_file_name, status,
                               __FILE__, __LINE__);
                     (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, msg_str);
                  }
                  else
                  {
                     (void)rec(trans_db_log_fd, ERROR_SIGN,
                               "%-*s[%d]: Failed to close remote ready file %s due to timeout. (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, ready_file_name,
                               __FILE__, __LINE__);
                  }
               }
               (void)rec(transfer_log_fd, INFO_SIGN,
                         "%-*s[%d]: %d Bytes send in %d file(s).\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                         fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                         fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
               if (timeout_flag == OFF)
               {
                  (void)rec(transfer_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to close remote ready file %s (%d). #%d (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, ready_file_name, status,
                            db.job_id, __FILE__, __LINE__);
                  (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, msg_str);
               }
               else
               {
                  (void)rec(transfer_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to close remote ready file %s due to timeout. #%d (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, ready_file_name,
                            db.job_id, __FILE__, __LINE__);
               }
               (void)ftp_quit();
               reset_fsa(p_db, YES, (CONNECT_STATUS_VAR |
                                    NO_OF_FILES_VAR |
                                    NO_OF_FILES_DONE_VAR |
                                    FILE_SIZE_DONE_VAR |
                                    FILE_SIZE_IN_USE_VAR |
                                    FILE_SIZE_IN_USE_DONE_VAR |
                                    FILE_NAME_IN_USE_VAR));
               exit(CLOSE_REMOTE_ERROR);
            }
            else
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  (void)rec(trans_db_log_fd, INFO_SIGN,
                            "%-*s[%d]: Closed remote ready file %s (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, ready_file_name,
                            __FILE__, __LINE__);
                  (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, msg_str);
               }
            }
         }
#endif /* _WITH_READY_FILES */

         /* Update FSA, one file transmitted. */
         if (host_deleted == NO)
         {
            lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
            rlock_region(fsa_fd, lock_offset);

            /* Before we read from the FSA lets make */
            /* sure that it is NOT stale!            */
            if (check_fsa() == YES)
            {
               if ((db.position = get_position(fsa, db.host_alias, no_of_hosts)) == INCORRECT)
               {
                  host_deleted = YES;
               }
               else
               {
                  lock_offset = (char *)&fsa[(int)db.position] - (char *)fsa;
                  rlock_region(fsa_fd, lock_offset);
               }
            }
            if (host_deleted == NO)
            {
               fsa[(int)db.position].job_status[(int)db.job_no].file_name_in_use[0] = '\0';
               fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done++;
               fsa[(int)db.position].job_status[(int)db.job_no].file_size_in_use = 0;
               fsa[(int)db.position].job_status[(int)db.job_no].file_size_in_use_done = 0;

               /* Total file counter */
               lock_region_w(fsa_fd, (char *)&fsa[(int)db.position].total_file_counter - (char *)fsa);
               fsa[(int)db.position].total_file_counter -= 1;
#ifdef _VERIFY_FSA
               if (fsa[(int)db.position].total_file_counter < 0)
               {
                  (void)rec(sys_log_fd, DEBUG_SIGN,
                            "Total file counter for host %s less then zero. Correcting to %d. (%s %d)\n",
                            fsa[(int)db.position].host_dsp_name,
                            files_to_send - (files_send + 1),
                            __FILE__, __LINE__);
                  fsa[(int)db.position].total_file_counter = files_to_send - (files_send + 1);
               }
#endif

               /* Total file size */
#ifdef _VERIFY_FSA
               ui_variable = fsa[(int)db.position].total_file_size;
#endif
               fsa[(int)db.position].total_file_size -= stat_buf.st_size;
#ifdef _VERIFY_FSA
               if (fsa[(int)db.position].total_file_size > ui_variable)
               {
                  int   k;
                  off_t *tmp_ptr = p_file_size_buffer;

                  tmp_ptr++;
                  fsa[(int)db.position].total_file_size = 0;
                  for (k = (files_send + 1); k < files_to_send; k++)
                  {
                     fsa[(int)db.position].total_file_size += *tmp_ptr;
                  }

                  (void)rec(sys_log_fd, DEBUG_SIGN,
                            "Total file size for host %s overflowed. Correcting to %lu. (%s %d)\n",
                            fsa[(int)db.position].host_dsp_name,
                            fsa[(int)db.position].total_file_size,
                            __FILE__, __LINE__);
               }
               else if ((fsa[(int)db.position].total_file_counter == 0) &&
                        (fsa[(int)db.position].total_file_size > 0))
                    {
                       (void)rec(sys_log_fd, DEBUG_SIGN,
                                 "fc for host %s is zero but fs is not zero. Correcting. (%s %d)\n",
                                 fsa[(int)db.position].host_dsp_name,
                                 __FILE__, __LINE__);
                       fsa[(int)db.position].total_file_size = 0;
                    }
#endif
               unlock_region(fsa_fd, (char *)&fsa[(int)db.position].total_file_counter - (char *)fsa);

               /* File counter done */
               lock_region_w(fsa_fd, (char *)&fsa[(int)db.position].file_counter_done - (char *)fsa);
               fsa[(int)db.position].file_counter_done += 1;
               unlock_region(fsa_fd, (char *)&fsa[(int)db.position].file_counter_done - (char *)fsa);

               /* Number of bytes send */
               lock_region_w(fsa_fd, (char *)&fsa[(int)db.position].bytes_send - (char *)fsa);
               fsa[(int)db.position].bytes_send += (stat_buf.st_size - append_offset);
               unlock_region(fsa_fd, (char *)&fsa[(int)db.position].bytes_send - (char *)fsa);
               unlock_region(fsa_fd, lock_offset);
            }
         }

         if (append_file_number != -1)
         {
            /* This file was appended, so lets remove it */
            /* from the append list in the message file. */
            remove_append(db.job_id, db.restart_file[append_file_number]);
         }

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
            if (archive_file(file_path, final_filename, p_db) < 0)
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  (void)rec(trans_db_log_fd, ERROR_SIGN,
                            "%-*s[%d]: Failed to archive file %s (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, final_filename,
                            __FILE__, __LINE__);
               }

#ifdef _OUTPUT_LOG
               if (db.output_log == YES)
               {
                  if (db.trans_rename_rule[0] == '\0')
                  {
                     (void)strcpy(ol_file_name, p_file_name_buffer);
                  }
                  else
                  {
                     (void)sprintf(ol_file_name, "%s /%s", p_file_name_buffer,
                                   remote_filename);
                  }
                  *ol_file_size = *p_file_size_buffer;
                  *ol_job_number = fsa[(int)db.position].job_status[(int)db.job_no].job_id;
                  *ol_transfer_time = end_time - start_time;
                  *ol_file_name_length = 0;
                  ol_real_size = strlen(p_file_name_buffer) + ol_size;
                  if (write(ol_fd, ol_data, ol_real_size) != ol_real_size)
                  {
                     (void)rec(sys_log_fd, ERROR_SIGN,
                               "write() error : %s (%s %d)\n",
                               strerror(errno), __FILE__, __LINE__);
                  }
               }
#endif
            }
            else
            {
               if (fsa[(int)db.position].debug == YES)
               {
                  (void)rec(trans_db_log_fd, INFO_SIGN,
                            "%-*s[%d]: Archived file %s (%s %d)\n",
                            MAX_HOSTNAME_LENGTH, tr_hostname,
                            (int)db.job_no, final_filename,
                            __FILE__, __LINE__);
               }

#ifdef _OUTPUT_LOG
               if (db.output_log == YES)
               {
                  if (db.trans_rename_rule[0] == '\0')
                  {
                     (void)strcpy(ol_file_name, p_file_name_buffer);
                     *ol_file_name_length = (unsigned short)strlen(ol_file_name);
                  }
                  else
                  {
                     *ol_file_name_length = (unsigned short)sprintf(ol_file_name,
                                                                    "%s /%s",
                                                                    p_file_name_buffer,
                                                                    remote_filename);
                  }
                  (void)strcpy(&ol_file_name[*ol_file_name_length + 1],
                               &db.archive_dir[db.archive_offset]);
                  *ol_file_size = *p_file_size_buffer;
                  *ol_job_number = fsa[(int)db.position].job_status[(int)db.job_no].job_id;
                  *ol_transfer_time = end_time - start_time;
                  ol_real_size = *ol_file_name_length +
                                 strlen(&ol_file_name[*ol_file_name_length + 1]) +
                                 ol_size;
                  if (write(ol_fd, ol_data, ol_real_size) != ol_real_size)
                  {
                     (void)rec(sys_log_fd, ERROR_SIGN,
                               "write() error : %s (%s %d)\n",
                               strerror(errno), __FILE__, __LINE__);
                  }
               }
#endif
            }
         }
         else
         {
            /* Delete the file we just have send */
            if (remove(fullname) == -1)
            {
               (void)rec(sys_log_fd, ERROR_SIGN,
                         "Could not remove local file %s after sending it successfully : %s (%s %d)\n",
                         fullname, strerror(errno), __FILE__, __LINE__);
            }

#ifdef _OUTPUT_LOG
            if (db.output_log == YES)
            {
               if (db.trans_rename_rule[0] == '\0')
               {
                  (void)strcpy(ol_file_name, p_file_name_buffer);
               }
               else
               {
                  (void)sprintf(ol_file_name, "%s /%s", p_file_name_buffer,
                                remote_filename);
               }
               *ol_file_size = *p_file_size_buffer;
               *ol_job_number = fsa[(int)db.position].job_status[(int)db.job_no].job_id;
               *ol_transfer_time = end_time - start_time;
               *ol_file_name_length = 0;
               ol_real_size = strlen(ol_file_name) + ol_size;
               if (write(ol_fd, ol_data, ol_real_size) != ol_real_size)
               {
                  (void)rec(sys_log_fd, ERROR_SIGN,
                            "write() error : %s (%s %d)\n",
                            strerror(errno), __FILE__, __LINE__);
               }
            }
#endif
         }

#ifdef _RADAR_CHECK
         if ((db.special_flag & RADAR_CHECK_FLAG) &&
             (p_file_name_buffer[0] == 'r') &&
             (p_file_name_buffer[1] == 'a') &&
             (p_file_name_buffer[2] == 'a') &&
             (p_file_name_buffer[3] == '0') &&
             (p_file_name_buffer[4] == '0') &&
             (p_file_name_buffer[5] == '-') &&
             (p_file_name_buffer[6] == 'p') &&
             (p_file_name_buffer[7] == 'l') &&
             (p_file_name_buffer[8] == '_') &&
             (isdigit(p_file_name_buffer[15])) &&
             (isdigit(p_file_name_buffer[16])) &&
             (isdigit(p_file_name_buffer[17])) &&
             (isdigit(p_file_name_buffer[18])) &&
             (isdigit(p_file_name_buffer[19])) &&
             (isdigit(p_file_name_buffer[20])) &&
             (isdigit(p_file_name_buffer[21])) &&
             (isdigit(p_file_name_buffer[22])) &&
             (isdigit(p_file_name_buffer[23])) &&
             (isdigit(p_file_name_buffer[24])))
         {
            char      str[3];
            time_t    diff_time,
                      time_val;
            struct tm *bd_time;

            time(&time_val);
            bd_time = gmtime(&time_val);
            bd_time->tm_sec  = 0;
            str[0] = p_file_name_buffer[23];
            str[1] = p_file_name_buffer[24];
            str[2] = '\0';
            bd_time->tm_min  = atoi(str);
            str[0] = p_file_name_buffer[21];
            str[1] = p_file_name_buffer[22];
            bd_time->tm_hour = atoi(str);
            str[0] = p_file_name_buffer[19];
            str[1] = p_file_name_buffer[20];
            bd_time->tm_mday = atoi(str);
            str[0] = p_file_name_buffer[17];
            str[1] = p_file_name_buffer[18];
            bd_time->tm_mon  = atoi(str) - 1;
            diff_time = time_val - (mktime(bd_time) - timezone);
            if (diff_time > 540) /* 9 minutes */
            {
               (void)rec(transfer_log_fd, DEBUG_SIGN,
                         "%-*s[%d]: =====> %s %ld seconds late. (%s %d)\n",
                          MAX_HOSTNAME_LENGTH, tr_hostname,
                          (int)db.job_no, p_file_name_buffer,
                          diff_time, __FILE__, __LINE__);
            }
            else if (diff_time > 420) /* 7 minutes */
                 {
                    (void)rec(transfer_log_fd, DEBUG_SIGN,
                              "%-*s[%d]: ====> %s %ld seconds late. (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, p_file_name_buffer,
                               diff_time, __FILE__, __LINE__);
                 }
            else if (diff_time > 300) /* 5 minutes */
                 {
                    (void)rec(transfer_log_fd, DEBUG_SIGN,
                              "%-*s[%d]: ===> %s %ld seconds late. (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, p_file_name_buffer,
                               diff_time, __FILE__, __LINE__);
                 }
            else if (diff_time > 180) /* 3 minutes */
                 {
                    (void)rec(transfer_log_fd, DEBUG_SIGN,
                              "%-*s[%d]: ==> %s %ld seconds late. (%s %d)\n",
                               MAX_HOSTNAME_LENGTH, tr_hostname,
                               (int)db.job_no, p_file_name_buffer,
                               diff_time, __FILE__, __LINE__);
                 }
         }
#endif /* _RADAR_CHECK */

         /*
          * After each successful transfer set error
          * counter to zero, so that other jobs can be
          * started.
          * Also move all, error entries back to the message
          * and file directories.
          */
         if (fsa[(int)db.position].error_counter > 0)
         {
            int  fd,
                 j;
            char fd_wake_up_fifo[MAX_PATH_LENGTH];

            lock_region_w(fsa_fd, (char *)&fsa[(int)db.position].error_counter - (char *)fsa);
            fsa[(int)db.position].error_counter = 0;

            /*
             * Wake up FD!
             */
            (void)sprintf(fd_wake_up_fifo, "%s%s%s", p_work_dir,
                          FIFO_DIR, FD_WAKE_UP_FIFO);
            if ((fd = open(fd_wake_up_fifo, O_RDWR)) == -1)
            {
               (void)rec(sys_log_fd, WARN_SIGN,
                         "Failed to open() FIFO %s : %s (%s %d)\n",
                         fd_wake_up_fifo, strerror(errno),
                         __FILE__, __LINE__);
            }
            else
            {
               char dummy;

               if (write(fd, &dummy, 1) != 1)
               {
                  (void)rec(sys_log_fd, WARN_SIGN,
                            "Failed to write() to FIFO %s : %s (%s %d)\n",
                            fd_wake_up_fifo, strerror(errno),
                            __FILE__, __LINE__);
               }
               if (close(fd) == -1)
               {
                  (void)rec(sys_log_fd, DEBUG_SIGN,
                            "Failed to close() FIFO %s : %s (%s %d)\n",
                            fd_wake_up_fifo, strerror(errno),
                            __FILE__, __LINE__);
               }
            }

            /*
             * Remove the error condition (NOT_WORKING) from all jobs
             * of this host.
             */
            for (j = 0; j < fsa[(int)db.position].allowed_transfers; j++)
            {
               if ((j != db.job_no) &&
                   (fsa[(int)db.position].job_status[j].connect_status == NOT_WORKING))
               {
                  fsa[(int)db.position].job_status[j].connect_status = DISCONNECT;
               }
            }
            unlock_region(fsa_fd, (char *)&fsa[(int)db.position].error_counter - (char *)fsa);

            /*
             * Since we have successfully transmitted a file, no need to
             * have the queue stopped anymore.
             */
            if (fsa[(int)db.position].host_status & AUTO_PAUSE_QUEUE_STAT)
            {
               fsa[(int)db.position].host_status ^= AUTO_PAUSE_QUEUE_STAT;
               (void)rec(sys_log_fd, INFO_SIGN,
                         "Starting queue for %s that was stopped by init_afd. (%s %d)\n",
                         fsa[(int)db.position].host_alias, __FILE__, __LINE__);
            }

            /*
             * Hmmm. This is very dangerous! But let see how it
             * works in practice.
             */
            if (fsa[(int)db.position].host_status & AUTO_PAUSE_QUEUE_LOCK_STAT)
            {
               fsa[(int)db.position].host_status ^= AUTO_PAUSE_QUEUE_LOCK_STAT;
            }
         } /* if (fsa[(int)db.position].error_counter > 0) */

         p_file_name_buffer += MAX_FILENAME_LENGTH;
         p_file_size_buffer++;
      } /* for (files_send = 0; files_send < files_to_send; files_send++) */

#ifdef _BURST_MODE
      search_for_files = YES;
      lock_region_w(fsa_fd, (char *)&fsa[(int)db.position].job_status[(int)db.job_no].job_id - (char *)fsa);
   } while (fsa[(int)db.position].job_status[(int)db.job_no].burst_counter != burst_counter);

   fsa[(int)db.position].job_status[(int)db.job_no].connect_status = 15;
   fsa[(int)db.position].job_status[(int)db.job_no].burst_counter = 0;
   total_files_send += files_send;
#endif

   free(buffer);

   /* Do not forget to remove lock file if we have created one */
   if ((db.lock == LOCKFILE) && (fsa[(int)db.position].active_transfers == 1))
   {
      if ((status = ftp_dele(LOCK_FILENAME)) != SUCCESS)
      {
         if (fsa[(int)db.position].debug == YES)
         {
            if (timeout_flag == OFF)
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to remove remote lock file %s (%d). (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, LOCK_FILENAME, status,
                         __FILE__, __LINE__);
               (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, msg_str);
            }
            else
            {
               (void)rec(trans_db_log_fd, ERROR_SIGN,
                         "%-*s[%d]: Failed to remove remote lock file %s due to timeout. (%s %d)\n",
                         MAX_HOSTNAME_LENGTH, tr_hostname,
                         (int)db.job_no, LOCK_FILENAME, __FILE__, __LINE__);
            }
         }
         (void)rec(transfer_log_fd, INFO_SIGN,
                   "%-*s[%d]: %d Bytes send in %d file(s).\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                   fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                   fsa[(int)db.position].job_status[(int)db.job_no].no_of_files_done);
         if (timeout_flag == OFF)
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to remove remote lock file %s (%d). #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, LOCK_FILENAME, status,
                      db.job_id, __FILE__, __LINE__);
            (void)rec(transfer_log_fd, ERROR_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
         else
         {
            (void)rec(transfer_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to remove remote lock file %s due to timeout. #%d (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, LOCK_FILENAME,
                      db.job_id, __FILE__, __LINE__);
         }
         (void)ftp_quit();
         reset_fsa(p_db, YES, (CONNECT_STATUS_VAR | NO_OF_FILES_VAR |
                              NO_OF_FILES_DONE_VAR | FILE_SIZE_DONE_VAR));
         exit(REMOVE_LOCKFILE_ERROR);
      }
      else
      {
         if (fsa[(int)db.position].debug == YES)
         {
            (void)rec(trans_db_log_fd, INFO_SIGN,
                      "%-*s[%d]: Removed lock file %s. (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, LOCK_FILENAME, __FILE__, __LINE__);
            (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
      }
   }

   (void)sprintf(line_buffer, "%-*s[%d]: %lu Bytes send in %d file(s).",
                 MAX_HOSTNAME_LENGTH, tr_hostname, (int)db.job_no,
                 fsa[(int)db.position].job_status[(int)db.job_no].file_size_done,
                 total_files_send);

   if (append_count == 1)
   {
      (void)strcat(line_buffer, " [APPEND]");
   }
   else if (append_count > 1)
        {
           char tmp_buffer[13 + MAX_INT_LENGTH];

           (void)sprintf(tmp_buffer, " [APPEND * %d]", append_count);
           (void)strcat(line_buffer, tmp_buffer);
        }
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

#ifdef _CHECK_BEFORE_EXIT
#endif

   /* Logout again */
   if ((status = ftp_quit()) != SUCCESS)
   {
      if (fsa[(int)db.position].debug == YES)
      {
         if (timeout_flag == OFF)
         {
            (void)rec(trans_db_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to disconnect from remote host. (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, __FILE__, __LINE__);
            (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
         else
         {
            (void)rec(trans_db_log_fd, ERROR_SIGN,
                      "%-*s[%d]: Failed to disconnect from remote host due to timeout. (%s %d)\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, __FILE__, __LINE__);
         }
      }

      /*
       * Since all files have been transfered successful it is
       * not necessary to indicate an error in the status display.
       * It's enough when we say in the Transfer log that we failed
       * to log out.
       */
      if (timeout_flag == OFF)
      {
         (void)rec(transfer_log_fd, WARN_SIGN,
                   "%-*s[%d]: Failed to log out (%d). #%d (%s %d)\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname,
                   (int)db.job_no, status,
                   db.job_id, __FILE__, __LINE__);
         if (status != INCORRECT)
         {
            (void)rec(transfer_log_fd, WARN_SIGN, "%-*s[%d]: %s\n",
                      MAX_HOSTNAME_LENGTH, tr_hostname,
                      (int)db.job_no, msg_str);
         }
      }
      else
      {
         (void)rec(transfer_log_fd, WARN_SIGN,
                   "%-*s[%d]: Failed to log out due to timeout. #%d (%s %d)\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname,
                   (int)db.job_no, db.job_id, __FILE__, __LINE__);
      }
   }
   else
   {
      if (fsa[(int)db.position].debug == YES)
      {
         (void)rec(trans_db_log_fd, INFO_SIGN,
                   "%-*s[%d]: Logged out. (%s %d)\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname,
                   (int)db.job_no, __FILE__, __LINE__);
         (void)rec(trans_db_log_fd, INFO_SIGN, "%-*s[%d]: %s\n",
                   MAX_HOSTNAME_LENGTH, tr_hostname,
                   (int)db.job_no, msg_str);
      }
   }

   /* Don't need the ASCII buffer */
   if (ascii_buffer != NULL)
   {
      free(ascii_buffer);
   }

   /*
    * If a file that is to be appended, removed (eg. by disabling
    * the host), the name of the append file will be left in the
    * message (in the worst case forever). So lets do a check
    * here if all files are transmitted only then remove all append
    * files from the message.
    */
   if ((db.no_of_restart_files > 0) &&
       (append_count != db.no_of_restart_files) &&
       (fsa[(int)db.position].total_file_counter == 0))
   {
      remove_all_appends(db.job_id);
   }

   /* Inform FSA that we have finished transferring */
   /* data and have disconnected.                   */
   reset_fsa(p_db, NO, (CONNECT_STATUS_VAR | NO_OF_FILES_VAR |
                        NO_OF_FILES_DONE_VAR | FILE_SIZE_DONE_VAR));

   /*
    * Remove file directory, but only when all files have
    * been transmitted.
    */
   if ((files_to_send == files_send) || (files_to_send == 0))
   {
      if (rmdir(file_path) < 0)
      {
         (void)rec(sys_log_fd, ERROR_SIGN,
                   "Failed to remove directory %s : %s (%s %d)\n",
                   file_path, strerror(errno), __FILE__, __LINE__);
      }
   }
   else
   {
      (void)rec(sys_log_fd, WARN_SIGN,
                "There are still %d files for %s. Will NOT remove this job! (%s %d)\n",
                files_to_send - files_send, file_path, __FILE__, __LINE__);
   }

   exit(TRANSFER_SUCCESS);
}


/*+++++++++++++++++++++++++++++ sf_ftp_exit() +++++++++++++++++++++++++++*/
static void
sf_ftp_exit(void)
{
   int  fd;
   char sf_fin_fifo[MAX_PATH_LENGTH];

#ifdef _SMART_APPEND
   if ((file_under_process == YES) &&
       (fsa[(int)db.position].file_size_offset > -1) &&
       (append_offset == 0) &&
       (fsa[(int)db.position].job_status[(int)db.job_no].file_size_done > MAX_SEND_BEFORE_APPEND))
   {
      log_append(db.job_id, initial_filename);
   }
#endif

   reset_fsa((struct job *)&db, NO, FILE_SIZE_VAR);

   if (db.trans_rename_rule[0] != '\0')
   {
      (void)close(counter_fd);
   }
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
      (void)rec(sys_log_fd, ERROR_SIGN,
                "Could not open fifo %s : %s (%s %d)\n",
                sf_fin_fifo, strerror(errno), __FILE__, __LINE__);
   }
   else
   {
      pid_t pid = getpid();
#ifdef _FIFO_DEBUG
      char  cmd[2];
#endif
      /* Tell FD we are finished */
#ifdef _FIFO_DEBUG
      cmd[0] = ACKN; cmd[1] = '\0';
      show_fifo_data('W', "sf_fin", cmd, 1, __FILE__, __LINE__);
#endif
      if (write(fd, &pid, sizeof(pid_t)) != sizeof(pid_t))
      {
         (void)rec(sys_log_fd, WARN_SIGN,
                   "write() error : %s (%s %d)\n",
                   strerror(errno), __FILE__, __LINE__);
      }
      (void)close(fd);
   }
   (void)close(sys_log_fd);

   return;
}


/*++++++++++++++++++++++++++++++ sig_pipe() +++++++++++++++++++++++++++++*/
static void
sig_pipe(int signo)
{
   /* Ignore any future signals of this kind. */
   if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
   {
      (void)rec(sys_log_fd, ERROR_SIGN, "signal() error : %s (%s %d)\n",
                strerror(errno), __FILE__, __LINE__);
   }
   sigpipe_flag = ON;

   return;
}


/*++++++++++++++++++++++++++++++ sig_segv() +++++++++++++++++++++++++++++*/
static void
sig_segv(int signo)
{
   reset_fsa((struct job *)&db, YES, (CONNECT_STATUS_VAR |
                        NO_OF_FILES_VAR | NO_OF_FILES_DONE_VAR |
                        FILE_SIZE_VAR | FILE_SIZE_DONE_VAR |
                        FILE_SIZE_IN_USE_VAR | FILE_SIZE_IN_USE_DONE_VAR |
                        FILE_NAME_IN_USE_VAR | PROC_ID_VAR));
   (void)rec(sys_log_fd, DEBUG_SIGN,
             "Aaarrrggh! Received SIGSEGV. Remove the programmer who wrote this! (%s %d)\n",
             __FILE__, __LINE__);
   abort();
}


/*++++++++++++++++++++++++++++++ sig_bus() ++++++++++++++++++++++++++++++*/
static void
sig_bus(int signo)
{
   reset_fsa((struct job *)&db, YES, (CONNECT_STATUS_VAR |
                        NO_OF_FILES_VAR | NO_OF_FILES_DONE_VAR |
                        FILE_SIZE_VAR | FILE_SIZE_DONE_VAR |
                        FILE_SIZE_IN_USE_VAR | FILE_SIZE_IN_USE_DONE_VAR |
                        FILE_NAME_IN_USE_VAR | PROC_ID_VAR));
   (void)rec(sys_log_fd, DEBUG_SIGN,
             "Uuurrrggh! Received SIGBUS. (%s %d)\n",
             __FILE__, __LINE__);
   abort();
}


/*++++++++++++++++++++++++++++++ sig_kill() +++++++++++++++++++++++++++++*/
static void
sig_kill(int signo)
{
   reset_fsa((struct job *)&db, NO, (CONNECT_STATUS_VAR |
                        NO_OF_FILES_VAR | NO_OF_FILES_DONE_VAR |
                        FILE_SIZE_VAR | FILE_SIZE_DONE_VAR |
                        FILE_SIZE_IN_USE_VAR | FILE_SIZE_IN_USE_DONE_VAR |
                        FILE_NAME_IN_USE_VAR | PROC_ID_VAR));
   exit(GOT_KILLED);
}


/*++++++++++++++++++++++++++++++ sig_exit() +++++++++++++++++++++++++++++*/
static void
sig_exit(int signo)
{
   reset_fsa((struct job *)&db, YES, (CONNECT_STATUS_VAR |
                        NO_OF_FILES_VAR | NO_OF_FILES_DONE_VAR |
                        FILE_SIZE_VAR | FILE_SIZE_DONE_VAR |
                        FILE_SIZE_IN_USE_VAR | FILE_SIZE_IN_USE_DONE_VAR |
                        FILE_NAME_IN_USE_VAR | PROC_ID_VAR));
   exit(INCORRECT);
}
