/*
 *  asmtpdefs.h - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1997 - 2000 Holger Kiehl <Holger.Kiehl@dwd.de>
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

#ifndef __asmtpdefs_h
#define __asmtpdefs_h

#ifndef _STANDALONE_
#include "afddefs.h"
#include "fddefs.h"
#endif

#ifdef _STANDALONE_
/* indicators for start and end of module description for man pages */
#define DESCR__S_M1             /* Start for User Command Man Page. */
#define DESCR__E_M1             /* End for User Command Man Page.   */
#define DESCR__S_M3             /* Start for Subroutines Man Page.  */
#define DESCR__E_M3             /* End for Subroutines Man Page.    */

#define NO                         0
#define YES                        1
#define ON                         1
#define OFF                        0
#define INCORRECT                  -1
#define SUCCESS                    0

#define INFO_SIGN                  "<I>"
#define WARN_SIGN                  "<W>"
#define ERROR_SIGN                 "<E>"
#define FATAL_SIGN                 "<F>"           /* donated by Paul M. */
#define DEBUG_SIGN                 "<D>"

/* Some default definitions */
#define DEFAULT_TRANSFER_TIMEOUT   120L
#define DEFAULT_TRANSFER_BLOCKSIZE 1024

/* Definitions for maximum values. */
#define MAX_HOSTNAME_LENGTH        8
#define MAX_FILENAME_LENGTH        256
#define MAX_PATH_LENGTH            1024
#define MAX_LINE_LENGTH            2048

/* Definitions for different exit status for asmtp */
#define TRANSFER_SUCCESS           0
#define CONNECT_ERROR              1
#define USER_ERROR                 2
#define TYPE_ERROR                 4
#define LIST_ERROR                 5
#define TRANSFER_SUCCESS           6
#define OPEN_REMOTE_ERROR          10
#define WRITE_REMOTE_ERROR         11
#define CLOSE_REMOTE_ERROR         12
#define MOVE_REMOTE_ERROR          13
#define CHDIR_ERROR                14
#define TIMEOUT_ERROR              20
#define READ_REMOTE_ERROR          22
#define SIZE_ERROR                 23
#define OPEN_LOCAL_ERROR           30
#define READ_LOCAL_ERROR           31
#define STAT_LOCAL_ERROR           32
#define ALLOC_ERROR                35
#define SYNTAX_ERROR               60

/* Runtime array */
#define RT_ARRAY(name, rows, columns, type)                                 \
        {                                                                   \
           int   row_counter;                                               \
                                                                            \
           if ((name = (type **)calloc((rows), sizeof(type *))) == NULL)    \
           {                                                                \
              (void)rec(sys_log_fd, FATAL_SIGN, "calloc() error : %s (%s %d)\n", \
                        strerror(errno), __FILE__, __LINE__);               \
              exit(INCORRECT);                                              \
           }                                                                \
                                                                            \
           if ((name[0] = (type *)calloc(((rows) * (columns)), sizeof(type))) == NULL)   \
           {                                                                \
              (void)rec(sys_log_fd, FATAL_SIGN, "calloc() error : %s (%s %d)\n", \
                        strerror(errno), __FILE__, __LINE__);               \
              exit(INCORRECT);                                              \
           }                                                                \
                                                                            \
           for (row_counter = 1; row_counter < (rows); row_counter++)       \
              name[row_counter] = (name[0] + (row_counter * (columns)));    \
        }
#define FREE_RT_ARRAY(name)         \
        {                           \
           free(name[0]);           \
           free(name);              \
        }
#define REALLOC_RT_ARRAY(name, rows, columns, type)                         \
        {                                                                   \
           int  row_counter;                                                \
           char *ptr = name[0];                                             \
                                                                            \
           if ((name = (type **)realloc((name), (rows) * sizeof(type *))) == NULL) \
           {                                                                \
              (void)rec(sys_log_fd, FATAL_SIGN,                             \
                        "realloc() error : %s (%s %d)\n",                   \
                        strerror(errno), __FILE__, __LINE__);               \
              exit(INCORRECT);                                              \
           }                                                                \
                                                                            \
           if ((name[0] = (type *)realloc(ptr,                              \
                           (((rows) * (columns)) * sizeof(type)))) == NULL) \
           {                                                                \
              (void)rec(sys_log_fd, FATAL_SIGN,                             \
                        "realloc() error : %s (%s %d)\n",                   \
                        strerror(errno), __FILE__, __LINE__);               \
              exit(INCORRECT);                                              \
           }                                                                \
                                                                            \
           for (row_counter = 1; row_counter < (rows); row_counter++)       \
              name[row_counter] = (name[0] + (row_counter * (columns)));    \
        }

/* Function prototypes */
extern void my_usleep(unsigned long),
            t_hostname(char *, char *),
            trans_log(char *, char *, int, char *, ...);
extern int  rec(int, char *, char *, ...);
#endif /* _STANDALONE_ */

/* Error output in german */
/* #define _GERMAN */

#define DEFAULT_AFD_USER     "anonymous"
#define DEFAULT_AFD_PASSWORD "afd@someplace"

/* Structure holding all filenames that are to be retrieved. */
struct filename_list
       {            
          char  file_name[MAX_FILENAME_LENGTH];
          off_t size;
       };

/* Structure that holds all data for one ftp job */
struct data
       {
          int          port;             /* TCP port.                      */
          int          blocksize;        /* Transfer block size.           */
          int          no_of_files;      /* The number of files to be send.*/
          long         transfer_timeout; /* When to timeout the            */
                                         /* transmitting job.              */
          char         *subject;         /* Subject of the mail.           */
          char         **filename;       /* Pointer to array that holds    */
                                         /* all file names.                */
          char         hostname[MAX_FILENAME_LENGTH];
          char         user[MAX_FILENAME_LENGTH];
          char         smtp_server[MAX_USER_NAME_LENGTH];
                                         /* Target directory on the remote */
                                         /* side.                          */
          char         verbose;          /* Flag to set verbose option.    */
          char         remove;           /* Remove file flag.              */
          char         flag;             /* Special flag to indicate the   */
                                         /* following: ATTACH_FILE         */
                                         /*            FILE_NAME_IS_SUBJECT*/
                                         /*            FILE_NAME_IS_USER   */
       };

extern int  init_asmtp(int, char **, struct data *);
extern void eval_filename_file(char *, struct data *);

#endif /* __asmtpdefs_h */
