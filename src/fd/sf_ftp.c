/*
 *  sf_ftp.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1995 - 2010 Deutscher Wetterdienst (DWD),
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
 **   sf_ftp <work dir> <job no.> <FSA id> <FSA pos> <msg name> [options]
 **
 **   options
 **       --version        Version Number
 **       -a <age limit>   The age limit for the files being send.
 **       -A               Disable archiving of files.
 **       -o <retries>     Old/Error message and number of retries.
 **       -r               Resend from archive (job from show_olog).
 **       -t               Temp toggle.
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
 **   26.04.1999 H.Kiehl Added option "no login".
 **   29.01.2000 H.Kiehl Added ftp_size() command.
 **   08.07.2000 H.Kiehl Cleaned up log output to reduce code size.
 **   12.11.2000 H.Kiehl Added ftp_exec() command.
 **   09.01.2004 H.Kiehl Added ftp_fast_move() command.
 **   01.02.2004 H.Kiehl Added TLS/SSL support.
 **
 */
DESCR__E_M1

#include <stdio.h>                     /* fprintf(), sprintf()           */
#include <string.h>                    /* strcpy(), strcat(), strcmp(),  */
                                       /* strerror()                     */
#include <stdlib.h>                    /* malloc(), free(), abort()      */
#include <ctype.h>                     /* isdigit()                      */
#ifdef FTP_CTRL_KEEP_ALIVE_INTERVAL
# include <time.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _OUTPUT_LOG
# include <sys/times.h>                /* times()                        */
#endif
#ifdef TM_IN_SYS_TIME
# include <sys/time.h>
#endif
#include <fcntl.h>
#include <signal.h>                    /* signal()                       */
#include <unistd.h>                    /* unlink(), close()              */
#include <errno.h>
#include "fddefs.h"
#include "ftpdefs.h"
#include "version.h"
#ifdef WITH_EUMETSAT_HEADERS
# include "eumetsat_header_defs.h"
#endif

/* Global variables. */
int                        event_log_fd = STDERR_FILENO,
                           exitflag = IS_FAULTY_VAR,
                           files_to_delete,
                           no_of_dirs,
                           no_of_hosts,
                           *p_no_of_hosts = NULL,
                           fra_fd = -1,
                           fra_id,
                           fsa_fd = -1,
                           fsa_id,
                           prev_no_of_files_done = 0,
                           sys_log_fd = STDERR_FILENO,
                           transfer_log_fd = STDERR_FILENO,
                           trans_db_log_fd = STDERR_FILENO,
#ifdef WITHOUT_FIFO_RW_SUPPORT
                           trans_db_log_readfd,
                           transfer_log_readfd,
#endif
                           trans_rename_blocked = NO,
                           amg_flag = NO,
                           timeout_flag;
#ifdef _OUTPUT_LOG
int                        ol_fd = -2;
# ifdef WITHOUT_FIFO_RW_SUPPORT
int                        ol_readfd = -2;
# endif
unsigned int               *ol_job_number,
                           *ol_retries;
char                       *ol_data = NULL,
                           *ol_file_name,
                           *ol_output_type;
unsigned short             *ol_archive_name_length,
                           *ol_file_name_length,
                           *ol_unl;
off_t                      *ol_file_size;
size_t                     ol_size,
                           ol_real_size;
clock_t                    *ol_transfer_time;
#endif
#ifdef _WITH_BURST_2
unsigned int               burst_2_counter = 0,
                           total_append_count = 0;
#endif
#ifdef HAVE_MMAP
off_t                      fra_size,
                           fsa_size;
#endif
off_t                      append_offset = 0,
                           *file_size_buffer = NULL;
time_t                     *file_mtime_buffer = NULL;
u_off_t                    prev_file_size_done = 0;
long                       transfer_timeout;
char                       *del_file_name_buffer = NULL,
                           *file_name_buffer = NULL,
                           *p_initial_filename,
                           msg_str[MAX_RET_MSG_LENGTH],
                           *p_work_dir = NULL,
                           tr_hostname[MAX_HOSTNAME_LENGTH + 1];
struct fileretrieve_status *fra = NULL;
struct filetransfer_status *fsa = NULL;
struct job                 db;
struct rule                *rule;
#ifdef _DELETE_LOG
struct delete_log          dl;
#endif
const char                 *sys_log_name = SYSTEM_LOG_FIFO;

/* Local global variables. */
static int                 files_send,
                           files_to_send,
                           local_file_counter;
static off_t               local_file_size,
                           *p_file_size_buffer;

/* Local function prototypes. */
static void                sf_ftp_exit(void),
                           sig_bus(int),
                           sig_segv(int),
                           sig_kill(int),
                           sig_exit(int);

/* #define _DEBUG_APPEND 1 */
/* #define _SIMULATE_SLOW_TRANSFER 2L */


/*$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ main() $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$*/
int
main(int argc, char *argv[])
{
   int              counter_fd = -1,
                    current_toggle,
                    exit_status = TRANSFER_SUCCESS,
                    j,
                    fd,
#ifdef _WITH_INTERRUPT_JOB
                    interrupt = NO,
#endif
                    status,
                    bytes_buffered,
                    append_file_number = -1,
                    blocksize,
                    *unique_counter;
   off_t            no_of_bytes;
   clock_t          clktck;
   time_t           last_update_time,
                    now,
                    *p_file_mtime_buffer;
#ifdef _WITH_BURST_2
   int              cb2_ret,
                    disconnect = NO;
   unsigned int     values_changed = 0;
#endif
#ifdef _OUTPUT_LOG
   clock_t          end_time = 0,
                    start_time = 0;
   struct tms       tmsdummy;
#endif
#ifdef FTP_CTRL_KEEP_ALIVE_INTERVAL
   time_t           keep_alive_time;
#endif
   char             *ptr,
                    *ascii_buffer = NULL,
                    *p_file_name_buffer,
                    append_count = 0,
                    *buffer,
                    final_filename[MAX_RECIPIENT_LENGTH + MAX_FILENAME_LENGTH],
                    initial_filename[MAX_RECIPIENT_LENGTH + MAX_FILENAME_LENGTH],
                    remote_filename[MAX_RECIPIENT_LENGTH + MAX_FILENAME_LENGTH],
                    fullname[MAX_PATH_LENGTH],
                    *p_final_filename,
                    *p_remote_filename,
                    *p_fullname,
                    file_path[MAX_PATH_LENGTH];
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
      system_log(FATAL_SIGN, __FILE__, __LINE__,
                 "sigaction() error : %s", strerror(errno));
      exit(INCORRECT);
   }
#endif

   /* Do some cleanups when we exit. */
   if (atexit(sf_ftp_exit) != 0)
   {
      system_log(FATAL_SIGN, __FILE__, __LINE__,
                 "Could not register exit function : %s", strerror(errno));
      exit(INCORRECT);
   }

   /* Initialise variables. */
   local_file_counter = 0;
   files_to_send = init_sf(argc, argv, file_path, FTP_FLAG);
   p_db = &db;
   msg_str[0] = '\0';
   if ((fsa->trl_per_process > 0) && (fsa->trl_per_process < fsa->block_size))
   {
      blocksize = fsa->trl_per_process;
   }
   else
   {
      blocksize = fsa->block_size;
   }
   (void)strcpy(fullname, file_path);
   p_fullname = fullname + strlen(fullname);
   if (*(p_fullname - 1) != '/')
   {
      *p_fullname = '/';
      p_fullname++;
   }
   if ((clktck = sysconf(_SC_CLK_TCK)) <= 0)
   {
      system_log(ERROR_SIGN, __FILE__, __LINE__,
                 "Could not get clock ticks per second : %s", strerror(errno));
      exit(INCORRECT);
   }

   if ((signal(SIGINT, sig_kill) == SIG_ERR) ||
       (signal(SIGQUIT, sig_exit) == SIG_ERR) ||
       (signal(SIGTERM, sig_kill) == SIG_ERR) ||
       (signal(SIGSEGV, sig_segv) == SIG_ERR) ||
       (signal(SIGBUS, sig_bus) == SIG_ERR) ||
       (signal(SIGHUP, SIG_IGN) == SIG_ERR) ||
       (signal(SIGPIPE, SIG_IGN) == SIG_ERR))
   {
      system_log(FATAL_SIGN, __FILE__, __LINE__,
                 "signal() error : %s", strerror(errno));
      exit(INCORRECT);
   }

   /*
    * In ASCII-mode an extra buffer is needed to convert LF's
    * to CRLF. By creating this buffer the function ftp_write()
    * knows it has to send the data in ASCII-mode.
    */
   if ((db.transfer_mode == 'A') || (db.transfer_mode == 'D'))
   {
      if (db.transfer_mode == 'D')
      {
         if (fsa->protocol_options & FTP_IGNORE_BIN)
         {
            db.transfer_mode = 'N';
         }
         else
         {
            db.transfer_mode = 'I';
         }
      }
      if ((ascii_buffer = malloc(((blocksize * 2) + 1))) == NULL)
      {
         system_log(ERROR_SIGN, __FILE__, __LINE__,
                    "malloc() error : %s", strerror(errno));
         exit(ALLOC_ERROR);
      }
   }

   /* Now determine the real hostname. */
   if (fsa->real_hostname[1][0] == '\0')
   {
      (void)strcpy(db.hostname, fsa->real_hostname[0]);
      current_toggle = HOST_ONE;
   }
   else
   {
      if (db.toggle_host == YES)
      {
         if (fsa->host_toggle == HOST_ONE)
         {
            (void)strcpy(db.hostname, fsa->real_hostname[HOST_TWO - 1]);
            current_toggle = HOST_TWO;
         }
         else
         {
            (void)strcpy(db.hostname, fsa->real_hostname[HOST_ONE - 1]);
            current_toggle = HOST_ONE;
         }
      }
      else
      {
         current_toggle = (int)fsa->host_toggle;
         (void)strcpy(db.hostname, fsa->real_hostname[(current_toggle - 1)]);
      }
      if (((db.special_flag & TRANS_RENAME_PRIMARY_ONLY) &&
           (current_toggle == HOST_TWO)) ||
          ((db.special_flag & TRANS_RENAME_SECONDARY_ONLY) &&
           (current_toggle == HOST_ONE)))
      {
         trans_rename_blocked = YES;
         db.trans_rename_rule[0] = '\0';
      }
   }

   if (fsa->debug > NORMAL_MODE)
   {
      msg_str[0] = '\0';
      trans_db_log(INFO_SIGN, __FILE__, __LINE__, NULL,
                   "Trying to do a %s connect to %s at port %d.",
                   db.mode_str, db.hostname, db.port);
   }

   /* Connect to remote FTP-server. */
#ifdef FTP_CTRL_KEEP_ALIVE_INTERVAL
   if (fsa->protocol_options & AFD_TCP_KEEPALIVE)
   {
      timeout_flag = transfer_timeout - 5;
      if (timeout_flag < MIN_KEEP_ALIVE_INTERVAL)
      {
         timeout_flag = MIN_KEEP_ALIVE_INTERVAL;
      }
   }
#else
   timeout_flag = OFF;
#endif
   if (((status = ftp_connect(db.hostname, db.port)) != SUCCESS) &&
       (status != 230))
   {
      trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                "FTP %s connection to `%s' at port %d failed (%d).",
                db.mode_str, db.hostname, db.port, status);
      exit(eval_timeout(CONNECT_ERROR));
   }
   else
   {
      if (fsa->debug > NORMAL_MODE)
      {
         if (status == 230)
         {
            trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                         "Connected (%s). No user and password required, logged in.",
                         db.mode_str);
         }
         else
         {
            trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                         "Connected (%s).", db.mode_str);
         }
      }
   }

#ifdef _WITH_BURST_2
   do
   {
      if (burst_2_counter > 0)
      {
         (void)memcpy(fsa->job_status[(int)db.job_no].unique_name,
                      db.msg_name, MAX_MSG_NAME_LENGTH);
         fsa->job_status[(int)db.job_no].job_id = db.job_id;
         if (values_changed & USER_CHANGED)
         {
            status = 0;
         }
         else
         {
            status = 230;
         }
         if (fsa->debug > NORMAL_MODE)
         {
            trans_db_log(INFO_SIGN, __FILE__, __LINE__, NULL,
# ifdef WITH_SSL
                         "%s Bursting.", (db.auth == NO) ? "FTP" : "FTPS");
# else
                         "FTP Bursting.");
# endif
         }
         (void)strcpy(fullname, file_path);
         p_fullname = fullname + strlen(fullname);
         if (*(p_fullname - 1) != '/')
         {
            *p_fullname = '/';
            p_fullname++;
         }
      }
#endif /* _WITH_BURST_2 */

#ifdef WITH_SSL
# ifdef _WITH_BURST_2
      if ((burst_2_counter == 0) || (values_changed & AUTH_CHANGED))
      {
# endif
         if ((db.auth == YES) || (db.auth == BOTH))
         {
            if (ftp_ssl_auth() == INCORRECT)
            {
               trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "SSL/TSL connection to server `%s' failed.",
                         db.hostname);
               exit(AUTH_ERROR);
            }
            else
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                               "Authentification successful.");
               }
            }
         }
# ifdef _WITH_BURST_2
      }
# endif
#endif

      /* Login. */
      if (status != 230) /* We are not already logged in! */
      {
         if (fsa->proxy_name[0] == '\0')
         {
#ifdef _WITH_BURST_2
            /* Send user name. */
            if ((disconnect == YES) ||
                (((status = ftp_user(db.user)) != SUCCESS) && (status != 230)))
            {
               if ((disconnect == YES) ||
                   ((burst_2_counter > 0) &&
                    ((status == 500) || (status == 503) || (status == 530))))
               {
                  /*
                   * Aaargghh..., we need to logout again! The server is
                   * not able to handle more than one USER request.
                   * We should use the REIN (REINITIALIZE) command here,
                   * however it seems most FTP-servers have this not
                   * implemented.
                   */
                  if ((status = ftp_quit()) != SUCCESS)
                  {
                     trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                               "Failed to disconnect from remote host (%d).",
                               status);
                     exit(eval_timeout(QUIT_ERROR));
                  }
                  else
                  {
                     if (fsa->debug > NORMAL_MODE)
                     {
                        trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                     "Logged out. Needed for burst.");
                        trans_db_log(INFO_SIGN, __FILE__, __LINE__, NULL,
                                     "Trying to again do a %s connect to %s at port %d.",
                                     db.mode_str, db.hostname, db.port);
                     }
                  }

                  /* Connect to remote FTP-server. */
                  msg_str[0] = '\0';
                  if (((status = ftp_connect(db.hostname, db.port)) != SUCCESS) &&
                      (status != 230))
                  {
                     trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                               "FTP connection to `%s' at port %d failed (%d).",
                               db.hostname, db.port, status);
                     exit(eval_timeout(CONNECT_ERROR));
                  }
                  else
                  {
                     if (fsa->debug > NORMAL_MODE)
                     {
                        if (status == 230)
                        {
                           trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                        "Connected. No user and password required, logged in.");
                        }
                        else
                        {
                           trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                        "Connected.");
                        }
                     }
                  }

                  if (status != 230) /* We are not already logged in! */
                  {
                     /* Send user name. */
                     if (((status = ftp_user(db.user)) != SUCCESS) &&
                         (status != 230))
                     {
                        trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                                  "Failed to send user `%s' (%d).",
                                  db.user, status);
                        (void)ftp_quit();
                        exit(eval_timeout(USER_ERROR));
                     }
                     else
                     {
                        if (fsa->debug > NORMAL_MODE)
                        {
                           if (status != 230)
                           {
                              trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                           "Entered user name `%s'.", db.user);
                           }
                           else
                           {
                              trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                           "Entered user name `%s'. No password required, logged in.",
                                           db.user);
                           }
                        }
                     }
                  }

                  /*
                   * Since we did a new connect we must set the transfer type
                   * again. Or else we will transfer files in ASCII mode.
                   */
                  values_changed |= TYPE_CHANGED;
                  disconnect = YES;
               }
               else
               {
                  trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                            "Failed to send user `%s' (%d).", db.user, status);
                  (void)ftp_quit();
                  exit(eval_timeout(USER_ERROR));
               }
            }
#else
            /* Send user name. */
            if (((status = ftp_user(db.user)) != SUCCESS) && (status != 230))
            {
               trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "Failed to send user `%s' (%d).", db.user, status);
               (void)ftp_quit();
               exit(eval_timeout(USER_ERROR));
            }
#endif
            else
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  if (status != 230)
                  {
                     trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                  "Entered user name `%s'.", db.user);
                  }
                  else
                  {
                     trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                  "Entered user name `%s'. No password required, logged in.",
                                  db.user);
                  }
               }
            }

            /* Send password (if required). */
            if (status != 230)
            {
               if ((status = ftp_pass(db.password)) != SUCCESS)
               {
                  trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                            "Failed to send password for user `%s' (%d).",
                            db.user, status);
                  (void)ftp_quit();
                  exit(eval_timeout(PASSWORD_ERROR));
               }
               else
               {
                  if (fsa->debug > NORMAL_MODE)
                  {
                     trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                  "Entered password, logged in as %s.", db.user);
                  }
               }
            } /* if (status != 230) */
         }
         else /* Go through the proxy procedure. */
         {
            handle_proxy();
         }
      } /* if (status != 230) */

#ifdef WITH_SSL
      if (db.auth > NO)
      {
         if (ftp_ssl_init(db.auth) == INCORRECT)
         {
            trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                      "SSL/TSL initialisation failed.");
            (void)ftp_quit();
            exit(AUTH_ERROR);
         }
         else
         {
            if (fsa->debug > NORMAL_MODE)
            {
               trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                            "SSL/TLS initialisation successful.");
            }
         }
      }
#endif

      if (db.special_flag & LOGIN_EXEC_FTP)
      {
         if ((status = ftp_exec(db.special_ptr, NULL)) != SUCCESS)
         {
            trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                      "Failed to send SITE %s (%d).",
                      db.special_ptr, status);
            if (timeout_flag == ON)
            {
               timeout_flag = OFF;
            }
         }
         else if (fsa->debug > NORMAL_MODE)
              {
                 trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                              "Send SITE %s", db.special_ptr);
              }
      }

      /* Check if we need to set the idle time for remote FTP-server. */
#ifdef _WITH_BURST_2
      if ((fsa->protocol_options & SET_IDLE_TIME) &&
          (burst_2_counter == 0))
#else
      if (fsa->protocol_options & SET_IDLE_TIME)
#endif
      {
         if ((status = ftp_idle(transfer_timeout)) != SUCCESS)
         {
            trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                      "Failed to set IDLE time to <%ld> (%d).",
                      transfer_timeout, status);
         }
         else
         {
            if (fsa->debug > NORMAL_MODE)
            {
               trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                            "Changed IDLE time to %ld.", transfer_timeout);
            }
         }
      }

#ifdef _WITH_BURST_2
      if ((burst_2_counter != 0) && (db.transfer_mode == 'I') &&
          (ascii_buffer != NULL))
      {
         free(ascii_buffer);
         ascii_buffer = NULL;
      }
      if ((burst_2_counter == 0) || (values_changed & TYPE_CHANGED))
      {
#endif
         /*
          * In ASCII-mode an extra buffer is needed to convert LF's
          * to CRLF. By creating this buffer the function ftp_write()
          * knows it has to send the data in ASCII-mode.
          */
         if ((db.transfer_mode == 'A') || (db.transfer_mode == 'D'))
         {
            if (db.transfer_mode == 'D')
            {
               if ((fsa->protocol_options & FTP_IGNORE_BIN) == 0)
               {
                  db.transfer_mode = 'I';
               }
               else
               {
                  db.transfer_mode = 'N';
               }
            }
            if (ascii_buffer == NULL)
            {
               if ((ascii_buffer = malloc(((blocksize * 2) + 1))) == NULL)
               {
                  system_log(ERROR_SIGN, __FILE__, __LINE__,
                             "malloc() error : %s", strerror(errno));
                  exit(ALLOC_ERROR);
               }
            }
         }

         if (db.transfer_mode != 'N')
         {
            /* Set transfer mode. */
            if ((status = ftp_type(db.transfer_mode)) != SUCCESS)
            {
               trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "Failed to set transfer mode to `%c' (%d).",
                         db.transfer_mode, status);
               (void)ftp_quit();
               exit(eval_timeout(TYPE_ERROR));
            }
            else
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                               "Changed transfer mode to `%c'.",
                               db.transfer_mode);
               }
            }
         }
#ifdef _WITH_BURST_2
      } /* ((burst_2_counter == 0) || (values_changed & TYPE_CHANGED)) */
#endif

#ifdef _WITH_BURST_2
      if ((burst_2_counter == 0) || (values_changed & TARGET_DIR_CHANGED))
      {
         /*
          * We must go to the home directory of the user when the target
          * directory is not the absolute path.
          */
         if ((burst_2_counter > 0) && (db.target_dir[0] != '/') &&
             ((fsa->protocol_options & FTP_FAST_CD) == 0) && (disconnect == NO))
         {
            if ((status = ftp_cd("", NO)) != SUCCESS)
            {
               trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "Failed to change to home directory (%d).", status);
               (void)ftp_quit();
               exit(eval_timeout(CHDIR_ERROR));
            }
            else
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                               "Changed to home directory.");
               }
            }
         }
#endif /* _WITH_BURST_2 */
         /* Change directory if necessary. */
         if ((fsa->protocol_options & FTP_FAST_CD) == 0)
         {
            if (db.target_dir[0] != '\0')
            {
               if ((status = ftp_cd(db.target_dir,
                                    (db.special_flag & CREATE_TARGET_DIR) ? YES : NO)) != SUCCESS)
               {
                  if (db.special_flag & CREATE_TARGET_DIR)
                  {
                     trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                               "Failed to change/create directory to `%s' (%d).",
                               db.target_dir, status);
                  }
                  else
                  {
                     trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                               "Failed to change directory to `%s' (%d).",
                               db.target_dir, status);
                  }
                  (void)ftp_quit();
                  exit(eval_timeout(CHDIR_ERROR));
               }
               else
               {
                  if (fsa->debug > NORMAL_MODE)
                  {
                     trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                  "Changed directory to %s.", db.target_dir);
                  }
               }
            }
            p_final_filename = final_filename;
            p_initial_filename = initial_filename;
            p_remote_filename = remote_filename;
         }
         else
         {
            if (db.target_dir[0] != '\0')
            {
               size_t target_dir_length;

               (void)strcpy(final_filename, db.target_dir);
               target_dir_length = strlen(db.target_dir);
               ptr = final_filename + target_dir_length;
               if (*(ptr - 1) != '/')
               {
                  *ptr = '/';
                  ptr++;
               }
               p_final_filename = ptr;
               memcpy(initial_filename, db.target_dir, target_dir_length);
               p_initial_filename = initial_filename + target_dir_length;
               if (*(p_initial_filename - 1) != '/')
               {
                  *p_initial_filename = '/';
                  p_initial_filename++;
               }
               memcpy(remote_filename, db.target_dir, target_dir_length);
               p_remote_filename = remote_filename + target_dir_length;
               if (*(p_remote_filename - 1) != '/')
               {
                  *p_remote_filename = '/';
                  p_remote_filename++;
               }
            }
            else
            {
               p_final_filename = final_filename;
               p_initial_filename = initial_filename;
               p_remote_filename = remote_filename;
            }
         }
#ifdef _WITH_BURST_2
      } /* ((burst_2_counter == 0) || (values_changed & TARGET_DIR_CHANGED)) */
#endif

      /* Inform FSA that we have finished connecting and */
      /* will now start to transfer data.                */
#ifdef _WITH_BURST_2
      if ((db.fsa_pos != INCORRECT) && (burst_2_counter == 0))
#else
      if (db.fsa_pos != INCORRECT)
#endif
      {
         if (gsf_check_fsa() != NEITHER)
         {
#ifdef LOCK_DEBUG
            lock_region_w(fsa_fd, db.lock_offset + LOCK_CON, __FILE__, __LINE__);
#else
            lock_region_w(fsa_fd, db.lock_offset + LOCK_CON);
#endif
            fsa->job_status[(int)db.job_no].connect_status = FTP_ACTIVE;
            fsa->job_status[(int)db.job_no].no_of_files = files_to_send;
            fsa->connections += 1;
#ifdef LOCK_DEBUG
            unlock_region(fsa_fd, db.lock_offset + LOCK_CON, __FILE__, __LINE__);
#else
            unlock_region(fsa_fd, db.lock_offset + LOCK_CON);
#endif
         }
      }

      /* If we send a lock file, do it now. */
      if (db.lock == LOCKFILE)
      {
         /* Create lock file on remote host. */
         msg_str[0] = '\0';
         if ((status = ftp_data(db.lock_file_name, 0, db.mode_flag,
                                DATA_WRITE, 0)) != SUCCESS)
         {
            trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                      "Failed to send lock file `%s' (%d).",
                      db.lock_file_name, status);
            (void)ftp_quit();
            exit(eval_timeout(WRITE_LOCK_ERROR));
         }
         else
         {
            if (fsa->debug > NORMAL_MODE)
            {
               trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                            "Created lock file %s.", db.lock_file_name);
            }
         }
#ifdef WITH_SSL
         if (db.auth == BOTH)
         {
            if (ftp_auth_data() == INCORRECT)
            {
               trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "TSL/SSL data connection to server `%s' failed.",
                         db.hostname);
               (void)ftp_quit();
               exit(eval_timeout(AUTH_ERROR));
            }
            else
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                               "Authentification successful.");
               }
            }
         }
#endif

         /* Close remote lock file. */
         if ((status = ftp_close_data()) != SUCCESS)
         {
            trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                      "Failed to close lock file `%s' (%d).",
                      db.lock_file_name, status);
            (void)ftp_quit();
            exit(eval_timeout(CLOSE_REMOTE_ERROR));
         }
         else
         {
            if (fsa->debug > NORMAL_MODE)
            {
               trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                            "Closed data connection for remote lock file `%s'.",
                            db.lock_file_name);
            }
         }
      }

#ifdef _WITH_BURST_2
      if (burst_2_counter == 0)
      {
#endif
         /* Allocate buffer to read data from the source file. */
         if ((buffer = malloc(blocksize + 4)) == NULL)
         {
            system_log(ERROR_SIGN, __FILE__, __LINE__,
                       "malloc() error : %s", strerror(errno));
            (void)ftp_quit();
            exit(ALLOC_ERROR);
         }
#ifdef _WITH_BURST_2
      } /* (burst_2_counter == 0) */
#endif

      /* Delete all remote files we have send but have been deleted */
      /* due to age-limit.                                          */
      if ((files_to_delete > 0) && (del_file_name_buffer != NULL))
      {
         int  i;
         char *p_del_file_name = del_file_name_buffer;

         for (i = 0; i < files_to_delete; i++)
         {
            if ((status = ftp_dele(p_del_file_name)) != SUCCESS)
            {
               trans_log(DEBUG_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "Failed to delete `%s' (%d).",
                         p_del_file_name, status);
            }
            else
            {
               if (fsa->debug == YES)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                               "Deleted `%s'.", p_del_file_name);
               }
            }
            p_del_file_name += MAX_FILENAME_LENGTH;
         }
      }

      /* Send all files. */
#ifdef _WITH_INTERRUPT_JOB
      interrupt = NO;
#endif
      p_file_name_buffer = file_name_buffer;
      p_file_size_buffer = file_size_buffer;
      p_file_mtime_buffer = file_mtime_buffer;
      last_update_time = time(NULL);
      local_file_size = 0;
      for (files_send = 0; files_send < files_to_send; files_send++)
      {
         /* Write status to FSA? */
         if (gsf_check_fsa() != NEITHER)
         {
            if ((fsa->active_transfers > 1) &&
                (*p_file_size_buffer > blocksize))
            {
               int file_is_duplicate = NO;

#ifdef LOCK_DEBUG
               lock_region_w(fsa_fd, db.lock_offset + LOCK_FIU + db.job_no,
                             __FILE__, __LINE__);
#else
               lock_region_w(fsa_fd, db.lock_offset + LOCK_FIU + db.job_no);
#endif

               /*
                * Check if this file is not currently being transfered!
                */
               for (j = 0; j < fsa->allowed_transfers; j++)
               {
                  if ((j != db.job_no) &&
                      (fsa->job_status[j].job_id == fsa->job_status[(int)db.job_no].job_id) &&
                      (CHECK_STRCMP(fsa->job_status[j].file_name_in_use, p_file_name_buffer) == 0))
                  {
#ifdef _DELETE_LOG
                     size_t dl_real_size;
#endif

#ifdef _OUTPUT_LOG
                     if (db.output_log == YES)
                     {
                        if (ol_fd == -2)
                        {
#  ifdef WITHOUT_FIFO_RW_SUPPORT
                           output_log_fd(&ol_fd, &ol_readfd);
#  else
                           output_log_fd(&ol_fd);
#  endif
                        }
                        if (ol_fd > -1)
                        {
                           if (ol_data == NULL)
                           {
                              output_log_ptrs(&ol_retries,
                                              &ol_job_number,
                                              &ol_data,   /* Pointer to buffer. */
                                              &ol_file_name,
                                              &ol_file_name_length,
                                              &ol_archive_name_length,
                                              &ol_file_size,          
                                              &ol_unl,
                                              &ol_size,
                                              &ol_transfer_time,
                                              &ol_output_type,
                                              db.host_alias,
                                              (current_toggle - 1),
# ifdef WITH_SSL
                                              (db.auth == NO) ? FTP : FTPS);
# else
                                              FTP);
# endif
                           }
                           (void)memcpy(ol_file_name, db.p_unique_name, db.unl);
                           (void)strcpy(ol_file_name + db.unl, p_file_name_buffer);
                           *ol_file_name_length = (unsigned short)strlen(ol_file_name);
                           ol_file_name[*ol_file_name_length] = SEPARATOR_CHAR;
                           ol_file_name[*ol_file_name_length + 1] = '\0';
                           (*ol_file_name_length)++;
                           *ol_file_size = *p_file_size_buffer;
                           *ol_job_number = db.job_id;
                           *ol_retries = db.retries;
                           *ol_unl = db.unl;
                           *ol_transfer_time = 0L;
                           *ol_archive_name_length = 0;
                           *ol_output_type = OT_OTHER_PROC_DELETE + '0';
                           ol_real_size = *ol_file_name_length + ol_size;
                           if (write(ol_fd, ol_data, ol_real_size) != ol_real_size)
                           {
                              system_log(ERROR_SIGN, __FILE__, __LINE__,
                                         "write() error : %s", strerror(errno));
                           }
                        }
                     }
#endif

#ifdef _DELETE_LOG
                     if (dl.fd == -1)
                     {
                        delete_log_ptrs(&dl);
                     }
                     (void)strcpy(dl.file_name, p_file_name_buffer);
                     (void)sprintf(dl.host_name, "%-*s %03x",
                                   MAX_HOSTNAME_LENGTH, fsa->host_alias,
                                   FILE_CURRENTLY_TRANSMITTED);
                     *dl.file_size = *p_file_size_buffer;
                     *dl.job_id = db.job_id;
                     *dl.dir_id = 0;
                     *dl.input_time = db.creation_time;
                     *dl.split_job_counter = db.split_job_counter;
                     *dl.unique_number = db.unique_number;
                     *dl.file_name_length = strlen(p_file_name_buffer);
                     dl_real_size = *dl.file_name_length + dl.size +
                                    sprintf((dl.file_name + *dl.file_name_length + 1),
                                            "%s%c(%s %d)",
                                            SEND_FILE_FTP, SEPARATOR_CHAR,
                                            __FILE__, __LINE__);
                     if (write(dl.fd, dl.data, dl_real_size) != dl_real_size)
                     {
                        system_log(ERROR_SIGN, __FILE__, __LINE__,
                                   "write() error : %s", strerror(errno));
                     }
#endif
                     (void)strcpy(p_fullname, p_file_name_buffer);
                     if (unlink(fullname) == -1)
                     {
                        system_log(WARN_SIGN, __FILE__, __LINE__,
                                   "Failed to unlink() duplicate file `%s' : %s",
                                   fullname, strerror(errno));
                     }
                     trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, NULL,
                               "File `%s' is currently transmitted by job %d. Will NOT send file again!",
                               p_file_name_buffer, j);

                     fsa->job_status[(int)db.job_no].no_of_files_done++;

                     local_file_size += *p_file_size_buffer;
                     local_file_counter += 1;
                     now = time(NULL);
                     if (now >= (last_update_time + LOCK_INTERVAL_TIME))
                     {
                        last_update_time = now;
                        update_tfc(local_file_counter, local_file_size,
                                   p_file_size_buffer, files_to_send,
                                   files_send);
                        local_file_size = 0;
                        local_file_counter = 0;
                     }

                     file_is_duplicate = YES;
                     p_file_name_buffer += MAX_FILENAME_LENGTH;
                     p_file_size_buffer++;
                     if (file_mtime_buffer != NULL)
                     {
                        p_file_mtime_buffer++;
                     }
                     break;
                  }
               } /* for (j = 0; j < allowed_transfers; j++) */

               if (file_is_duplicate == NO)
               {
                  fsa->job_status[(int)db.job_no].file_size_in_use = *p_file_size_buffer;
                  (void)strcpy(fsa->job_status[(int)db.job_no].file_name_in_use,
                               p_file_name_buffer);
#ifdef LOCK_DEBUG
                  unlock_region(fsa_fd, db.lock_offset + LOCK_FIU + db.job_no, __FILE__, __LINE__);
#else
                  unlock_region(fsa_fd, db.lock_offset + LOCK_FIU + db.job_no);
#endif
               }
               else
               {
#ifdef LOCK_DEBUG
                  unlock_region(fsa_fd, db.lock_offset + LOCK_FIU + db.job_no, __FILE__, __LINE__);
#else
                  unlock_region(fsa_fd, db.lock_offset + LOCK_FIU + db.job_no);
#endif
#ifdef WITH_ERROR_QUEUE
                  if (fsa->host_status & ERROR_QUEUE_SET)
                  {
                     remove_from_error_queue(db.job_id, fsa, db.fsa_pos, fsa_fd);
                  }
#endif
                  continue;
               }
            }
            else
            {
               fsa->job_status[(int)db.job_no].file_size_in_use = *p_file_size_buffer;
               (void)strcpy(fsa->job_status[(int)db.job_no].file_name_in_use,
                            p_file_name_buffer);
            }
         }

         (void)strcpy(p_final_filename, p_file_name_buffer);
         (void)strcpy(p_fullname, p_file_name_buffer);

         /* Send file in dot notation? */
         if ((db.lock == DOT) || (db.lock == DOT_VMS))
         {
            (void)strcpy(p_initial_filename, db.lock_notation);
            (void)strcat(p_initial_filename, p_final_filename);
         }
         else
         {
            (void)strcpy(p_initial_filename, p_final_filename);
            if (db.lock == POSTFIX)
            {
               (void)strcat(p_initial_filename, db.lock_notation);
            }
         }

         if (db.special_flag & UNIQUE_LOCKING)
         {
            char *p_end;

            p_end = p_initial_filename + strlen(p_initial_filename);
            (void)sprintf(p_end, ".%u", (unsigned int)db.unique_number);
         }

         if (db.special_flag & SEQUENCE_LOCKING)
         {
            char *p_end;

            p_end = p_initial_filename + strlen(p_initial_filename);

            /*
             * Check if we need to delete an old lock file.
             */
            if ((db.retries > 0) && ((db.special_flag & UNIQUE_LOCKING) == 0))
            {
               (void)sprintf(p_end, "-%u", db.retries - 1);
               if ((status = ftp_dele(initial_filename)) != SUCCESS)
               {
                  trans_log(DEBUG_SIGN, __FILE__, __LINE__, NULL, msg_str,
                            "Failed to delete file `%s' (%d).",
                            initial_filename, status);
               }
               else
               {
                  if (fsa->debug > NORMAL_MODE)
                  {
                     trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                  "Removed file `%s'.", initial_filename);
                  }
               }
            }
            (void)sprintf(p_end, "-%u", db.retries);
         }

         /*
          * Check if the file has not already been partly
          * transmitted. If so, lets first get the size of the
          * remote file, to append it.
          */
         append_offset = 0;
         append_file_number = -1;
         if ((fsa->file_size_offset != -1) &&
             ((db.special_flag & SEQUENCE_LOCKING) == 0) &&
             ((db.special_flag & UNIQUE_LOCKING) == 0) &&
             (db.no_of_restart_files > 0))
         {
            int ii;

            for (ii = 0; ii < db.no_of_restart_files; ii++)
            {
               if ((CHECK_STRCMP(db.restart_file[ii], p_initial_filename) == 0) &&
                   (append_compare(db.restart_file[ii], fullname) == YES))
               {
                  append_file_number = ii;
                  break;
               }
            }
            if (append_file_number != -1)
            {
               if (fsa->file_size_offset == AUTO_SIZE_DETECT)
               {
                  off_t remote_size;

                  if ((status = ftp_size(initial_filename,
                                         &remote_size)) != SUCCESS)
                  {
                     trans_log(DEBUG_SIGN, __FILE__, __LINE__, NULL, msg_str,
                               "Failed to send SIZE command for file `%s' (%d).",
                               initial_filename, status);
                     if (timeout_flag == ON)
                     {
                        timeout_flag = OFF;
                     }
                  }
                  else
                  {
                     append_offset = remote_size;
                     if (fsa->debug > NORMAL_MODE)
                     {
                        trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
#if SIZEOF_OFF_T == 4
                                     "Remote size of `%s' is %ld.",
#else
                                     "Remote size of `%s' is %lld.",
#endif
                                     initial_filename, (pri_off_t)remote_size);
                     }
                  }
               }
               else
               {
                  int  type;
                  char line_buffer[MAX_RET_MSG_LENGTH];

#ifdef WITH_SSL
                  if (db.auth == BOTH)
                  {
                     type = LIST_CMD | ENCRYPT_DATA;
                  }
                  else
                  {
#endif
                     type = LIST_CMD;
#ifdef WITH_SSL
                  }
#endif
                  if ((status = ftp_list(db.mode_flag, type, initial_filename,
                                         line_buffer)) != SUCCESS)
                  {
                     trans_log(DEBUG_SIGN, __FILE__, __LINE__, NULL, msg_str,
                               "Failed to send LIST command for file `%s' (%d).",
                               initial_filename, status);
                     if (timeout_flag == ON)
                     {
                        timeout_flag = OFF;
                     }
                  }
                  else if (line_buffer[0] != '\0')
                       {
                          int  space_count = 0;
                          char *p_end_line;

                          ptr = line_buffer;

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
                                if (*(p_end_line - 1) == '\n')
                                {
                                   *(p_end_line - 1) = '\0';
                                }
                                system_log(WARN_SIGN, __FILE__, __LINE__,
                                           "Assuming <file size offset> for host %s is to large! [%s]",
                                           tr_hostname, line_buffer);
                                space_count = -1;
                                break;
                             }
                          } while (space_count != fsa->file_size_offset);

                          if ((space_count > -1) &&
                              (space_count == fsa->file_size_offset))
                          {
                             char *p_end = ptr;

                             while ((isdigit((int)(*p_end)) != 0) &&
                                    (p_end < p_end_line))
                             {
                                p_end++;
                             }
                             *p_end = '\0';
                             append_offset = (off_t)str2offt(ptr, NULL, 10);
                             if (fsa->debug > NORMAL_MODE)
                             {
                                trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
#if SIZEOF_OFF_T == 4
                                             "Remote size of `%s' is %ld.",
#else
                                             "Remote size of `%s' is %lld.",
#endif
                                             initial_filename,
                                             (pri_off_t)append_offset);
                             }
                          }
                       }
               }
               if (append_offset > 0)
               {
                  fsa->job_status[(int)db.job_no].file_size_done += append_offset;
                  fsa->job_status[(int)db.job_no].file_size_in_use_done = append_offset;
               }
            } /* if (append_file_number != -1) */
         }

         no_of_bytes = 0;
         if ((append_offset < *p_file_size_buffer) ||
             (*p_file_size_buffer == 0))
         {
#ifdef FTP_CTRL_KEEP_ALIVE_INTERVAL
            int keep_alive_timeout = transfer_timeout - 5;

            if (fsa->protocol_options & STAT_KEEPALIVE)
            {
               if (keep_alive_timeout < MIN_KEEP_ALIVE_INTERVAL)
               {
                  keep_alive_timeout = MIN_KEEP_ALIVE_INTERVAL;
               }
            }
#endif

#ifdef _OUTPUT_LOG
            if (db.output_log == YES)
            {
               start_time = times(&tmsdummy);
            }
#endif

            /* Open file on remote site. */
            msg_str[0] = '\0';
            if ((status = ftp_data(initial_filename, append_offset,
                                   db.mode_flag, DATA_WRITE,
                                   db.sndbuf_size)) != SUCCESS)
            {
               if ((db.rename_file_busy != '\0') && (timeout_flag != ON) &&
                   (msg_str[0] != '\0') &&
                   ((lposi(msg_str, "Cannot open or remove a file containing a running program.", 58) != NULL) ||
                    (lposi(msg_str, "Cannot STOR. No permission.", 27) != NULL)))
               {
                  size_t length = strlen(p_initial_filename);

                  p_initial_filename[length] = db.rename_file_busy;
                  p_initial_filename[length + 1] = '\0';
                  msg_str[0] = '\0';
                  if ((status = ftp_data(initial_filename, 0,
                                         db.mode_flag, DATA_WRITE,
                                         db.sndbuf_size)) != SUCCESS)
                  {
                     trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                               "Failed to open remote file `%s' (%d).",
                               initial_filename, status);
                     (void)ftp_quit();
                     exit(eval_timeout(OPEN_REMOTE_ERROR));
                  }
                  else
                  {
                     trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                               "Internal rename to `%s' due to remote error.",
                               initial_filename);
                     if (fsa->debug > NORMAL_MODE)
                     {
                        trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                     "Open remote file `%s'.",
                                     initial_filename);
                     }
                  }
               }
               else
               {
                  trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                            "Failed to open remote file `%s' (%d).",
                            initial_filename, status);
                  (void)ftp_quit();
                  exit(eval_timeout(OPEN_REMOTE_ERROR));
               }
            }
            else
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                               "Open remote file `%s'.", initial_filename);
               }
            }
#ifdef WITH_SSL
            if (db.auth == BOTH)
            {
               if (ftp_auth_data() == INCORRECT)
               {
                  trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                            "TSL/SSL data connection to server `%s' failed.",
                            db.hostname);
                  (void)ftp_quit();
                  exit(AUTH_ERROR);
               }
               else
               {
                  if (fsa->debug > NORMAL_MODE)
                  {
                     trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                  "Authentification successful.");
                  }
               }
            }
#endif

#ifdef FTP_CTRL_KEEP_ALIVE_INTERVAL
            if (fsa->protocol_options & STAT_KEEPALIVE)
            {
               keep_alive_time = time(NULL);
            }
#endif

            /* Open local file. */
#ifdef O_LARGEFILE
            if ((fd = open(fullname, O_RDONLY | O_LARGEFILE)) == -1)
#else
            if ((fd = open(fullname, O_RDONLY)) == -1)
#endif
            {
               trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, NULL,
                         "Failed to open local file `%s' : %s",
                         fullname, strerror(errno));
               (void)ftp_quit();
               exit(OPEN_LOCAL_ERROR);
            }
            if (fsa->debug > NORMAL_MODE)
            {
               trans_db_log(INFO_SIGN, __FILE__, __LINE__, NULL,
                            "Open local file `%s'", fullname);
            }
            if (append_offset > 0)
            {
               if ((*p_file_size_buffer - append_offset) > 0)
               {
                  if (lseek(fd, append_offset, SEEK_SET) < 0)
                  {
                     append_offset = 0;
                     trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, NULL,
                               "Failed to seek() in `%s' (Ignoring append): %s",
                               fullname, strerror(errno));
                  }
                  else
                  {
                     append_count++;
                     if (fsa->debug > NORMAL_MODE)
                     {
                        trans_db_log(INFO_SIGN, __FILE__, __LINE__, NULL,
#if SIZEOF_OFF_T == 4
                                     "Appending file `%s' at %ld.",
#else
                                     "Appending file `%s' at %lld.",
#endif
                                     fullname, (pri_off_t)append_offset);
                     }
                  }
               }
               else
               {
                  append_offset = 0;
               }
            }

#ifdef WITH_EUMETSAT_HEADERS
            if ((db.special_flag & ADD_EUMETSAT_HEADER) &&
                (append_offset == 0) && (db.special_ptr != NULL) &&
                (file_mtime_buffer != NULL))
            {
               size_t header_length;
               char   *p_header;

               if ((p_header = create_eumetsat_header(db.special_ptr,
                                                      (unsigned char)db.special_ptr[4],
                                                      *p_file_size_buffer,
                                                      *p_file_mtime_buffer,
                                                      &header_length)) != NULL)
               {
                  if ((status = ftp_write(p_header, NULL,
                                          header_length)) != SUCCESS)
                  {
                     /*
                      * It could be that we have received a SIGPIPE
                      * signal. If this is the case there might be data
                      * from the remote site on the control connection.
                      * Try to read this data into the global variable
                      * 'msg_str'.
                      */
                     if (status == EPIPE)
                     {
                        (void)ftp_get_reply();
                     }
                     trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL,
                               (status == EPIPE) ? msg_str : NULL,
                               "Failed to write EUMETSAT header to remote file `%s'",
                               initial_filename);
                     if (status == EPIPE)
                     {
                        /*
                         * When pipe is broken no nead to send a QUIT
                         * to the remote side since the connection has
                         * already been closed by the remote side.
                         */
                        trans_log(DEBUG_SIGN, __FILE__, __LINE__, NULL, NULL,
                                  "Hmm. Pipe is broken. Will NOT send a QUIT.");
                     }
                     else
                     {
                        (void)ftp_quit();
                     }
                     exit(eval_timeout(WRITE_REMOTE_ERROR));
                  }
                  if (gsf_check_fsa() != NEITHER)
                  {
                     fsa->job_status[(int)db.job_no].file_size_done += header_length;
                     fsa->job_status[(int)db.job_no].bytes_send += header_length;
                  }
                  free(p_header);
               }
            }
#endif
            if ((db.special_flag & FILE_NAME_IS_HEADER) &&
                (append_offset == 0))
            {
               int header_length,
                   space_count;

               ptr = p_file_name_buffer;
               buffer[0] = 1; /* SOH */
               buffer[1] = '\015'; /* CR */
               buffer[2] = '\015'; /* CR */
               buffer[3] = '\012'; /* LF */
               header_length = 4;
               space_count = 0;

               for (;;)
               {
                  while ((*ptr != '_') && (*ptr != '-') && (*ptr != ' ') &&
                         (*ptr != '\0') && (*ptr != '.') && (*ptr != ';'))
                  {
                     buffer[header_length] = *ptr;
                     header_length++; ptr++;
                  }
                  if ((*ptr == '\0') || (*ptr == '.') || (*ptr == ';'))
                  {
                     break;
                  }
                  else
                  {
                     if (space_count == 2)
                     {
                        if ((isalpha((int)(*(ptr + 1)))) &&
                            (isalpha((int)(*(ptr + 2)))) &&
                            (isalpha((int)(*(ptr + 3)))))
                        {
                           buffer[header_length] = ' ';
                           buffer[header_length + 1] = *(ptr + 1);
                           buffer[header_length + 2] = *(ptr + 2);
                           buffer[header_length + 3] = *(ptr + 3);
                           header_length += 4;
                        }
                        break;
                     }
                     else
                     {
                        buffer[header_length] = ' ';
                        header_length++; ptr++; space_count++;
                     }
                  }
               }
               buffer[header_length] = '\015'; /* CR */
               buffer[header_length + 1] = '\015'; /* CR */
               buffer[header_length + 2] = '\012'; /* LF */
               header_length += 3;

               if (ascii_buffer != NULL)
               {
                  ascii_buffer[0] = 0;
               }
               if ((status = ftp_write(buffer, ascii_buffer,
                                       header_length)) != SUCCESS)
               {
                  /*
                   * It could be that we have received a SIGPIPE
                   * signal. If this is the case there might be data
                   * from the remote site on the control connection.
                   * Try to read this data into the global variable
                   * 'msg_str'.
                   */
                  if (status == EPIPE)
                  {
                     (void)ftp_get_reply();
                  }
                  trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL,
                            (status == EPIPE) ? msg_str : NULL,
                            "Failed to write WMO header to remote file `%s'",
                            initial_filename);
                  if (status == EPIPE)
                  {
                     /*
                      * When pipe is broken no nead to send a QUIT
                      * to the remote side since the connection has
                      * already been closed by the remote side.
                      */
                     trans_log(DEBUG_SIGN, __FILE__, __LINE__, NULL, NULL,
                               "Hmm. Pipe is broken. Will NOT send a QUIT.");
                  }
                  else
                  {
                     (void)ftp_quit();
                  }
                  exit(eval_timeout(WRITE_REMOTE_ERROR));
               }
               if (gsf_check_fsa() != NEITHER)
               {
                  fsa->job_status[(int)db.job_no].file_size_done += header_length;
                  fsa->job_status[(int)db.job_no].bytes_send += header_length;
               }
            }

            if (fsa->trl_per_process > 0)
            {
               init_limit_transfer_rate();
            }

#ifdef WITH_SENDFILE
# if defined (WITH_SSL) || defined (WITH_EUMETSAT_HEADERS)
            if (((db.special_flag & FILE_NAME_IS_HEADER) == 0) &&
#  ifdef WITH_SSL
#   ifdef WITH_EUMETSAT_HEADERS
                (db.auth == NO) &&
#   else
                (db.auth == NO))
#   endif
#  endif
#  ifdef WITH_EUMETSAT_HEADERS
                ((db.special_flag & ADD_EUMETSAT_HEADER) == 0))
#  endif
# else
            if ((db.special_flag & FILE_NAME_IS_HEADER) == 0)
# endif
            {
               off_t offset = append_offset;

               do
               {
# ifdef _SIMULATE_SLOW_TRANSFER
                  (void)sleep(_SIMULATE_SLOW_TRANSFER);
# endif
                  if ((bytes_buffered = ftp_sendfile(fd, &offset,
                                                     blocksize)) < 0)
                  {
                     trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, NULL,
                               "Failed to write %d bytes to remote file `%s' (%d)",
                               blocksize, initial_filename, -bytes_buffered);

                     if (timeout_flag == OFF)
                     {
                        /*
                         * Close the remote file can sometimes provide
                         * useful information, why the write fails.
                         * This however can be dangerous in situations
                         * when people do not use any form of file
                         * logging. Lets see how things work out.
                         */
                        if ((status = ftp_close_data()) != SUCCESS)
                        {
                           trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                                     "Failed to close remote file `%s' (%d).",
                                     initial_filename, status);
                        }
                        else
                        {
                           if (fsa->debug > NORMAL_MODE)
                           {
                              trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                           "Closed data connection for file `%s'.",
                                           initial_filename);
                           }
                        }
                     }
                     (void)ftp_quit();
                     exit(eval_timeout(WRITE_REMOTE_ERROR));
                  }

                  if (bytes_buffered > 0)
                  {
                     if (fsa->trl_per_process > 0)
                     {
                        limit_transfer_rate(bytes_buffered,
                                            fsa->trl_per_process, clktck);
                     }

                     no_of_bytes += bytes_buffered;
                     if (db.fsa_pos != INCORRECT)
                     {
                        if (gsf_check_fsa() != NEITHER)
                        {
                              fsa->job_status[(int)db.job_no].file_size_in_use_done = no_of_bytes + append_offset;
                              fsa->job_status[(int)db.job_no].file_size_done += bytes_buffered;
                              fsa->job_status[(int)db.job_no].bytes_send += bytes_buffered;
                        }
                     }
# ifdef FTP_CTRL_KEEP_ALIVE_INTERVAL
                     if ((db.fsa_pos != INCORRECT) &&
                         (fsa->protocol_options & STAT_KEEPALIVE))
                     {
                        time_t tmp_time = time(NULL);

                        if ((tmp_time - keep_alive_time) >= keep_alive_timeout)
                        {
                           keep_alive_time = tmp_time;
                           if ((status = ftp_keepalive()) != SUCCESS)
                           {
                              trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                                        "Failed to send STAT command (%d).",
                                        status);
                              if (timeout_flag == ON)
                              {
                                 timeout_flag = OFF;
                              }
                           }
                           else if (fsa->debug > NORMAL_MODE)
                                {
                                   trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                                "Send STAT command.");
                                }
                        }
                     }
# endif
                  }
               } while (bytes_buffered > 0);
            }
            else
            {
#endif /* WITH_SENDFILE */
               /* Read (local) and write (remote) file. */
               if (ascii_buffer != NULL)
               {
                  ascii_buffer[0] = 0;
               }
               do
               {
#ifdef _SIMULATE_SLOW_TRANSFER
                  (void)sleep(_SIMULATE_SLOW_TRANSFER);
#endif
                  if ((bytes_buffered = read(fd, buffer, blocksize)) < 0)
                  {
                     trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, NULL,
                               "Could not read() local file `%s' [%d] : %s",
                               fullname, bytes_buffered, strerror(errno));
                     (void)ftp_quit();
                     exit(READ_LOCAL_ERROR);
                  }
                  if (bytes_buffered > 0)
                  {
#ifdef _DEBUG_APPEND
                     if (((status = ftp_write(buffer, ascii_buffer,
                                              bytes_buffered)) != SUCCESS) ||
                         (fsa->job_status[(int)db.job_no].file_size_done > MAX_SEND_BEFORE_APPEND))
#else
                     if ((status = ftp_write(buffer, ascii_buffer,
                                             bytes_buffered)) != SUCCESS)
#endif
                     {
                        /*
                         * It could be that we have received a SIGPIPE
                         * signal. If this is the case there might be data
                         * from the remote site on the control connection.
                         * Try to read this data into the global variable
                         * 'msg_str'.
                         */
                        if (status == EPIPE)
                        {
                           (void)ftp_get_reply();
                        }
                        trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL,
                                  (status == EPIPE) ? msg_str : NULL,
                                  "Failed to write %d bytes to remote file `%s'",
                                  bytes_buffered, initial_filename);
                        if (status == EPIPE)
                        {
                           /*
                            * When pipe is broken no nead to send a QUIT
                            * to the remote side since the connection has
                            * already been closed by the remote side.
                            */
                           trans_log(DEBUG_SIGN, __FILE__, __LINE__, NULL, NULL,
                                     "Hmm. Pipe is broken. Will NOT send a QUIT.");
                        }
                        else
                        {
                           if (timeout_flag == OFF)
                           {
                              /*
                               * Close the remote file can sometimes provide
                               * useful information, why the write fails.
                               * This however can be dangerous in situations
                               * when people do not use any form of file
                               * logging. Lets see how things work out.
                               */
                              if ((status = ftp_close_data()) != SUCCESS)
                              {
                                 trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                                           "Failed to close remote file `%s' (%d).",
                                           initial_filename, status);
                              }
                              else
                              {
                                 if (fsa->debug > NORMAL_MODE)
                                 {
                                    trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                                 "Closed data connection for file `%s'.",
                                                 initial_filename);
                                 }
                              }
                           }
                           (void)ftp_quit();
                        }
                        exit(eval_timeout(WRITE_REMOTE_ERROR));
                     }

                     if (fsa->trl_per_process > 0)
                     {
                        limit_transfer_rate(bytes_buffered, fsa->trl_per_process,
                                            clktck);
                     }

                     no_of_bytes += bytes_buffered;
                     if (db.fsa_pos != INCORRECT)
                     {
                        if (gsf_check_fsa() != NEITHER)
                        {
                              fsa->job_status[(int)db.job_no].file_size_in_use_done = no_of_bytes + append_offset;
                              fsa->job_status[(int)db.job_no].file_size_done += bytes_buffered;
                              fsa->job_status[(int)db.job_no].bytes_send += bytes_buffered;
                        }
                     }
#ifdef FTP_CTRL_KEEP_ALIVE_INTERVAL
                     if (fsa->protocol_options & STAT_KEEPALIVE)
                     {
                        time_t tmp_time = time(NULL);

                        if ((tmp_time - keep_alive_time) >= keep_alive_timeout)
                        {
                           keep_alive_time = tmp_time;
                           if ((status = ftp_keepalive()) != SUCCESS)
                           {
                              trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                                        "Failed to send STAT command (%d).",
                                        status);
                              if (timeout_flag == ON)
                              {
                                 timeout_flag = OFF;
                              }
                           }
                           else if (fsa->debug > NORMAL_MODE)
                                {
                                   trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                                "Send STAT command.");
                                }
                        }
                     }
#endif
                  } /* if (bytes_buffered > 0) */
               } while (bytes_buffered == blocksize);
#ifdef WITH_SENDFILE
            }
#endif

            /*
             * Since there are always some users sending files to the
             * AFD not in dot notation, lets check here if the file size
             * has changed.
             */
            if ((no_of_bytes + append_offset) != *p_file_size_buffer)
            {
               /*
                * Give a warning in the system log, so some action
                * can be taken against the originator.
                */
               receive_log(WARN_SIGN, __FILE__, __LINE__, 0L, db.job_id,
#if SIZEOF_OFF_T == 4
                           "File `%s' for host %s was DEFINITELY send without any locking. Size changed from %ld to %ld.",
#else
                           "File `%s' for host %s was DEFINITELY send without any locking. Size changed from %lld to %lld.",
#endif
                           p_final_filename, fsa->host_dsp_name,
                           (pri_off_t)*p_file_size_buffer,
                           (pri_off_t)(no_of_bytes + append_offset));
            }

            /* Close local file. */
            if (close(fd) == -1)
            {
               system_log(WARN_SIGN, __FILE__, __LINE__,
                          "Failed to close() local file `%s' : %s",
                          p_final_filename, strerror(errno));
               /*
                * Since we usually do not send more then 100 files and
                * sf_ftp() will exit(), there is no point in stopping
                * the transmission.
                */
            }

            if (db.special_flag & FILE_NAME_IS_HEADER)
            {
               buffer[0] = '\015';
               buffer[1] = '\015';
               buffer[2] = '\012';
               buffer[3] = 3;  /* ETX */
               if (ascii_buffer != NULL)
               {
                  ascii_buffer[0] = 0;
               }
               if ((status = ftp_write(buffer, ascii_buffer, 4)) != SUCCESS)
               {
                  /*
                   * It could be that we have received a SIGPIPE
                   * signal. If this is the case there might be data
                   * from the remote site on the control connection.
                   * Try to read this data into the global variable
                   * 'msg_str'.
                   */
                  if (status == EPIPE)
                  {
                     (void)ftp_get_reply();
                  }
                  trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL,
                            (status == EPIPE) ? msg_str : NULL,
                            "Failed to write <CR><CR><LF><ETX> to remote file `%s'",
                            initial_filename);
                  if (status == EPIPE)
                  {
                     /*
                      * When pipe is broken no nead to send a QUIT
                      * to the remote side since the connection has
                      * already been closed by the remote side.
                      */
                     trans_log(DEBUG_SIGN, __FILE__, __LINE__, NULL, NULL,
                               "Hmm. Pipe is broken. Will NOT send a QUIT.");
                  }
                  else
                  {
                     (void)ftp_quit();
                  }
                  exit(eval_timeout(WRITE_REMOTE_ERROR));
               }

               if (db.fsa_pos != INCORRECT)
               {
                  if (gsf_check_fsa() != NEITHER)
                  {
                     fsa->job_status[(int)db.job_no].file_size_done += 4;
                     fsa->job_status[(int)db.job_no].bytes_send += 4;
                  }
               }
            }

            /* Close remote file. */
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
                  trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                            "Failed to close remote file `%s'",
                            initial_filename);
                  (void)ftp_quit();
                  exit(eval_timeout(CLOSE_REMOTE_ERROR));
               }
               else
               {
                  trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                            "Failed to close remote file `%s' (%d). Ignoring since file size is 0.",
                            initial_filename, status);
               }
            }
            else
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                               "Closed data connection for file `%s'.",
                               initial_filename);
               }
            }

#ifdef _OUTPUT_LOG
            if (db.output_log == YES)
            {
               end_time = times(&tmsdummy);
            }
#endif
            if (db.chmod_str[0] != '\0')
            {
               if ((status = ftp_chmod(initial_filename,
                                       db.chmod_str)) != SUCCESS)
               {
                  trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                            "Failed to chmod remote file `%s' to %s (%d).",
                            initial_filename, db.chmod_str, status);
                  if (timeout_flag == ON)
                  {
                     timeout_flag = OFF;
                  }
               }
               else if (fsa->debug > NORMAL_MODE)
                    {
                       trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                    "Changed mode of remote file `%s' to %s",
                                    initial_filename, db.chmod_str);
                    }
            }

            if (fsa->debug > NORMAL_MODE)
            {
               int  type;
               char line_buffer[MAX_RET_MSG_LENGTH];

#ifdef WITH_SSL
               if (db.auth == BOTH)
               {
                  type = LIST_CMD | ENCRYPT_DATA;
               }
               else
               {
#endif
                  type = LIST_CMD;
#ifdef WITH_SSL
               }
#endif
               if ((status = ftp_list(db.mode_flag, type, initial_filename,
                                      line_buffer)) != SUCCESS)
               {
                  trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                            "Failed to list remote file `%s' (%d).",
                            initial_filename, status);
                  if (timeout_flag == ON)
                  {
                     timeout_flag = OFF;
                  }
               }
               else
               {
                  trans_db_log(INFO_SIGN, NULL, 0, NULL, "%s", line_buffer);
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, NULL,
#if SIZEOF_OFF_T == 4
                               "Local file size of `%s' is %ld",
#else
                               "Local file size of `%s' is %lld",
#endif
                               p_final_filename,
                               (pri_off_t)(no_of_bytes + append_offset));
               }
            }
         } /* if (append_offset < p_file_size_buffer) */

         if ((fsa->protocol_options & KEEP_TIME_STAMP) &&
             (file_mtime_buffer != NULL))
         {
            if (ftp_set_date(initial_filename, *p_file_mtime_buffer) != SUCCESS)
            {
               trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "Failed to set remote file modification time of `%s' (%d)",
                         initial_filename, status);
            }
         }

         /* If we used dot notation, don't forget to rename. */
         if ((db.lock == DOT) || (db.lock == POSTFIX) || (db.lock == DOT_VMS) ||
             (db.special_flag & SEQUENCE_LOCKING) ||
             (db.special_flag & UNIQUE_LOCKING) ||
             (db.trans_rename_rule[0] != '\0'))
         {
            *p_remote_filename = '\0';
            if (db.lock == DOT_VMS)
            {
               (void)strcat(p_final_filename, DOT_NOTATION);
            }
            if (db.trans_rename_rule[0] != '\0')
            {
               register int k;

               for (k = 0; k < rule[db.trans_rule_pos].no_of_rules; k++)
               {
                  if (pmatch(rule[db.trans_rule_pos].filter[k],
                             p_final_filename, NULL) == 0)
                  {
                     change_name(p_final_filename,
                                 rule[db.trans_rule_pos].filter[k],
                                 rule[db.trans_rule_pos].rename_to[k],
                                 p_remote_filename, &counter_fd,
                                 &unique_counter, db.job_id);
                     break;
                  }
               }
            }
            if (*p_remote_filename == '\0')
            {
               (void)strcpy(p_remote_filename, p_final_filename);
            }
            if ((status = ftp_move(initial_filename,
                                   remote_filename,
                                   fsa->protocol_options & FTP_FAST_MOVE,
                                   (db.special_flag & CREATE_TARGET_DIR) ? YES : NO)) != SUCCESS)
            {
#ifdef WITH_DUP_CHECK
               trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "Failed to move remote file `%s' to `%s' (%d (crc_id = %x))",
                         initial_filename, remote_filename, status, db.crc_id);
#else
               trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "Failed to move remote file `%s' to `%s' (%d)",
                         initial_filename, remote_filename, status);
#endif
               (void)ftp_quit();
               exit(eval_timeout(MOVE_REMOTE_ERROR));
            }
            else
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                               "Renamed remote file `%s' to `%s'",
                               initial_filename, remote_filename);
               }
            }
            if (db.lock == DOT_VMS)
            {
               /* Remove dot at end of name. */
               ptr = p_final_filename + strlen(p_final_filename) - 1;
               *ptr = '\0';
            }
         }

#ifdef WITH_READY_FILES
         if ((db.lock == READY_A_FILE) || (db.lock == READY_B_FILE))
         {
            int  rdy_length;
            char file_type,
                 ready_file_name[MAX_FILENAME_LENGTH],
                 ready_file_buffer[MAX_PATH_LENGTH + 25];

            /* Generate the name for the ready file. */
            (void)sprintf(ready_file_name, "%s_rdy", final_filename);

            /* Open ready file on remote site. */
            msg_str[0] = '\0';
            if ((status = ftp_data(ready_file_name, append_offset,
                                   db.mode_flag, DATA_WRITE,
                                   db.sndbuf_size)) != SUCCESS)
            {
               trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "Failed to open remote ready file `%s' (%d).",
                         ready_file_name, status);
               (void)ftp_quit();
               exit(eval_timeout(OPEN_REMOTE_ERROR));
            }
            else
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                               "Open remote ready file `%s'", ready_file_name);
               }
            }
# ifdef WITH_SSL
            if (db.auth == BOTH)
            {
               if (ftp_auth_data() == INCORRECT)
               {
                  trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                            "TSL/TSL data connection to server `%s' failed.",
                            db.hostname);
                  (void)ftp_quit();
                  exit(AUTH_ERROR);
               }
               else
               {
                  if (fsa->debug > NORMAL_MODE)
                  {
                     trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                  "Authentification successful.");
                  }
               }
            }
# endif

            /* Create contents for ready file. */
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
                                 p_initial_filename, file_type);

            /* Write remote ready file in one go. */
            if ((status = ftp_write(ready_file_buffer, NULL,
                                    rdy_length)) != SUCCESS)
            {
               /*
                * It could be that we have received a SIGPIPE
                * signal. If this is the case there might be data
                * from the remote site on the control connection.
                * Try to read this data into the global variable
                * 'msg_str'.
                */
               if (status == EPIPE)
               {
                  (void)ftp_get_reply();
               }
               trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL,
                         (status == EPIPE) ? msg_str : NULL,
                         "Failed to write to remote ready file `%s' (%d).",
                         ready_file_name, status);
               if (status == EPIPE)
               {
                  /*
                   * When pipe is broken no nead to send a QUIT
                   * to the remote side since the connection has
                   * already been closed by the remote side.
                   */
                  trans_log(DEBUG_SIGN, __FILE__, __LINE__, NULL, NULL,
                            "Hmm. Pipe is broken. Will NOT send a QUIT.");
               }
               else
               {
                  (void)ftp_quit();
               }
               exit(eval_timeout(WRITE_REMOTE_ERROR));
            }

            /* Close remote ready file. */
            if ((status = ftp_close_data()) != SUCCESS)
            {
               trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "Failed to close remote ready file `%s' (%d).",
                         ready_file_name, status);
               (void)ftp_quit();
               exit(eval_timeout(CLOSE_REMOTE_ERROR));
            }
            else
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                               "Closed remote ready file `%s'",
                               ready_file_name);
               }
            }
         }
#endif /* WITH_READY_FILES */

         if (db.special_flag & EXEC_FTP)
         {
            char *p_name;

            if (db.trans_rename_rule[0] != '\0')
            {
               p_name = remote_filename;
            }
            else
            {
               p_name = final_filename;
            }
            if ((status = ftp_exec(db.special_ptr, p_name)) != SUCCESS)
            {
               trans_log(WARN_SIGN, __FILE__, __LINE__, NULL, msg_str,
                         "Failed to send SITE %s %s (%d).",
                         db.special_ptr, p_name, status);
               if (timeout_flag == ON)
               {
                  timeout_flag = OFF;
               }
            }
            else if (fsa->debug > NORMAL_MODE)
                 {
                    trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                                 "Send SITE %s %s", db.special_ptr, p_name);
                 }
         }

         /* Update FSA, one file transmitted. */
         if (gsf_check_fsa() != NEITHER)
         {
            fsa->job_status[(int)db.job_no].file_name_in_use[0] = '\0';
            fsa->job_status[(int)db.job_no].no_of_files_done++;
            fsa->job_status[(int)db.job_no].file_size_in_use = 0;
            fsa->job_status[(int)db.job_no].file_size_in_use_done = 0;
            local_file_size += *p_file_size_buffer;
            local_file_counter += 1;

            now = time(NULL);
            if (now >= (last_update_time + LOCK_INTERVAL_TIME))
            {
               last_update_time = now;
               update_tfc(local_file_counter, local_file_size,
                          p_file_size_buffer, files_to_send,
                          files_send);
               local_file_size = 0;
               local_file_counter = 0;
            }
         }

         if (append_file_number != -1)
         {
            /* This file was appended, so lets remove it */
            /* from the append list in the message file. */
            remove_append(db.job_id, db.restart_file[append_file_number]);
         }

#ifdef _WITH_TRANS_EXEC
         if (db.special_flag & TRANS_EXEC)
         {
            trans_exec(file_path, fullname, p_file_name_buffer);
         }
#endif

#ifdef _OUTPUT_LOG
         if (db.output_log == YES)
         {
            if (ol_fd == -2)
            {
# ifdef WITHOUT_FIFO_RW_SUPPORT
               output_log_fd(&ol_fd, &ol_readfd);
# else
               output_log_fd(&ol_fd);
# endif
            }
            if ((ol_fd > -1) && (ol_data == NULL))
            {
               output_log_ptrs(&ol_retries,
                               &ol_job_number,
                               &ol_data,              /* Pointer to buffer.       */
                               &ol_file_name,
                               &ol_file_name_length,
                               &ol_archive_name_length,
                               &ol_file_size,
                               &ol_unl,
                               &ol_size,
                               &ol_transfer_time,
                               &ol_output_type,
                               db.host_alias,
                               (current_toggle - 1),
# ifdef WITH_SSL
                               (db.auth == NO) ? FTP : FTPS);
# else
                               FTP);
# endif
            }
         }
#endif

         /* Now archive file if necessary. */
         if ((db.archive_time > 0) &&
             (p_db->archive_dir[0] != FAILED_TO_CREATE_ARCHIVE_DIR))
         {
            /*
             * By telling the function archive_file() that this
             * is the first time to archive a file for this job
             * (in struct p_db) it does not always have to check
             * whether the directory has been created or not. And
             * we ensure that we do not create duplicate names
             * when adding db.archive_time to msg_name.
             */
            if (archive_file(file_path, p_file_name_buffer, p_db) < 0)
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  trans_db_log(ERROR_SIGN, __FILE__, __LINE__, NULL,
                               "Failed to archive file `%s'",
                               p_file_name_buffer);
               }

               /*
                * NOTE: We _MUST_ delete the file we just send,
                *       else the file directory will run full!
                */
               if ((unlink(fullname) == -1) && (errno != ENOENT))
               {
                  system_log(ERROR_SIGN, __FILE__, __LINE__,
                             "Could not unlink() local file `%s' after sending it successfully : %s",
                             fullname, strerror(errno));
               }

#ifdef _OUTPUT_LOG
               if (db.output_log == YES)
               {
                  (void)memcpy(ol_file_name, db.p_unique_name, db.unl);
                  if (db.trans_rename_rule[0] == '\0')
                  {
                     (void)strcpy(ol_file_name + db.unl, p_file_name_buffer);
                     *ol_file_name_length = (unsigned short)strlen(ol_file_name);
                     ol_file_name[*ol_file_name_length] = SEPARATOR_CHAR;
                     ol_file_name[*ol_file_name_length + 1] = '\0';
                     (*ol_file_name_length)++;
                  }
                  else
                  {
                     *ol_file_name_length = (unsigned short)sprintf(ol_file_name + db.unl,
                                                                    "%s%c%s",
                                                                    p_file_name_buffer,
                                                                    SEPARATOR_CHAR,
                                                                    p_remote_filename) +
                                                                    db.unl;
                  }
                  *ol_file_size = no_of_bytes + append_offset;
                  *ol_job_number = db.job_id;
                  *ol_retries = db.retries;
                  *ol_unl = db.unl;
                  *ol_transfer_time = end_time - start_time;
                  *ol_archive_name_length = 0;
                  *ol_output_type = OT_NORMAL_DELIVERED + '0';
                  ol_real_size = *ol_file_name_length + ol_size;
                  if (write(ol_fd, ol_data, ol_real_size) != ol_real_size)
                  {
                     system_log(ERROR_SIGN, __FILE__, __LINE__,
                                "write() error : %s", strerror(errno));
                  }
               }
#endif /* _OUTPUT_LOG */
            }
            else
            {
               if (fsa->debug > NORMAL_MODE)
               {
                  trans_db_log(INFO_SIGN, __FILE__, __LINE__, NULL,
                               "Archived file `%s'", p_final_filename);
               }

#ifdef _OUTPUT_LOG
               if (db.output_log == YES)
               {
                  (void)memcpy(ol_file_name, db.p_unique_name, db.unl);
                  if (db.trans_rename_rule[0] == '\0')
                  {
                     (void)strcpy(ol_file_name + db.unl, p_file_name_buffer);
                     *ol_file_name_length = (unsigned short)strlen(ol_file_name);
                     ol_file_name[*ol_file_name_length] = SEPARATOR_CHAR;
                     ol_file_name[*ol_file_name_length + 1] = '\0';
                     (*ol_file_name_length)++;
                  }
                  else
                  {
                     *ol_file_name_length = (unsigned short)sprintf(ol_file_name + db.unl,
                                                                    "%s%c%s",
                                                                    p_file_name_buffer,
                                                                    SEPARATOR_CHAR,
                                                                    p_remote_filename) +
                                                                    db.unl;
                  }
                  (void)strcpy(&ol_file_name[*ol_file_name_length + 1],
                               &db.archive_dir[db.archive_offset]);
                  *ol_file_size = no_of_bytes + append_offset;
                  *ol_job_number = db.job_id;
                  *ol_retries = db.retries;
                  *ol_unl = db.unl;
                  *ol_transfer_time = end_time - start_time;
                  *ol_archive_name_length = (unsigned short)strlen(&ol_file_name[*ol_file_name_length + 1]);
                  *ol_output_type = OT_NORMAL_DELIVERED + '0';
                  ol_real_size = *ol_file_name_length +
                                 *ol_archive_name_length + 1 + ol_size;
                  if (write(ol_fd, ol_data, ol_real_size) != ol_real_size)
                  {
                     system_log(ERROR_SIGN, __FILE__, __LINE__,
                                "write() error : %s", strerror(errno));
                  }
               }
#endif /* _OUTPUT_LOG */
            }
         }
         else
         {
#ifdef WITH_UNLINK_DELAY
            int unlink_loops = 0;

try_again_unlink:
#endif
            /* Delete the file we just have send. */
            if (unlink(fullname) == -1)
            {
#ifdef WITH_UNLINK_DELAY
               if ((errno == EBUSY) && (unlink_loops < 20))
               {
                  (void)my_usleep(100000L);
                  unlink_loops++;
                  goto try_again_unlink;
               }
#endif
               system_log(ERROR_SIGN, __FILE__, __LINE__,
                          "Could not unlink() local file `%s' after sending it successfully : %s",
                          fullname, strerror(errno));
            }

#ifdef _OUTPUT_LOG
            if (db.output_log == YES)
            {
               (void)memcpy(ol_file_name, db.p_unique_name, db.unl);
               if (db.trans_rename_rule[0] == '\0')
               {
                  (void)strcpy(ol_file_name + db.unl, p_file_name_buffer);
                  *ol_file_name_length = (unsigned short)strlen(ol_file_name);
                  ol_file_name[*ol_file_name_length] = SEPARATOR_CHAR;
                  ol_file_name[*ol_file_name_length + 1] = '\0';
                  (*ol_file_name_length)++;
               }
               else
               {
                  *ol_file_name_length = (unsigned short)sprintf(ol_file_name + db.unl,
                                                                 "%s%c%s",
                                                                 p_file_name_buffer,
                                                                 SEPARATOR_CHAR,
                                                                 p_remote_filename) +
                                                                 db.unl;
               }
               *ol_file_size = no_of_bytes + append_offset;
               *ol_job_number = db.job_id;
               *ol_retries = db.retries;
               *ol_unl = db.unl;
               *ol_transfer_time = end_time - start_time;
               *ol_archive_name_length = 0;
               *ol_output_type = OT_NORMAL_DELIVERED + '0';
               ol_real_size = *ol_file_name_length + ol_size;
               if (write(ol_fd, ol_data, ol_real_size) != ol_real_size)
               {
                  system_log(ERROR_SIGN, __FILE__, __LINE__,
                             "write() error : %s", strerror(errno));
               }
            }
#endif /* _OUTPUT_LOG */
         }

         /*
          * After each successful transfer set error counter to zero,
          * so that other jobs can be started.
          */
         if (fsa->error_counter > 0)
         {
            int  fd,
#ifdef WITHOUT_FIFO_RW_SUPPORT
                 readfd,
#endif
                 j;
            char fd_wake_up_fifo[MAX_PATH_LENGTH];

#ifdef LOCK_DEBUG
            lock_region_w(fsa_fd, db.lock_offset + LOCK_EC, __FILE__, __LINE__);
#else
            lock_region_w(fsa_fd, db.lock_offset + LOCK_EC);
#endif
            fsa->error_counter = 0;

            /*
             * Wake up FD!
             */
            (void)sprintf(fd_wake_up_fifo, "%s%s%s", p_work_dir,
                          FIFO_DIR, FD_WAKE_UP_FIFO);
#ifdef WITHOUT_FIFO_RW_SUPPORT
            if (open_fifo_rw(fd_wake_up_fifo, &readfd, &fd) == -1)
#else
            if ((fd = open(fd_wake_up_fifo, O_RDWR)) == -1)
#endif
            {
               system_log(WARN_SIGN, __FILE__, __LINE__,
                          "Failed to open() FIFO `%s' : %s",
                          fd_wake_up_fifo, strerror(errno));
            }
            else
            {
               if (write(fd, "", 1) != 1)
               {
                  system_log(WARN_SIGN, __FILE__, __LINE__,
                             "Failed to write() to FIFO `%s' : %s",
                             fd_wake_up_fifo, strerror(errno));
               }
#ifdef WITHOUT_FIFO_RW_SUPPORT
               if (close(readfd) == -1)
               {
                  system_log(DEBUG_SIGN, __FILE__, __LINE__,
                             "Failed to close() FIFO `%s' (read) : %s",
                             fd_wake_up_fifo, strerror(errno));
               }
#endif
               if (close(fd) == -1)
               {
                  system_log(DEBUG_SIGN, __FILE__, __LINE__,
                             "Failed to close() FIFO `%s' : %s",
                             fd_wake_up_fifo, strerror(errno));
               }
            }

            /*
             * Remove the error condition (NOT_WORKING) from all jobs
             * of this host.
             */
            for (j = 0; j < fsa->allowed_transfers; j++)
            {
               if ((j != db.job_no) &&
                   (fsa->job_status[j].connect_status == NOT_WORKING))
               {
                  fsa->job_status[j].connect_status = DISCONNECT;
               }
            }
            fsa->error_history[0] = 0;
            fsa->error_history[1] = 0;
#ifdef LOCK_DEBUG
            unlock_region(fsa_fd, db.lock_offset + LOCK_EC, __FILE__, __LINE__);
#else
            unlock_region(fsa_fd, db.lock_offset + LOCK_EC);
#endif

            /*
             * Since we have successfully transmitted a file, no need to
             * have the queue stopped anymore.
             */
            if (fsa->host_status & AUTO_PAUSE_QUEUE_STAT)
            {
               char *sign;

#ifdef LOCK_DEBUG
               lock_region_w(fsa_fd, db.lock_offset + LOCK_HS, __FILE__, __LINE__);
#else
               lock_region_w(fsa_fd, db.lock_offset + LOCK_HS);
#endif
               fsa->host_status &= ~AUTO_PAUSE_QUEUE_STAT;
               if (fsa->host_status & HOST_ERROR_EA_STATIC)
               {
                  fsa->host_status &= ~EVENT_STATUS_STATIC_FLAGS;
               }
               else
               {
                  fsa->host_status &= ~EVENT_STATUS_FLAGS;
               }
               fsa->host_status &= ~PENDING_ERRORS;
#ifdef LOCK_DEBUG
               unlock_region(fsa_fd, db.lock_offset + LOCK_HS, __FILE__, __LINE__);
#else
               unlock_region(fsa_fd, db.lock_offset + LOCK_HS);
#endif
               error_action(fsa->host_alias, "stop", HOST_ERROR_ACTION);
               event_log(0L, EC_HOST, ET_EXT, EA_ERROR_END, "%s",
                         fsa->host_alias);
               if ((fsa->host_status & HOST_ERROR_OFFLINE_STATIC) ||
                   (fsa->host_status & HOST_ERROR_OFFLINE) ||
                   (fsa->host_status & HOST_ERROR_OFFLINE_T))
               {
                  sign = OFFLINE_SIGN;
               }
               else
               {
                  sign = INFO_SIGN;
               }
               trans_log(sign, __FILE__, __LINE__, NULL, NULL,
                         "Starting input queue that was stopped by init_afd.");
               event_log(0L, EC_HOST, ET_AUTO, EA_START_QUEUE, "%s",
                         fsa->host_alias);
            }
         } /* if (fsa->error_counter > 0) */
#ifdef WITH_ERROR_QUEUE
         if (fsa->host_status & ERROR_QUEUE_SET)
         {
            remove_from_error_queue(db.job_id, fsa, db.fsa_pos, fsa_fd);
         }
#endif
         if (fsa->host_status & HOST_ACTION_SUCCESS)
         {
            error_action(fsa->host_alias, "start", HOST_SUCCESS_ACTION);
         }

#ifdef _WITH_INTERRUPT_JOB
         if ((fsa->job_status[(int)db.job_no].special_flag & INTERRUPT_JOB) &&
             ((files_send + 1) < files_to_send))
         {
            interrupt = YES;
            break;
         }
#endif

         p_file_name_buffer += MAX_FILENAME_LENGTH;
         p_file_size_buffer++;
         if (file_mtime_buffer != NULL)
         {
            p_file_mtime_buffer++;
         }
      } /* for (files_send = 0; files_send < files_to_send; files_send++) */

      if (local_file_counter)
      {
         if (gsf_check_fsa() != NEITHER)
         {
            update_tfc(local_file_counter, local_file_size,
                       p_file_size_buffer, files_to_send, files_send);
            local_file_size = 0;
            local_file_counter = 0;
         }
      }

      /* Do not forget to remove lock file if we have created one. */
      if ((db.lock == LOCKFILE) && (fsa->active_transfers == 1))
      {
         if ((status = ftp_dele(db.lock_file_name)) != SUCCESS)
         {
            trans_log(ERROR_SIGN, __FILE__, __LINE__, NULL, msg_str,
                      "Failed to remove remote lock file `%s' (%d)",
                      db.lock_file_name, status);
            (void)ftp_quit();
            exit(eval_timeout(REMOVE_LOCKFILE_ERROR));
         }
         else
         {
            if (fsa->debug > NORMAL_MODE)
            {
               trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str,
                            "Removed lock file `%s'.", db.lock_file_name);
            }
         }
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
          (fsa->total_file_counter == 0))
      {
         remove_all_appends(db.job_id);
      }

#ifdef _WITH_INTERRUPT_JOB
      if (interrupt == NO)
      {
#endif
         /*
          * Remove file directory.
          */
         if (rmdir(file_path) == -1)
         {
            system_log(ERROR_SIGN, __FILE__, __LINE__,
                       "Failed to remove directory `%s' : %s [PID = %d] [job_no = %d]",
                       file_path, strerror(errno), db.my_pid, (int)db.job_no);
            exit_status = STILL_FILES_TO_SEND;
         }
#ifdef _WITH_INTERRUPT_JOB
      }
#endif

#ifdef _WITH_BURST_2
      burst_2_counter++;
      total_append_count += append_count;
      append_count = 0;
   } while ((cb2_ret = check_burst_2(file_path, &files_to_send, 0,
# ifdef _WITH_INTERRUPT_JOB
                                     interrupt,
# endif
# ifdef _OUTPUT_LOG
                                     &ol_fd,
# endif
# ifndef AFDBENCH_CONFIG
                                     &total_append_count,
# endif
                                     &values_changed)) == YES);
   burst_2_counter--;

   if (cb2_ret == NEITHER)
   {
      exit_status = STILL_FILES_TO_SEND;
   }
#endif /* _WITH_BURST_2 */

   fsa->job_status[(int)db.job_no].connect_status = CLOSING_CONNECTION;
   free(buffer);

#ifdef _CHECK_BEFORE_EXIT
#endif

   /* Logout again. */
   if ((status = ftp_quit()) != SUCCESS)
   {
      trans_log(WARN_SIGN, __FILE__, __LINE__, NULL,
                (status == INCORRECT) ? NULL : msg_str,
                "Failed to disconnect from remote host (%d).", status);

      /*
       * Since all files have been transfered successful it is
       * not necessary to indicate an error in the status display.
       * It's enough when we say in the Transfer log that we failed
       * to log out.
       */
   }
   else
   {
      if (fsa->debug > NORMAL_MODE)
      {
         trans_db_log(INFO_SIGN, __FILE__, __LINE__, msg_str, "Logged out.");
      }
   }

   /* Don't need the ASCII buffer. */
   if (ascii_buffer != NULL)
   {
      free(ascii_buffer);
   }

   exitflag = 0;
   exit(exit_status);
}


/*+++++++++++++++++++++++++++++ sf_ftp_exit() +++++++++++++++++++++++++++*/
static void
sf_ftp_exit(void)
{
   if ((fsa != NULL) && (db.fsa_pos >= 0))
   {
      int     diff_no_of_files_done;
      u_off_t diff_file_size_done;

      if (local_file_counter)
      {
         if (gsf_check_fsa() != NEITHER)
         {
            update_tfc(local_file_counter, local_file_size,
                       p_file_size_buffer, files_to_send, files_send);
         }
      }

      diff_no_of_files_done = fsa->job_status[(int)db.job_no].no_of_files_done -
                              prev_no_of_files_done;
      diff_file_size_done = fsa->job_status[(int)db.job_no].file_size_done -
                            prev_file_size_done;
      if ((diff_file_size_done > 0) || (diff_no_of_files_done > 0))
      {
         int  length;
#ifdef _WITH_BURST_2
         char buffer[MAX_INT_LENGTH + 5 + MAX_OFF_T_LENGTH + 16 + MAX_INT_LENGTH + 21 + MAX_INT_LENGTH + 11 + MAX_INT_LENGTH + 1];
#else
         char buffer[MAX_INT_LENGTH + 5 + MAX_OFF_T_LENGTH + 16 + MAX_INT_LENGTH + 21 + MAX_INT_LENGTH + 1];
#endif

         WHAT_DONE_BUFFER(length, buffer, "send", diff_file_size_done,
                          diff_no_of_files_done);
#ifdef _WITH_BURST_2
         if (total_append_count == 1)
         {
            (void)strcpy(&buffer[length], " [APPEND]");
            length += 9;
         }
         else if (total_append_count > 1)
              {
                 length += sprintf(&buffer[length], " [APPEND * %u]",
                                   total_append_count);
              }
         if (burst_2_counter == 1)
         {
            (void)strcpy(&buffer[length], " [BURST]");
         }
         else if (burst_2_counter > 1)
              {
                 (void)sprintf(&buffer[length], " [BURST * %u]",
                               burst_2_counter);
           }
#else
         if (append_count == 1)
         {
            (void)strcat(buffer, " [APPEND]");
         }
         else if (append_count > 1)
              {
                 char tmp_buffer[13 + MAX_INT_LENGTH];

                 (void)sprintf(tmp_buffer, " [APPEND * %d]", append_count);
                 (void)strcat(buffer, tmp_buffer);
              }
#endif
         trans_log(INFO_SIGN, NULL, 0, NULL, NULL, "%s", buffer);
      }

      if ((fsa->job_status[(int)db.job_no].file_name_in_use[0] != '\0') &&
          (fsa->file_size_offset != -1) &&
          (append_offset == 0) &&
          (fsa->job_status[(int)db.job_no].file_size_done > MAX_SEND_BEFORE_APPEND))
      {
         log_append(&db, p_initial_filename,
                    fsa->job_status[(int)db.job_no].file_name_in_use);
      }
      reset_fsa((struct job *)&db, exitflag);
   }

   if (file_name_buffer != NULL)
   {
      free(file_name_buffer);
   }
   if (file_size_buffer != NULL)
   {
      free(file_size_buffer);
   }

   send_proc_fin(NO);
   if (sys_log_fd != STDERR_FILENO)
   {
      (void)close(sys_log_fd);
   }

   return;
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
   exitflag = 0;
   if (fsa->job_status[(int)db.job_no].unique_name[2] == 5)
   {
      exit(SUCCESS);
   }
   else
   {
      exit(GOT_KILLED);
   }
}


/*++++++++++++++++++++++++++++++ sig_exit() +++++++++++++++++++++++++++++*/
static void
sig_exit(int signo)
{
   exit(INCORRECT);
}
