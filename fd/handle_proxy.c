/*
 *  handle_proxy.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 2000 Holger Kiehl <Holger.Kiehl@dwd.de>
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
 **   handle_proxy - handles the ftp proxy procedure
 **
 ** SYNOPSIS
 **   void handle_proxy(void)
 **
 ** DESCRIPTION
 **   This function handles the FTP login procedure via a proxy.
 **   The procedure itself is specified in the HOST_CONFIG file
 **   and has the following format:
 **   $U<login-name1>;[$P<password1>;]...[$U<login-nameN>;[$P<passwordN>;]]
 **   handle_proxy() sends to the remote FTP-server the login
 **   and password in the same order as specified in the HOST_CONFIG
 **   file.
 **
 ** RETURN VALUES
 **   None.
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   15.07.2000 H.Kiehl Created
 **
 */
DESCR__E_M3

#include <stdio.h>
#include <string.h>
#include "fddefs.h"
#include "ftpdefs.h"

/* External global variables */
extern int                        trans_db_log_fd;
extern char                       msg_str[];
extern struct filetransfer_status *fsa;
extern struct job                 db;


/*########################### handle_proxy() ############################*/
void
handle_proxy(void)
{
   int  i,
        status = 0;
   char buffer[MAX_USER_NAME_LENGTH],
        *proxy_ptr = fsa[db.fsa_pos].proxy_name,
        *ptr;

   do
   {
      if (*proxy_ptr == '$')
      {
         ptr = proxy_ptr + 2;
         switch (*(proxy_ptr + 1))
         {
            case 'u' :
            case 'U' : /* Enter user name. */
               i = 0;
               while ((*ptr != ';') && (*ptr != '$') && (*ptr != '\0') &&
                      (i < MAX_USER_NAME_LENGTH))
               {
                  if (*ptr == '\\')
                  {
                     ptr++;
                  }
                  buffer[i] = *ptr;
                  ptr++; i++;
               }
               if (i == MAX_USER_NAME_LENGTH)
               {
                  msg_str[0] = '\0';
                  trans_log(ERROR_SIGN, __FILE__, __LINE__,
                            "User name in proxy definition is to long (> %d).",
                            MAX_USER_NAME_LENGTH - 1);
                  (void)ftp_quit();
                  exit(USER_ERROR);
               }
               buffer[i] = '\0';
               if (buffer[0] == '\0')
               {
                  (void)strcpy(buffer, db.user);
               }

               /* Send user name */
               if (((status = ftp_user(buffer)) != SUCCESS) && (status != 230))
               {
                  trans_log(ERROR_SIGN, __FILE__, __LINE__,
                            "Failed to send user <%s> (%d) [Proxy].",
                            buffer, status);
                  (void)ftp_quit();
                  exit(USER_ERROR);
               }
               else
               {
                  if ((fsa[db.fsa_pos].debug == YES) &&
                      (trans_db_log_fd != -1))
                  {
                     if (status != 230)
                     {
                        trans_db_log(INFO_SIGN, __FILE__, __LINE__,
                                     "Entered user name <%s> [Proxy].",
                                     buffer);
                     }
                     else
                     {
                        trans_db_log(INFO_SIGN, __FILE__, __LINE__,
                                     "Entered user name <%s> [Proxy]. No password required, logged in.",
                                     buffer);
                     }
                  }
               }

               /* Don't forget to position the proxy pointer. */
               if (*ptr == ';')
               {
                  proxy_ptr = ptr + 1;
               }
               else
               {
                  proxy_ptr = ptr;
               }
               break;

            case 'p' :
            case 'P' : /* Enter passwd. */
               i = 0;
               while ((*ptr != ';') && (*ptr != '$') && (*ptr != '\0') &&
                      (i < MAX_USER_NAME_LENGTH))
               {
                  if (*ptr == '\\')
                  {
                     ptr++;
                  }
                  buffer[i] = *ptr;
                  ptr++; i++;
               }
               if (i == MAX_USER_NAME_LENGTH)
               {
                  msg_str[0] = '\0';
                  trans_log(ERROR_SIGN, __FILE__, __LINE__,
                            "Password in proxy definition is to long (> %d).",
                            MAX_USER_NAME_LENGTH - 1);
                  (void)ftp_quit();
                  exit(USER_ERROR);
               }
               buffer[i] = '\0';
               if (buffer[0] == '\0')
               {
                  (void)strcpy(buffer, db.password);
               }

               /* Maybe the passwd is not required, so make sure! */
               if (status != 230)
               {
                  if ((status = ftp_pass(buffer)) != SUCCESS)
                  {
                     trans_log(ERROR_SIGN, __FILE__, __LINE__,
                               "Failed to send password (%d).",
                               status);
                     (void)ftp_quit();
                     exit(PASSWORD_ERROR);
                  }
                  else
                  {
                     if ((fsa[db.fsa_pos].debug == YES) &&
                         (trans_db_log_fd != -1))
                     {
                        trans_db_log(INFO_SIGN, __FILE__, __LINE__,
                                     "Entered password.");
                     }
                  }
               }

               /* Don't forget to position the proxy pointer. */
               if (*ptr == ';')
               {
                  proxy_ptr = ptr + 1;
               }
               else
               {
                  proxy_ptr = ptr;
               }
               break;

            default : /* Syntax error in proxy format. */
               msg_str[0] = '\0';
               trans_log(ERROR_SIGN, __FILE__, __LINE__,
                         "Syntax error in proxy string <%s>.",
                         fsa[db.fsa_pos].proxy_name);
               (void)ftp_quit();
               exit(USER_ERROR);
         }
      }
      else
      {
         msg_str[0] = '\0';
         trans_log(ERROR_SIGN, __FILE__, __LINE__,
                   "Syntax error in proxy string <%s>.",
                   fsa[db.fsa_pos].proxy_name);
         (void)ftp_quit();
         exit(USER_ERROR);
      }
   } while (*proxy_ptr != '\0');
   return;
}
