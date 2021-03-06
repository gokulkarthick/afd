/*
 *  common.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 2004 - 2013 Holger Kiehl <Holger.Kiehl@dwd.de>
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
 **   common - functions that can be used for several protocols
 **
 ** SYNOPSIS
 **   int     command(int fd, char *fmt, ...)
 **   ssize_t ssl_write(SSL *ssl, const char *buf, size_t count)
 **   char    *ssl_error_msg(char *function, SSL *ssl, int *ssl_ret, int reply, char *msg_str)
 **   int     connect_with_timeout(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
 **
 ** DESCRIPTION
 **
 ** RETURN VALUES
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   10.03.2004 H.Kiehl Created
 **   26.06.2005 H.Kiehl Don't show password during a trace.
 **   27.02.2013 H.Kiehl Added function connect_with_timeout().
 */
DESCR__E_M3


#include <stdio.h>
#include <stdarg.h>       /* va_start(), va_end()                        */
#include <string.h>       /* memcpy(), strerror()                        */
#include <sys/types.h>    /* fd_set                                      */
#include <sys/time.h>     /* struct timeval                              */
#include <sys/stat.h>     /* S_ISUID, S_ISGID, etc                       */
#ifdef WITH_SSL
# include <openssl/ssl.h>
# include <openssl/err.h>
#endif
#include <unistd.h>       /* write()                                     */
#include <errno.h>
#include "fddefs.h"
#include "commondefs.h"


/* External global variables. */
extern int  timeout_flag;
extern long transfer_timeout;
#ifdef WITH_SSL
extern SSL  *ssl_con;
#endif


/*############################## command() ##############################*/
int
command(int fd, char *fmt, ...)
{
   int     length;
   char    buf[MAX_LINE_LENGTH + 1];
   va_list ap;
   char    *ptr,
           *ptr_start;

   va_start(ap, fmt);
#ifdef HAVE_VSNPRINTF
   length = vsnprintf(buf, MAX_LINE_LENGTH, fmt, ap);
#else
   length = vsprintf(buf, fmt, ap);
#endif
   va_end(ap);
   buf[length] = '\r';
   buf[length + 1] = '\n';
   length += 2;
#ifdef WITH_SSL
   if (ssl_con == NULL)
   {
#endif
      if (write(fd, buf, length) != length)
      {
         if ((errno == ECONNRESET) || (errno == EBADF))
         {
            timeout_flag = CON_RESET;
         }
         trans_log(ERROR_SIGN, __FILE__, __LINE__, "command", NULL,
                   _("write() error : %s"), strerror(errno));
         ptr = buf;
         do
         {
            ptr_start = ptr;
            while ((*ptr != '\r') && (*ptr != '\n') && (ptr < &buf[length - 1]))
            {
               ptr++;
            }
            if ((*ptr == '\r') || (*ptr == '\n'))
            {
               *ptr = '\0';
               ptr++;
               while (((*ptr == '\n') || (*ptr == '\r')) && (ptr < &buf[length - 1]))
               {
                  ptr++;
               }
            }
            trans_log(DEBUG_SIGN, NULL, 0, "command", NULL, "%s", ptr_start);
         } while (ptr < &buf[length - 1]);

         return(INCORRECT);
      }
#ifdef WITH_SSL
   }
   else
   {
      if (ssl_write(ssl_con, buf, length) != length)
      {
         return(INCORRECT);
      }
   }
#endif
#ifdef WITH_TRACE
   ptr = buf;
   do
   {
      ptr_start = ptr;
      while ((*ptr != '\r') && (*(ptr + 1) != '\n') && (ptr < &buf[length - 1]))
      {
         ptr++;
      }
      if (*ptr == '\r')
      {
         *ptr = '\0';
      }
      if ((*(ptr + 2) == '\r') && (*(ptr + 3) == '\n'))
      {
         /* This is required by HTTP, meaning end of command. */
         if (((ptr_start + 5) < (buf + length)) &&
             (*ptr_start == 'P') && (*(ptr_start + 1) == 'A') &&
             (*(ptr_start + 2) == 'S') && (*(ptr_start + 3) == 'S') &&
             (*(ptr_start + 4) == ' '))
         {
            trace_log(NULL, 0, W_TRACE, NULL, 0, "PASS xxx<0D><0A><0D><0A>");
         }
         else
         {
            trace_log(NULL, 0, W_TRACE, NULL, 0, "%s<0D><0A><0D><0A>",
                      ptr_start);
         }
         ptr += 4;
      }
      else
      {
         if (((ptr_start + 5) < (buf + length)) &&
             (*ptr_start == 'P') && (*(ptr_start + 1) == 'A') &&
             (*(ptr_start + 2) == 'S') && (*(ptr_start + 3) == 'S') &&
             (*(ptr_start + 4) == ' '))
         {
            trace_log(NULL, 0, W_TRACE, NULL, 0, "PASS xxx<0D><0A>");
         }
         else
         {
            trace_log(NULL, 0, W_TRACE, NULL, 0, "%s<0D><0A>", ptr_start);
         }
         ptr += 2;
      }
   } while (ptr < &buf[length - 1]);
#endif
   return(SUCCESS);
}


#ifdef WITH_SSL
/*############################# ssl_write() #############################*/
ssize_t
ssl_write(SSL *ssl, const char *buf, size_t count)
{
   int     bytes_done;
   ssize_t bytes_total = 0;

   do
   {
      if ((bytes_done = SSL_write(ssl, buf + bytes_total, count)) <= 0)
      {
         int ret;

         ret = SSL_get_error(ssl, bytes_done);
         switch (ret)
         {
            case SSL_ERROR_WANT_READ : /* Renegotiation takes place. */
               my_usleep(50000L);
               break;

            case SSL_ERROR_SYSCALL :
               if ((errno == ECONNRESET) || (errno == EBADF))
               {
                  timeout_flag = CON_RESET;
               }
               trans_log(ERROR_SIGN, __FILE__, __LINE__, "ssl_write", NULL,
                         _("SSL_write() error (%d) : %s"),
                         ret, strerror(errno));
               return(INCORRECT);

            default : /* Error */
               trans_log(ERROR_SIGN, __FILE__, __LINE__, "ssl_write", NULL,
                         _("SSL_write() error (%d)"), ret);
               return(INCORRECT);
         }
      }
      else
      {
         count -= bytes_done;
         bytes_total += bytes_done;
      }
   } while (count > 0);

   return(bytes_total);
}


/*########################### ssl_error_msg() ###########################*/
char *
ssl_error_msg(char *function, SSL *ssl, int *ssl_ret, int reply, char *msg_str)
{
   int len,
       *p_ret,
       ret;

   if (ssl_ret == NULL)
   {
      p_ret = &ret;
   }
   else
   {
      p_ret = ssl_ret;
   }
   *p_ret = SSL_get_error(ssl, reply);
   switch (*p_ret)
   {
      case SSL_ERROR_NONE :
#ifdef HAVE_SNPRINTF
         len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
         len = sprintf(msg_str,
#endif
                       _("%s error SSL_ERROR_NONE : The TLS/SSL I/O operation completed."),
                       function);
         break;

      case SSL_ERROR_ZERO_RETURN :
#ifdef HAVE_SNPRINTF
         len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
         len = sprintf(msg_str,
#endif
                       _("%s error SSL_ERROR_ZERO_RETURN : The TLS/SSL connection has been closed."),
                       function);
         break;

      case SSL_ERROR_WANT_WRITE :
#ifdef HAVE_SNPRINTF
         len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
         len = sprintf(msg_str,
#endif
                       _("%s error SSL_ERROR_WANT_WRITE : Operation not complete, try again later."),
                       function);
         break;

      case SSL_ERROR_WANT_READ :
#ifdef HAVE_SNPRINTF
         len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
         len = sprintf(msg_str,
#endif
                       _("%s error SSL_ERROR_WANT_READ : Operation not complete, try again later."),
                       function);
         break;

#ifdef SSL_ERROR_WANT_ACCEPT
      case SSL_ERROR_WANT_ACCEPT :
# ifdef HAVE_SNPRINTF
         len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
# else
         len = sprintf(msg_str,
# endif
                       _("%s error SSL_ERROR_WANT_ACCEPT : Operation not complete, try again later."),
                       function);
         break;
#endif
      case SSL_ERROR_WANT_CONNECT :
#ifdef HAVE_SNPRINTF
         len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
         len = sprintf(msg_str,
#endif
                       _("%s error SSL_ERROR_WANT_CONNECT : Operation not complete, try again later."),
                       function);
         break;

      case SSL_ERROR_WANT_X509_LOOKUP :
#ifdef HAVE_SNPRINTF
         len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
         len = sprintf(msg_str,
#endif
                       _("%s error SSL_ERROR_WANT_X509_LOOKUP : Operation not complete, tray again."),
                       function);
         break;

      case SSL_ERROR_SYSCALL :
         {
            unsigned long queued;

            queued = ERR_get_error();
            if (queued == 0)
            {
               if (reply == 0)
               {
#ifdef HAVE_SNPRINTF
                  len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
                  len = sprintf(msg_str,
#endif
                                _("%s error SSL_ERROR_SYSCALL : Observed EOF which violates the protocol."),
                                 function);
               }
               else if (reply == -1)
                    {
#ifdef HAVE_SNPRINTF
                       len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
                       len = sprintf(msg_str,
#endif
                                     _("%s error SSL_ERROR_SYSCALL : %s"),
                                     function, strerror(errno));
                    }
                    else
                    {
#ifdef HAVE_SNPRINTF
                        len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
                        len = sprintf(msg_str,
#endif
                                      _("%s error SSL_ERROR_SYSCALL : No error queued."),
                                      function);
                    }
            }
            else
            {
               SSL_load_error_strings();
#ifdef HAVE_SNPRINTF
               len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
               len = sprintf(msg_str,
#endif
                             _("%s error SSL_ERROR_SYSCALL : %s"),
                             function, ERR_error_string(queued, NULL));
               ERR_free_strings();
            }
         }
         break;

      case SSL_ERROR_SSL :
         SSL_load_error_strings();
#ifdef HAVE_SNPRINTF
         len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
         len = sprintf(msg_str,
#endif
                       _("%s error SSL_ERROR_SSL : %s"),
                       function, ERR_error_string(ERR_get_error(), NULL));
         ERR_free_strings();
         break;

      default :
#ifdef HAVE_SNPRINTF
         len = snprintf(msg_str, MAX_RET_MSG_LENGTH,
#else
         len = sprintf(msg_str,
#endif
                       _("%s error unknown (%d)."), function, *p_ret);
   }
   return(msg_str + len);
}
#endif
