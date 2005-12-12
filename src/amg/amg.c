/*
 *  amg.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1995 - 2005 Deutscher Wetterdienst (DWD),
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
 **   amg - creates messages for the FD (File Distributor)
 **
 ** SYNOPSIS
 **   amg [-r          rescan time
 **        -w          working directory
 **        --version   show current version]
 **
 ** DESCRIPTION
 **   The AMG (Automatic Message Generator) searches certain directories
 **   for files to then generate a message for the process FD (File
 **   Distributor). The directories where the AMG must search are
 **   specified in the DIR_CONFIG file. When it generates the message
 **   it also moves all the files from the 'user' directory to a unique
 **   directory, so the FD just needs to send all files which are in
 **   this directory. Since the message name and the directory name are
 **   the same, the FD will need no further information to get the
 **   files.
 **
 **   These 'user'-directories are scanned every DEFAULT_RESCAN_TIME
 **   (5 seconds) time. It also checks if there are any changes made to
 **   the DIR_CONFIG or HOST_CONFIG file. If so, it will reread them,
 **   stop all its process, create a new shared memory area and restart
 **   all jobs again (only if the DIR_CONFIG changes). Thus, it is not
 **   necessary to stop the AFD when entering a new host entry or removing
 **   one.
 **
 **   The AMG is also able to receive commands via the AFD_CMD_FIFO
 **   fifo from the AFD. So far only one command is recognised: STOP.
 **   This is used when the user wants to stop only the AMG or when
 **   the AFD is shutdown.
 **
 ** RETURN VALUES
 **   SUCCESS on normal exit and INCORRECT when an error has occurred.
 **   When it failes to start any of its jobs because they cannot
 **   access there shared memory area it will exit and return the value
 **   3. So the process init_afd can restart it.
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   10.08.1995 H.Kiehl Created
 **   31.08.1997 H.Kiehl Remove check for HOST_CONFIG file.
 **   19.11.1997 H.Kiehl Return of the HOST_CONFIG file!
 **   17.03.2003 H.Kiehl Support for reading multiple DIR_CONFIG files.
 **   16.07.2005 H.Kiehl Made old_file_time and delete_files_flag
 **                      configurable via AFD_CONFIG.
 **
 */
DESCR__E_M1

#include <stdio.h>                    /* fprintf(), sprintf()            */
#include <string.h>                   /* strcpy(), strcat(), strerror()  */
#include <stdlib.h>                   /* atexit(), abort()               */
#include <time.h>                     /* ctime(), time()                 */
#include <sys/types.h>                /* fdset                           */
#include <sys/stat.h>
#include <sys/time.h>                 /* struct timeval                  */
#include <sys/wait.h>                 /* waitpid()                       */
#ifdef HAVE_MMAP
#include <sys/mman.h>                 /* mmap(), munmap()                */
#endif
#include <signal.h>                   /* kill(), signal()                */
#include <unistd.h>                   /* select(), read(), write(),      */
                                      /* close(), unlink(), sleep()      */
#ifdef HAVE_FCNTL_H
#include <fcntl.h>                    /* O_RDWR, O_CREAT, O_WRONLY, etc  */
                                      /* open()                          */
#endif
#include <errno.h>
#include "amgdefs.h"
#include "version.h"

/* Global variables */
#ifdef _DEBUG
FILE                       *p_debug_file;
#endif
int                        dnb_fd,
                           data_length, /* The size of data for one job. */
                           default_delete_files_flag = 0,
                           default_old_file_time = -1,
                           max_process_per_dir = MAX_PROCESS_PER_DIR,
                           *no_of_dir_names,
                           no_of_dirs,
                           no_of_hosts,
                           no_of_local_dir,  /* Number of local          */
                                             /* directories found in the */
                                             /* DIR_CONFIG file.         */
                           no_of_dir_configs,
                           fra_fd = -1,
                           fra_id,
                           fsa_fd = -1,
                           fsa_id,
                           remove_unused_hosts = NO,
                           sys_log_fd = STDERR_FILENO,
                           stop_flag = 0,
                           amg_flag = YES;
unsigned int               max_copied_files = MAX_COPIED_FILES;
pid_t                      dc_pid;      /* dir_check pid                 */
off_t                      fra_size,
                           fsa_size,
                           max_copied_file_size = MAX_COPIED_FILE_SIZE * MAX_COPIED_FILE_SIZE_UNIT;
mode_t                     create_source_dir_mode = DIR_MODE;
#ifdef HAVE_MMAP
off_t                      afd_active_size;
#endif
char                       *p_work_dir,
                           *pid_list,
                           *host_config_file;
struct host_list           *hl;
struct fileretrieve_status *fra;
struct filetransfer_status *fsa = NULL;
struct afd_status          *p_afd_status;
struct dir_name_buf        *dnb = NULL;
struct dir_config_buf      *dcl;
#ifdef _DELETE_LOG
struct delete_log          dl;
#endif
const char                 *sys_log_name = SYSTEM_LOG_FIFO;

/* local functions    */
static void                amg_exit(void),
                           get_afd_config_value(int *, int *, int *, mode_t *,
                                                unsigned int *, off_t *,
                                                int *, int *, int *),
                           notify_dir_check(void),
                           sig_segv(int),
                           sig_bus(int),
                           sig_exit(int);


/*$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ main() $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$*/
int
main(int argc, char *argv[])
{
   int              i,
                    amg_cmd_fd = 0,          /* File descriptor of the   */
                                             /* used by the controlling  */
                                             /* program AFD.             */
                    db_update_fd = 0,        /* If the dialog for creat- */
                                             /* ing a new database has a */
                                             /* new database it can      */
                                             /* notify AMG over this     */
                                             /* fifo.                    */
                    max_fd = 0,              /* Biggest file descriptor. */
                    afd_active_fd,           /* File descriptor to file  */
                                             /* holding all active pid's */
                                             /* of the AFD.              */
                    status = 0,
                    fd,
                    rescan_time = DEFAULT_RESCAN_TIME,
                    max_no_proc = MAX_NO_OF_DIR_CHECKS;
   time_t           hc_old_time;
   char             buffer[10],
                    work_dir[MAX_PATH_LENGTH],
                    *ptr;
   fd_set           rset;
   struct timeval   timeout;
   struct stat      stat_buf;
#ifdef SA_FULLDUMP
   struct sigaction sact;
#endif

   CHECK_FOR_VERSION(argc, argv);

#ifdef _DEBUG
   /* Open debug file for writing */
   if ((p_debug_file = fopen("amg.debug", "w")) == NULL)
   {
      (void)fprintf(stderr, "ERROR   : Could not open %s : %s (%s %d)\n",
                    "amg.debug", strerror(errno), __FILE__, __LINE__);
      exit(INCORRECT);
   }
#endif

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

   /* Do some cleanups when we exit */
   if (atexit(amg_exit) != 0)
   {
      system_log(FATAL_SIGN, __FILE__, __LINE__,
                 "Could not register exit function : %s", strerror(errno));
      exit(INCORRECT);
   }
   if ((signal(SIGINT, sig_exit) == SIG_ERR) ||
       (signal(SIGQUIT, sig_exit) == SIG_ERR) ||
       (signal(SIGTERM, sig_exit) == SIG_ERR) ||
       (signal(SIGSEGV, sig_segv) == SIG_ERR) ||
       (signal(SIGBUS, sig_bus) == SIG_ERR) ||
       (signal(SIGHUP, SIG_IGN) == SIG_ERR))
   {
      system_log(FATAL_SIGN, __FILE__, __LINE__,
                 "Could not set signal handler : %s", strerror(errno));
      exit(INCORRECT);
   }
   
   /* Check syntax if necessary */
   if (get_afd_path(&argc, argv, work_dir) < 0)
   {
      exit(INCORRECT);
   }
   p_work_dir = work_dir;
   (void)umask(0);

   /*
    * Lock AMG so no other AMG can be started!
    */
   if ((ptr = lock_proc(AMG_LOCK_ID, NO)) != NULL)
   {
      (void)fprintf(stderr, "Process AMG already started by %s : (%s %d)\n",
                    ptr, __FILE__, __LINE__);
      system_log(ERROR_SIGN, __FILE__, __LINE__,
                 "Process AMG already started by %s", ptr);
      _exit(INCORRECT);
   }
   else
   {
      int   first_time = NO;
      off_t db_size;
      char  afd_active_file[MAX_PATH_LENGTH],
            amg_cmd_fifo[MAX_PATH_LENGTH],
            counter_file[MAX_PATH_LENGTH],
            db_update_fifo[MAX_PATH_LENGTH],
            dc_cmd_fifo[MAX_PATH_LENGTH],
            dc_resp_fifo[MAX_FILENAME_LENGTH];

      if ((host_config_file = malloc((strlen(work_dir) + strlen(ETC_DIR) +
                                      strlen(DEFAULT_HOST_CONFIG_FILE) + 1))) == NULL)
      {
         system_log(ERROR_SIGN, __FILE__, __LINE__,
                    "malloc() error : %s", strerror(errno));
         _exit(INCORRECT);
      }
      (void)strcpy(host_config_file, work_dir);
      (void)strcat(host_config_file, ETC_DIR);
      (void)strcat(host_config_file, DEFAULT_HOST_CONFIG_FILE);

      /* Initialise variables with default values */
      (void)strcpy(amg_cmd_fifo, work_dir);
      (void)strcat(amg_cmd_fifo, FIFO_DIR);
      (void)strcpy(dc_cmd_fifo, amg_cmd_fifo);
      (void)strcat(dc_cmd_fifo, DC_CMD_FIFO);
      (void)strcpy(dc_resp_fifo, amg_cmd_fifo);
      (void)strcat(dc_resp_fifo, DC_RESP_FIFO);
      (void)strcpy(db_update_fifo, amg_cmd_fifo);
      (void)strcat(db_update_fifo, DB_UPDATE_FIFO);
      (void)strcpy(counter_file, amg_cmd_fifo);
      (void)strcat(counter_file, COUNTER_FILE);
      (void)strcpy(afd_active_file, amg_cmd_fifo);
      (void)strcat(afd_active_file, AFD_ACTIVE_FILE);
      (void)strcat(amg_cmd_fifo, AMG_CMD_FIFO);

      if (attach_afd_status(NULL) < 0)
      {
         system_log(FATAL_SIGN, __FILE__, __LINE__,
                    "Failed to attach to AFD status shared area.");
         exit(INCORRECT);
      }

      /*
       * We need to write the pid of dir_check to the AFD_ACTIVE file.
       * Otherwise it can happen that two or more dir_check's run at
       * the same time, when init_afd was killed.
       */
      if ((afd_active_fd = coe_open(afd_active_file, O_RDWR)) == -1)
      {
         system_log(WARN_SIGN, __FILE__, __LINE__,
                    "Could not open() %s : %s",
                    afd_active_file, strerror(errno));
         pid_list = NULL;
      }
      else
      {
         if (fstat(afd_active_fd, &stat_buf) < 0)
         {
            system_log(WARN_SIGN, __FILE__, __LINE__,
                       "Could not fstat() %s : %s",
                       afd_active_file, strerror(errno));
            (void)close(afd_active_fd);
            pid_list = NULL;
         }
         else
         {
#ifdef HAVE_MMAP
            if ((pid_list = mmap(0, stat_buf.st_size,
                                 (PROT_READ | PROT_WRITE), MAP_SHARED,
                                 afd_active_fd, 0)) == (caddr_t) -1)
#else
            if ((pid_list = mmap_emu(0, stat_buf.st_size,
                                     (PROT_READ | PROT_WRITE), MAP_SHARED,
                                     afd_active_file, 0)) == (caddr_t) -1)
#endif
            {
               system_log(WARN_SIGN, __FILE__, __LINE__,
                          "mmap() error : %s", strerror(errno));
               pid_list = NULL;
            }
            afd_active_size = stat_buf.st_size;

            if (close(afd_active_fd) == -1)
            {
               system_log(DEBUG_SIGN, __FILE__, __LINE__,
                          "Failed to close() %s : %s",
                          afd_active_file, strerror(errno));
            }

            /*
             * Before starting to activate new process make sure there is
             * no old process still running.
             */
            if (*(pid_t *)(pid_list + ((DC_NO + 1) * sizeof(pid_t))) > 0)
            {
               if (kill(*(pid_t *)(pid_list + ((DC_NO + 1) * sizeof(pid_t))), SIGINT) == 0)
               {
                  system_log(DEBUG_SIGN, __FILE__, __LINE__,
                             "Had to kill() an old %s job!", DC_PROC_NAME);
               }
            }
         }
      }

      /*
       * Create and initialize AMG counter file. Do it here to
       * avoid having two dir_checks trying to do the same.
       */
      if ((stat(counter_file, &stat_buf) == -1) && (errno == ENOENT))
      {
         /*
          * Lets assume when there is no counter file that this is the
          * first time that AFD is started.
          */
         first_time = YES;
      }
      if ((fd = coe_open(counter_file, O_RDWR | O_CREAT,
#ifdef GROUP_CAN_WRITE
                         S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1)
#else
                         S_IRUSR | S_IWUSR)) == -1)
#endif
      {
         system_log(FATAL_SIGN, __FILE__, __LINE__,
                    "Could not open() %s : %s",
                    counter_file, strerror(errno));
         exit(INCORRECT);
      }

      /* Initialise counter file with zero */
      if (write(fd, &status, sizeof(int)) != sizeof(int))
      {
         system_log(FATAL_SIGN, __FILE__, __LINE__,
                    "Could not initialise %s : %s",
                    counter_file, strerror(errno));
         exit(INCORRECT);
      }
      if (close(fd) == -1)
      {
         system_log(DEBUG_SIGN, __FILE__, __LINE__,
                    "close() error : %s", strerror(errno));
      }

      /* If process AFD and AMG_DIALOG have not yet been created */
      /* we create the fifos needed to communicate with them.    */
      if ((stat(amg_cmd_fifo, &stat_buf) < 0) || (!S_ISFIFO(stat_buf.st_mode)))
      {
         if (make_fifo(amg_cmd_fifo) < 0)
         {
            system_log(FATAL_SIGN, __FILE__, __LINE__,
                       "Failed to create fifo %s.", amg_cmd_fifo);
            exit(INCORRECT);
         }
      }
      if ((stat(db_update_fifo, &stat_buf) < 0) || (!S_ISFIFO(stat_buf.st_mode)))
      {
         if (make_fifo(db_update_fifo) < 0)
         {
            system_log(FATAL_SIGN, __FILE__, __LINE__,
                       "Failed to create fifo %s.", db_update_fifo);
            exit(INCORRECT);
         }
      }

      /* Open fifo to AFD to receive commands. */
      if ((amg_cmd_fd = coe_open(amg_cmd_fifo, O_RDWR)) == -1)
      {
         system_log(FATAL_SIGN, __FILE__, __LINE__,
                    "Could not open fifo %s : %s",
                    amg_cmd_fifo, strerror(errno));
         exit(INCORRECT);
      }

      /* Open fifo for edit_hc and edit_dc so they can */
      /* inform the AMG about any changes.             */
      if ((db_update_fd = coe_open(db_update_fifo, O_RDWR)) == -1)
      {
         system_log(FATAL_SIGN, __FILE__, __LINE__,
                    "Could not open fifo %s : %s",
                    db_update_fifo, strerror(errno));
         exit(INCORRECT);
      }
      get_afd_config_value(&rescan_time, &max_no_proc, &max_process_per_dir,
                           &create_source_dir_mode, &max_copied_files,
                           &max_copied_file_size, &default_delete_files_flag,
                           &default_old_file_time, &remove_unused_hosts);

      /* Find largest file descriptor. */
      if (amg_cmd_fd > db_update_fd)
      {
         max_fd = amg_cmd_fd + 1;
      }
      else
      {
         max_fd = db_update_fd + 1;
      }

      /* Evaluate HOST_CONFIG file. */
      hl = NULL;
      if ((eval_host_config(&no_of_hosts, host_config_file,
                            &hl, first_time) == NO_ACCESS) &&
          (first_time == NO))
      {
         /*
          * Try get the host information from the current FSA.
          */
         if (fsa_attach() != INCORRECT)
         {
            size_t new_size = ((no_of_hosts / HOST_BUF_SIZE) + 1) *
                              HOST_BUF_SIZE * sizeof(struct host_list);

            if ((hl = realloc(hl, new_size)) == NULL)
            {
               system_log(FATAL_SIGN, __FILE__, __LINE__,
                          "Could not reallocate memory for host list : %s",
                          strerror(errno));
               exit(INCORRECT);
            }

            for (i = 0; i < no_of_hosts; i++)
            {
               (void)memcpy(hl[i].host_alias, fsa[i].host_alias, MAX_HOSTNAME_LENGTH + 1);
               (void)memcpy(hl[i].real_hostname[0], fsa[i].real_hostname[0], MAX_REAL_HOSTNAME_LENGTH);
               (void)memcpy(hl[i].real_hostname[1], fsa[i].real_hostname[1], MAX_REAL_HOSTNAME_LENGTH);
               (void)memcpy(hl[i].host_toggle_str, fsa[i].host_toggle_str, 5);
               (void)memcpy(hl[i].proxy_name, fsa[i].proxy_name, MAX_PROXY_NAME_LENGTH);
               (void)memset(hl[i].fullname, 0, MAX_FILENAME_LENGTH);
               hl[i].allowed_transfers   = fsa[i].allowed_transfers;
               hl[i].max_errors          = fsa[i].max_errors;
               hl[i].retry_interval      = fsa[i].retry_interval;
               hl[i].transfer_blksize    = fsa[i].block_size;
               hl[i].successful_retries  = fsa[i].max_successful_retries;
               hl[i].file_size_offset    = fsa[i].file_size_offset;
               hl[i].transfer_timeout    = fsa[i].transfer_timeout;
               hl[i].protocol            = fsa[i].protocol;
               hl[i].number_of_no_bursts = fsa[i].special_flag & NO_BURST_COUNT_MASK;
               hl[i].transfer_rate_limit = fsa[i].transfer_rate_limit;
               hl[i].protocol_options    = fsa[i].protocol_options;
               hl[i].host_status = 0;
               if (fsa[i].special_flag & HOST_DISABLED)
               {
                  hl[i].host_status |= HOST_CONFIG_HOST_DISABLED;
               }
               if ((fsa[i].special_flag & HOST_IN_DIR_CONFIG) == 0)
               {
                  hl[i].host_status |= HOST_NOT_IN_DIR_CONFIG;
               }
               if (fsa[i].host_status & STOP_TRANSFER_STAT)
               {
                  hl[i].host_status |= STOP_TRANSFER_STAT;
               }
               if (fsa[i].host_status & PAUSE_QUEUE_STAT)
               {
                  hl[i].host_status |= PAUSE_QUEUE_STAT;
               }
               if (fsa[i].host_toggle == HOST_TWO)
               {
                  hl[i].host_status |= HOST_TWO_FLAG;
               }
            }

            (void)fsa_detach(NO);
         }
      }

      db_size = 0;
      for (i = 0; i < no_of_dir_configs; i++)
      {
         /* Get the size of the database file */
         if (stat(dcl[i].dir_config_file, &stat_buf) == -1)
         {
            system_log(WARN_SIGN, __FILE__, __LINE__,
                       "Could not get size of database file `%s' : %s",
                       dcl[i].dir_config_file, strerror(errno));
            dcl[i].dc_old_time = 0L;
         }
         else
         {
            /* Since this is the first time round and this */
            /* is the time of the actual database, we      */
            /* store its value here in dcl[i].dc_old_time. */
            dcl[i].dc_old_time = stat_buf.st_mtime;
            db_size += stat_buf.st_size;
         }
      }
      if (db_size < 12)
      {
         system_log(FATAL_SIGN, __FILE__, __LINE__,
                    "There is no valid data in DIR_CONFIG %s.",
                    (no_of_dir_configs > 1) ? "files" : "file");
         exit(INCORRECT);
      }
      lookup_dc_id(&dcl, no_of_dir_configs);

      /*
       * If necessary inform FD that AMG is (possibly) about to change
       * the FSA. This is needed when we start/stop the AMG by hand.
       */
      if ((p_afd_status->amg_jobs & REREADING_DIR_CONFIG) == 0)
      {
         p_afd_status->amg_jobs ^= REREADING_DIR_CONFIG;
      }
      inform_fd_about_fsa_change();

      /* evaluate database */
      if (eval_dir_config(db_size) != SUCCESS)
      {
         system_log(FATAL_SIGN, __FILE__, __LINE__,
                    "Could not find any valid entries in database %s",
                    (no_of_dir_configs > 1) ? "files" : "file");
         exit(INCORRECT);
      }

      /*
       * Since there might have been an old FSA which has more information
       * then the HOST_CONFIG lets rewrite this file using the information
       * from both HOST_CONFIG and old FSA. That what is found in the
       * HOST_CONFIG will always have a higher priority.
       */
      hc_old_time = write_host_config(no_of_hosts, host_config_file, hl);
      system_log(INFO_SIGN, NULL, 0,
                 "Found %d hosts in HOST_CONFIG.", no_of_hosts);

      /*
       * Before we start any programs copy any files that are in the
       * pool directory back to their original directories (if they
       * still exist).
       */
#ifdef _DELETE_LOG
      delete_log_ptrs(&dl);
#endif
      clear_pool_dir();
#ifdef _DELETE_LOG
      if ((dl.fd != -1) && (dl.data != NULL))
      {
         free(dl.data);
      }
#endif

      /* Free dir name buffer which is no longer needed. */
      if (dnb != NULL)
      {
         unmap_data(dnb_fd, (void *)&dnb);
      }

      /* First create the fifo to communicate with other process */
      (void)unlink(dc_cmd_fifo);
      if (make_fifo(dc_cmd_fifo) < 0)
      {
         system_log(FATAL_SIGN, __FILE__, __LINE__,
                    "Could not create fifo %s.", dc_cmd_fifo);
         exit(INCORRECT);
      }
      (void)unlink(dc_resp_fifo);
      if (make_fifo(dc_resp_fifo) < 0)
      {
         system_log(FATAL_SIGN, __FILE__, __LINE__,
                    "Could not create fifo %s.", dc_resp_fifo);
         exit(INCORRECT);
      }
   }

   /* Start process dir_check if database has information. */
   if (data_length > 0)
   {
      dc_pid = make_process_amg(work_dir, DC_PROC_NAME, rescan_time,
                                max_no_proc);
      if (pid_list != NULL)
      {
         *(pid_t *)(pid_list + ((DC_NO + 1) * sizeof(pid_t))) = dc_pid;
      }
   }
   else
   {
      /* Process not started */
      dc_pid = NOT_RUNNING;
   }

   /* Note time when AMG is started */
   system_log(INFO_SIGN, NULL, 0, "Starting %s (%s)", AMG, PACKAGE_VERSION);
   system_log(DEBUG_SIGN, NULL, 0,
              "AMG Configuration: Directory rescan time     %d (sec)",
              rescan_time);
   system_log(DEBUG_SIGN, NULL, 0,
              "AMG Configuration: Max process               %d", max_no_proc);
   system_log(DEBUG_SIGN, NULL, 0,
              "AMG Configuration: Max process per directory %d",
              max_process_per_dir);
   if (default_delete_files_flag != 0)
   {
      char *ptr,
           tmp_str[22];

      ptr = tmp_str;
      if (default_delete_files_flag & UNKNOWN_FILES)
      {
         ptr += sprintf(ptr, "UNKNOWN ");
      }
      if (default_delete_files_flag & QUEUED_FILES)
      {
         ptr += sprintf(ptr, "QUEUED ");
      }
      if (default_delete_files_flag & OLD_LOCKED_FILES)
      {
         (void)sprintf(ptr, "LOCKED");
      }
      system_log(DEBUG_SIGN, NULL, 0,
                 "AMG Configuration: Default delete file flag  %s", tmp_str);
      if (default_old_file_time == -1)
      {
         system_log(DEBUG_SIGN, NULL, 0,
                    "AMG Configuration: Default old file time     %d",
                    DEFAULT_OLD_FILE_TIME);
      }
      else
      {
         system_log(DEBUG_SIGN, NULL, 0,
                    "AMG Configuration: Default old file time     %d",
                    default_old_file_time);
      }
   }
   system_log(DEBUG_SIGN, NULL, 0,
              "AMG Configuration: Default max copied files  %u",
              max_copied_files);
   system_log(DEBUG_SIGN, NULL, 0,
#if SIZEOF_OFF_T == 4
              "AMG Configuration: Def max copied file size  %ld (Bytes)",
#else
              "AMG Configuration: Def max copied file size  %lld (Bytes)",
#endif
              max_copied_file_size);
   system_log(DEBUG_SIGN, NULL, 0,
              "AMG Configuration: Remove unused hosts       %s",
              (remove_unused_hosts == NO) ? "No" : "Yes");

   /* Check if the database has been changed */
   FD_ZERO(&rset);
   for (;;)
   {
      /* Initialise descriptor set and timeout */
      FD_SET(amg_cmd_fd, &rset);
      FD_SET(db_update_fd, &rset);
      timeout.tv_usec = 0;
      timeout.tv_sec = rescan_time;

      /* Wait for message x seconds and then continue. */
      status = select(max_fd, &rset, NULL, NULL, &timeout);

      /* Did we get a message from the AFD control */
      /* fifo to shutdown the AMG?                 */
      if ((status > 0) && (FD_ISSET(amg_cmd_fd, &rset)))
      {
         if ((status = read(amg_cmd_fd, buffer, 10)) > 0)
         {
            /* Show user we got shutdown message */
            system_log(INFO_SIGN, NULL, 0, "%s shutting down ....", AMG);

            /* Do not forget to stop all running jobs */
            if (dc_pid > 0)
            {
               int j;

               if (com(STOP) == INCORRECT)
               {
                  system_log(INFO_SIGN, NULL, 0, "Giving it another try ...");
                  (void)com(STOP);
               }

               /* Wait for the child to terminate */
               for (j = 0; j < MAX_SHUTDOWN_TIME;  j++)
               {
                  if (waitpid(dc_pid, NULL, WNOHANG) == dc_pid)
                  {
                     dc_pid = NOT_RUNNING;
                     break;
                  }
                  else
                  {
                     (void)sleep(1);
                  }
               }
            }

            stop_flag = 1;

            break;
         }
         else if (status == -1)
              {
                 system_log(FATAL_SIGN, __FILE__, __LINE__,
                            "Failed to read() from %s : %s",
                            AMG_CMD_FIFO, strerror(errno));
                 exit(INCORRECT);
              }
              else /* == 0 */
              {
                 system_log(FATAL_SIGN, __FILE__, __LINE__,
                            "Hmm, reading zero from %s.", AMG_CMD_FIFO);
                 exit(INCORRECT);
              }
      }
           /* Did we receive a message from the edit_hc or */
           /* edit_dc dialog?                              */
      else if ((status > 0) && (FD_ISSET(db_update_fd, &rset)))
           {
              int n,
                  count = 0;

              if ((n = read(db_update_fd, buffer, 10)) > 0)
              {
#ifdef _FIFO_DEBUG
                 show_fifo_data('R', DB_UPDATE_FIFO, buffer, n, __FILE__, __LINE__);
#endif
                 while (count < n)
                 {
                    switch(buffer[count])
                    {
                       case HOST_CONFIG_UPDATE :
                          /* HOST_CONFIG updated by edit_hc */
                          if (fsa_attach() == INCORRECT)
                          {
                             system_log(FATAL_SIGN, __FILE__, __LINE__,
                                        "Could not attach to FSA!");
                             exit(INCORRECT);
                          }

                          for (i = 0; i < no_of_hosts; i++)
                          {
                             (void)memcpy(hl[i].host_alias, fsa[i].host_alias, MAX_HOSTNAME_LENGTH + 1);
                             (void)memcpy(hl[i].real_hostname[0], fsa[i].real_hostname[0], MAX_REAL_HOSTNAME_LENGTH);
                             (void)memcpy(hl[i].real_hostname[1], fsa[i].real_hostname[1], MAX_REAL_HOSTNAME_LENGTH);
                             (void)memcpy(hl[i].host_toggle_str, fsa[i].host_toggle_str, 5);
                             (void)memcpy(hl[i].proxy_name, fsa[i].proxy_name, MAX_PROXY_NAME_LENGTH);
                             (void)memset(hl[i].fullname, 0, MAX_FILENAME_LENGTH);
                             hl[i].allowed_transfers   = fsa[i].allowed_transfers;
                             hl[i].max_errors          = fsa[i].max_errors;
                             hl[i].retry_interval      = fsa[i].retry_interval;
                             hl[i].transfer_blksize    = fsa[i].block_size;
                             hl[i].successful_retries  = fsa[i].max_successful_retries;
                             hl[i].file_size_offset    = fsa[i].file_size_offset;
                             hl[i].transfer_timeout    = fsa[i].transfer_timeout;
                             hl[i].protocol            = fsa[i].protocol;
                             hl[i].number_of_no_bursts = fsa[i].special_flag & NO_BURST_COUNT_MASK;
                             hl[i].transfer_rate_limit = fsa[i].transfer_rate_limit;
                             hl[i].protocol_options    = fsa[i].protocol_options;
                             hl[i].host_status = 0;
                             if (fsa[i].special_flag & HOST_DISABLED)
                             {
                                hl[i].host_status |= HOST_CONFIG_HOST_DISABLED;
                             }
                             if ((fsa[i].special_flag & HOST_IN_DIR_CONFIG) == 0)
                             {
                                hl[i].host_status |= HOST_NOT_IN_DIR_CONFIG;
                             }
                             if (fsa[i].host_status & STOP_TRANSFER_STAT)
                             {
                                hl[i].host_status |= STOP_TRANSFER_STAT;
                             }
                             if (fsa[i].host_status & PAUSE_QUEUE_STAT)
                             {
                                hl[i].host_status |= PAUSE_QUEUE_STAT;
                             }
                             if (fsa[i].host_toggle == HOST_TWO)
                             {
                                hl[i].host_status |= HOST_TWO_FLAG;
                             }
                          }

                          /* Increase HOST_CONFIG counter so others */
                          /* can see there was a change.            */
                          (*(unsigned char *)((char *)fsa - AFD_WORD_OFFSET + SIZEOF_INT))++;
                          (void)fsa_detach(YES);

                          notify_dir_check();
                          hc_old_time = write_host_config(no_of_hosts, host_config_file, hl);
                          system_log(INFO_SIGN, __FILE__, __LINE__,
                                     "Updated HOST_CONFIG file.");
                          break;

                       case DIR_CONFIG_UPDATE :
                          /* DIR_CONFIG updated by edit_dc */
                          system_log(INFO_SIGN, __FILE__, __LINE__,
                                     "This function has not yet been implemented.");
                          break;

                       case REREAD_HOST_CONFIG :
                          reread_host_config(&hc_old_time, NULL, NULL,
                                             NULL, NULL, YES);

                          /*
                           * Do not forget to start dir_check if we have
                           * stopped it!
                           */
                          if (dc_pid == NOT_RUNNING)
                          {
                             dc_pid = make_process_amg(work_dir, DC_PROC_NAME,
                                                       rescan_time, max_no_proc);
                             if (pid_list != NULL)
                             {
                                *(pid_t *)(pid_list + ((DC_NO + 1) * sizeof(pid_t))) = dc_pid;
                             }
                             system_log(INFO_SIGN, __FILE__, __LINE__,
                                        "Restarted %s.", DC_PROC_NAME);
                          }
                          break;

                       case REREAD_DIR_CONFIG :
                          {
                             int         dc_changed = NO;
                             off_t       db_size = 0;
                             struct stat stat_buf;

                             for (i = 0; i < no_of_dir_configs; i++)
                             {
                                if (stat(dcl[i].dir_config_file, &stat_buf) < 0)
                                {
                                   system_log(WARN_SIGN, __FILE__, __LINE__,
                                              "Failed to stat() %s : %s",
                                              dcl[i].dir_config_file,
                                              strerror(errno));
                                }
                                else
                                {
                                   if (dcl[i].dc_old_time != stat_buf.st_mtime)
                                   {
                                      dcl[i].dc_old_time = stat_buf.st_mtime;
                                      dc_changed = YES;
                                   }
                                   db_size += stat_buf.st_size;
                                }
                             }
                             if (db_size > 0)
                             {
                                if (dc_changed == YES)
                                {
                                   int              old_no_of_hosts,
                                                    rewrite_host_config = NO;
                                   size_t           old_size = 0;
                                   struct host_list *old_hl = NULL;

                                   /* Set flag to indicate that we are */
                                   /* rereading the DIR_CONFIG.        */
                                   if ((p_afd_status->amg_jobs & REREADING_DIR_CONFIG) == 0)
                                   {
                                      p_afd_status->amg_jobs ^= REREADING_DIR_CONFIG;
                                   }
                                   inform_fd_about_fsa_change();

                                   /* Better check if there was a change in HOST_CONFIG */
                                   reread_host_config(&hc_old_time,
                                                      &old_no_of_hosts,
                                                      &rewrite_host_config,
                                                      &old_size, &old_hl, NO);

                                   reread_dir_config(dc_changed,
                                                     db_size,
                                                     &hc_old_time,
                                                     old_no_of_hosts,
                                                     rewrite_host_config,
                                                     old_size,
                                                     rescan_time,
                                                     max_no_proc,
                                                     old_hl);
                                }
                                else
                                {
                                   if (no_of_dir_configs > 1)
                                   {
                                      system_log(INFO_SIGN, NULL, 0,
                                                 "There is no change in all DIR_CONFIG's.");
                                   }
                                   else
                                   {
                                      system_log(INFO_SIGN, NULL, 0,
                                                 "There is no change in DIR_CONFIG.");
                                   }
                                }
                             }
                             else
                             {
                                if (no_of_dir_configs > 1)
                                {
                                   system_log(WARN_SIGN, NULL, 0,
                                              "All DIR_CONFIG files are empty.");
                                }
                                else
                                {
                                   system_log(WARN_SIGN, NULL, 0,
                                              "DIR_CONFIG file is empty.");
                                }
                             }
                          }
                          break;

                       default : 
                          /* Assume we are reading garbage */
                          system_log(INFO_SIGN, __FILE__, __LINE__,
                                     "Reading garbage (%d) on fifo %s",
                                     (int)buffer[count], DB_UPDATE_FIFO);
                          break;
                    }
                    count++;
                 }
              }
           }
           /* Did we get a timeout. */
      else if (status == 0)
           {
              /*
               * Check if the HOST_CONFIG file still exists. If not recreate
               * it from the internal current host_list structure.
               */
              if (stat(host_config_file, &stat_buf) < 0)
              {
                 if (errno == ENOENT)
                 {
                    system_log(INFO_SIGN, NULL, 0,
                               "Recreating HOST_CONFIG file with %d hosts.",
                               no_of_hosts);
                    hc_old_time = write_host_config(no_of_hosts,
                                                    host_config_file, hl);
                 }
                 else
                 {
                    system_log(FATAL_SIGN, __FILE__, __LINE__,
                               "Could not stat() HOST_CONFIG file %s : %s",
                               host_config_file, strerror(errno));
                    exit(INCORRECT);
                 }
              }
           }
           else 
           {
              system_log(FATAL_SIGN, __FILE__, __LINE__,
                         "select() error : %s", strerror(errno));
              exit(INCORRECT);
           }

      /* Check if any process died */
      if (dc_pid > 0) 
      {
         if ((amg_zombie_check(&dc_pid, WNOHANG) == YES) &&
             (data_length > 0))
         {
            /* So what do we do now? */
            /* For now lets only tell the user that the job died. */
            system_log(ERROR_SIGN, __FILE__, __LINE__,
                       "Job %s has died!", DC_PROC_NAME);

            dc_pid = make_process_amg(work_dir, DC_PROC_NAME,
                                      rescan_time, max_no_proc);
            if (pid_list != NULL)
            {
               *(pid_t *)(pid_list + ((DC_NO + 1) * sizeof(pid_t))) = dc_pid;
            }
            system_log(INFO_SIGN, __FILE__, __LINE__,
                       "Restarted %s.", DC_PROC_NAME);
         }
      } /* if (dc_pid > 0) */
   } /* for (;;) */

#ifdef _DEBUG
   /* Don't forget to close debug file */
   (void)fclose(p_debug_file);
#endif

   exit(SUCCESS);
}


/*+++++++++++++++++++++++++ get_afd_config_value() ++++++++++++++++++++++*/
static void
get_afd_config_value(int          *rescan_time,
                     int          *max_no_proc,
                     int          *max_process_per_dir,
                     mode_t       *create_source_dir_mode,
                     unsigned int *max_copied_files,
                     off_t        *max_copied_file_size,
                     int          *default_delete_files_flag,
                     int          *default_old_file_time,
                     int          *remove_unused_hosts)
{
   char *buffer,
        config_file[MAX_PATH_LENGTH];

   (void)sprintf(config_file, "%s%s%s",
                 p_work_dir, ETC_DIR, AFD_CONFIG_FILE);
   if ((eaccess(config_file, F_OK) == 0) &&
       (read_file(config_file, &buffer) != INCORRECT))
   {
      size_t length,
             max_length;
      char   *ptr,
             value[MAX_INT_LENGTH];

      if (get_definition(buffer, AMG_DIR_RESCAN_TIME_DEF,
                         value, MAX_INT_LENGTH) != NULL)
      {
         *rescan_time = atoi(value);
         if (*rescan_time < 1)
         {
            system_log(DEBUG_SIGN, __FILE__, __LINE__,
                       "Incorrect value (%d) set in AFD_CONFIG for %s. Setting to default %d.",
                       *rescan_time, AMG_DIR_RESCAN_TIME_DEF,
                       DEFAULT_RESCAN_TIME);
            *rescan_time = DEFAULT_RESCAN_TIME;
         }
      }
      if (get_definition(buffer, MAX_NO_OF_DIR_CHECKS_DEF,
                         value, MAX_INT_LENGTH) != NULL)
      {
         *max_no_proc = atoi(value);
         if ((*max_no_proc < 1) || (*max_no_proc > 10240))
         {
            system_log(DEBUG_SIGN, __FILE__, __LINE__,
                       "Incorrect value (%d) set in AFD_CONFIG for %s. Setting to default %d.",
                       *max_no_proc, MAX_NO_OF_DIR_CHECKS_DEF,
                       MAX_NO_OF_DIR_CHECKS);
            *max_no_proc = MAX_NO_OF_DIR_CHECKS;
         }
      }
      if (get_definition(buffer, MAX_PROCESS_PER_DIR_DEF,
                         value, MAX_INT_LENGTH) != NULL)
      {
         *max_process_per_dir = atoi(value);
         if ((*max_process_per_dir < 1) || (*max_process_per_dir > 10240))
         {
            system_log(DEBUG_SIGN, __FILE__, __LINE__,
                       "Incorrect value (%d) set in AFD_CONFIG for %s. Setting to default %d.",
                       *max_process_per_dir, MAX_PROCESS_PER_DIR_DEF,
                       MAX_PROCESS_PER_DIR);
            *max_process_per_dir = MAX_PROCESS_PER_DIR;
         }
         if (*max_process_per_dir > *max_no_proc)
         {
            system_log(DEBUG_SIGN, __FILE__, __LINE__,
                       "%s (%d) may not be larger than %s (%d) in AFD_CONFIG. Setting to %d.",
                       MAX_PROCESS_PER_DIR_DEF, *max_process_per_dir, 
                       MAX_NO_OF_DIR_CHECKS_DEF, *max_no_proc, *max_no_proc);
            *max_process_per_dir = MAX_PROCESS_PER_DIR;
         }
      }
      if (get_definition(buffer, CREATE_SOURCE_DIR_MODE_DEF,
                         value, MAX_INT_LENGTH) != NULL)
      {
         *create_source_dir_mode = (unsigned int)atoi(value);
         if ((*create_source_dir_mode <= 700) ||
             (*create_source_dir_mode > 7777))
         {
            system_log(DEBUG_SIGN, __FILE__, __LINE__,
                       "Invalid mode %u set in AFD_CONFIG for %s. Setting to default %d.",
                       *create_source_dir_mode, CREATE_SOURCE_DIR_MODE_DEF,
                       DIR_MODE);
            *create_source_dir_mode = DIR_MODE;
         }
      }
      if (get_definition(buffer, REMOVE_UNUSED_HOSTS_DEF, NULL, 0) != NULL)
      {
         *remove_unused_hosts = YES;
      }
      if (get_definition(buffer, MAX_COPIED_FILE_SIZE_DEF,
                         value, MAX_INT_LENGTH) != NULL)  
      {
         /* The value is given in megabytes, so convert to bytes. */
         *max_copied_file_size = atoi(value) * 1048576;
         if ((*max_copied_file_size < 1) || (*max_copied_file_size > 1048576000))
         {
            *max_copied_file_size = MAX_COPIED_FILE_SIZE * MAX_COPIED_FILE_SIZE_UNIT;
         }
      }
      else
      {
         *max_copied_file_size = MAX_COPIED_FILE_SIZE * MAX_COPIED_FILE_SIZE_UNIT;
      }
      if (get_definition(buffer, MAX_COPIED_FILES_DEF,
                         value, MAX_INT_LENGTH) != NULL)
      {
         *max_copied_files = atoi(value);
         if ((*max_copied_files < 1) || (*max_copied_files > 10240))
         {
            *max_copied_files = MAX_COPIED_FILES;
         }
      }
      else
      {
         *max_copied_files = MAX_COPIED_FILES;
      }
      if (get_definition(buffer, DEFAULT_OLD_FILE_TIME_DEF,
                         value, MAX_INT_LENGTH) != NULL)
      {
         *default_old_file_time = atoi(value);
         if ((*default_old_file_time < 1) || (*default_old_file_time > 596523))
         {
            system_log(DEBUG_SIGN, __FILE__, __LINE__,
                       "Incorrect value (%d) set in AFD_CONFIG for %s. Setting to default %d.",
                       *default_old_file_time, DEFAULT_OLD_FILE_TIME_DEF,
                       DEFAULT_OLD_FILE_TIME);
            *default_old_file_time = DEFAULT_OLD_FILE_TIME;
         }
      }
      if (get_definition(buffer, DEFAULT_DELETE_FILES_FLAG_DEF,
                         config_file, MAX_PATH_LENGTH) != NULL)
      {
         ptr = config_file;
         do
         {
            while (((*ptr == ' ') || (*ptr == '\t') || (*ptr == ',')) &&
                   ((ptr + 1) < (config_file + MAX_PATH_LENGTH - 1)))
            {
               ptr++;
            }
            if (((ptr + 7) < (config_file + MAX_PATH_LENGTH - 1)) &&
                ((*ptr == 'U') || (*ptr == 'u')) &&
                ((*(ptr + 1) == 'N') || (*(ptr + 1) == 'n')) &&
                ((*(ptr + 2) == 'K') || (*(ptr + 2) == 'k')) &&
                ((*(ptr + 3) == 'N') || (*(ptr + 3) == 'n')) &&
                ((*(ptr + 4) == 'O') || (*(ptr + 4) == 'o')) &&
                ((*(ptr + 5) == 'W') || (*(ptr + 5) == 'w')) &&
                ((*(ptr + 6) == 'N') || (*(ptr + 6) == 'n')) &&
                ((*(ptr + 7) == ' ') || (*(ptr + 7) == '\t') ||
                 (*(ptr + 7) == ',') || (*(ptr + 7) == '\0')))
            {
               ptr += 7;
               if ((*default_delete_files_flag & UNKNOWN_FILES) == 0)
               {
                  *default_delete_files_flag |= UNKNOWN_FILES;
               }
            }
            else if (((ptr + 6) < (config_file + MAX_PATH_LENGTH - 1)) &&
                     ((*ptr == 'Q') || (*ptr == 'q')) &&
                     ((*(ptr + 1) == 'U') || (*(ptr + 1) == 'u')) &&
                     ((*(ptr + 2) == 'E') || (*(ptr + 2) == 'e')) &&
                     ((*(ptr + 3) == 'U') || (*(ptr + 3) == 'u')) &&
                     ((*(ptr + 4) == 'E') || (*(ptr + 4) == 'e')) &&
                     ((*(ptr + 5) == 'D') || (*(ptr + 5) == 'd')) &&
                     ((*(ptr + 6) == ' ') || (*(ptr + 6) == '\t') ||
                      (*(ptr + 6) == ',') || (*(ptr + 6) == '\0')))
                 {
                    ptr += 6;
                    if ((*default_delete_files_flag & QUEUED_FILES) == 0)
                    {
                       *default_delete_files_flag |= QUEUED_FILES;
                    }
                 }
            else if (((ptr + 6) < (config_file + MAX_PATH_LENGTH - 1)) &&
                     ((*ptr == 'L') || (*ptr == 'l')) &&
                     ((*(ptr + 1) == 'O') || (*(ptr + 1) == 'o')) &&
                     ((*(ptr + 2) == 'C') || (*(ptr + 2) == 'c')) &&
                     ((*(ptr + 3) == 'K') || (*(ptr + 3) == 'k')) &&
                     ((*(ptr + 4) == 'E') || (*(ptr + 4) == 'e')) &&
                     ((*(ptr + 5) == 'D') || (*(ptr + 5) == 'd')) &&
                     ((*(ptr + 6) == ' ') || (*(ptr + 6) == '\t') ||
                      (*(ptr + 6) == ',') || (*(ptr + 6) == '\0')))
                 {
                    ptr += 6;
                    if ((*default_delete_files_flag & OLD_LOCKED_FILES) == 0)
                    {
                       *default_delete_files_flag |= OLD_LOCKED_FILES;
                    }
                 }
                 else
                 {
                    while ((*ptr != ' ') && (*ptr != '\t') &&
                           (*ptr != ',') && (*ptr != '\0') &&
                           ((ptr + 1) < (config_file + MAX_PATH_LENGTH - 1)))
                    {
                       ptr++;
                    }
                 }
         } while (*ptr != '\0');
      }
      ptr = buffer;
      no_of_dir_configs = 0;
      max_length = 0;
      while ((ptr = get_definition(ptr, DIR_CONFIG_NAME_DEF,
                                   config_file, MAX_PATH_LENGTH)) != NULL)
      {
         length = strlen(config_file) + 1;
         if (length > max_length)
         {
            max_length = length;
         }
         no_of_dir_configs++;
      }
      if ((no_of_dir_configs > 0) && (max_length > 0))
      {
         int i;

         if ((dcl = malloc(no_of_dir_configs * sizeof(struct dir_config_buf))) == NULL)
         {
            system_log(FATAL_SIGN, __FILE__, __LINE__,
                       "Failed to malloc() %d bytes : %s",
                       no_of_dir_configs * sizeof(struct dir_config_buf),
                       strerror(errno));
            exit(INCORRECT);
         }
         ptr = buffer;
         for (i = 0; i < no_of_dir_configs; i++)
         {
            if ((dcl[i].dir_config_file = malloc(max_length)) == NULL)
            {
               system_log(FATAL_SIGN, __FILE__, __LINE__,
                          "Failed to malloc() %d bytes : %s",
                          max_length, strerror(errno));
               exit(INCORRECT);
            }
            ptr = get_definition(ptr, DIR_CONFIG_NAME_DEF,
                                 dcl[i].dir_config_file, max_length);
         }
      }
      else
      {
         length = sprintf(config_file, "%s%s%s",
                          p_work_dir, ETC_DIR, DEFAULT_DIR_CONFIG_FILE) + 1;
         no_of_dir_configs = 1;
         if ((dcl = malloc(sizeof(struct dir_config_buf))) == NULL)
         {
            system_log(FATAL_SIGN, __FILE__, __LINE__,
                       "Failed to malloc() %d bytes : %s",
                       sizeof(struct dir_config_buf), strerror(errno));
            exit(INCORRECT);
         }
         if ((dcl[0].dir_config_file = malloc(length)) == NULL)
         {
            system_log(FATAL_SIGN, __FILE__, __LINE__,
                       "Failed to malloc() %d bytes : %s",
                       length, strerror(errno));
            exit(INCORRECT);
         }
         (void)strcpy(dcl[0].dir_config_file, config_file);
      }
      free(buffer);
   }
   else
   {
      size_t length;

      length = sprintf(config_file, "%s%s%s",
                       p_work_dir, ETC_DIR, DEFAULT_DIR_CONFIG_FILE) + 1;
      no_of_dir_configs = 1;
      if ((dcl = malloc(sizeof(struct dir_config_buf))) == NULL)
      {
         system_log(FATAL_SIGN, __FILE__, __LINE__,
                    "Failed to malloc() %d bytes : %s",
                    sizeof(struct dir_config_buf), strerror(errno));
         exit(INCORRECT);
      }
      if ((dcl[0].dir_config_file = malloc(length)) == NULL)
      {
         system_log(FATAL_SIGN, __FILE__, __LINE__,
                    "Failed to malloc() %d bytes : %s",
                    length, strerror(errno));
         exit(INCORRECT);
      }
      (void)strcpy(dcl[0].dir_config_file, config_file);
   }

   return;
}


/*+++++++++++++++++++++++++++ notify_dir_check() ++++++++++++++++++++++++*/
static void
notify_dir_check(void)
{
   int  fd;
   char fifo_name[MAX_PATH_LENGTH];

   (void)sprintf(fifo_name, "%s%s%s", p_work_dir, FIFO_DIR, IP_FIN_FIFO);
   if ((fd = open(fifo_name, O_RDWR)) == -1)
   {
      system_log(WARN_SIGN, __FILE__, __LINE__,
                 "Could not open() fifo %s : %s", fifo_name, strerror(errno));
   }
   else
   {
      pid_t pid = -1;

      if (write(fd, &pid, sizeof(pid_t)) != sizeof(pid_t))
      {
         system_log(WARN_SIGN, __FILE__, __LINE__,
                    "Could not write() to fifo %s : %s",
                    fifo_name, strerror(errno));
      }
      if (close(fd) == -1)
      {
         system_log(DEBUG_SIGN, __FILE__, __LINE__,
                    "close() error : %s", strerror(errno));
      }
   }

   return;
}


/*++++++++++++++++++++++++++++++ amg_exit() +++++++++++++++++++++++++++++*/
static void
amg_exit(void)
{
   system_log(INFO_SIGN, NULL, 0, "Stopped %s.", AMG);

   /* Kill all jobs that where started */
   if (dc_pid > 0)
   {
      if (kill(dc_pid, SIGINT) < 0)
      {
         if (errno != ESRCH)
         {
            system_log(ERROR_SIGN, __FILE__, __LINE__,
                       "Failed to kill process %s with pid %d : %s",
                       DC_PROC_NAME, dc_pid, strerror(errno));
         }
      }
   }

   if (pid_list != NULL)
   {
#ifdef HAVE_MMAP
      (void)munmap((void *)pid_list, afd_active_size);
#else
      (void)munmap_emu((void *)pid_list);
#endif
   }

   if (stop_flag == 0)
   {
      char counter_file[MAX_PATH_LENGTH];

      (void)sprintf(counter_file, "%s%s%s", p_work_dir, FIFO_DIR, COUNTER_FILE);
      (void)unlink(counter_file);
   }

   return;
}


/*++++++++++++++++++++++++++++++ sig_segv() +++++++++++++++++++++++++++++*/
static void
sig_segv(int signo)
{
   system_log(FATAL_SIGN, __FILE__, __LINE__, "Aaarrrggh! Received SIGSEGV.");
   amg_exit();
   abort();
}


/*++++++++++++++++++++++++++++++ sig_bus() ++++++++++++++++++++++++++++++*/
static void
sig_bus(int signo)
{
   system_log(FATAL_SIGN, __FILE__, __LINE__, "Uuurrrggh! Received SIGBUS.");
   amg_exit();
   abort();
}


/*++++++++++++++++++++++++++++++ sig_exit() +++++++++++++++++++++++++++++*/
static void
sig_exit(int signo)
{
   exit(INCORRECT);
}