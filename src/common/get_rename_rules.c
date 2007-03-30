/*
 *  get_rename_rules.c - Part of AFD, an automatic file distribution program.
 *  Copyright (c) 1997 - 2006 Holger Kiehl <Holger.Kiehl@dwd.de>
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
 **   get_rename_rules - reads rename rules from a file and stores them
 **                      in a shared memory area
 **
 ** SYNOPSIS
 **   void get_rename_rules(char *rule_file, int verbose)
 **
 ** DESCRIPTION
 **   This function reads the rule file and stores the contents in the
 **   global structure rule. The contents of the rule file looks as
 **   follows:
 **
 **      [T4-charts]
 **      *PGAH??_EGRR*     *waf_egr_nat_000_000*
 **      *PGCX??_EGRR*     *waf_egr_gaf_000_900*
 **
 **   Where [T4-charts] is here defined as a rule header. *PGAH??_EGRR*
 **   and *PGCX??_EGRR* are the filter/file-mask, while the rest is the
 **   part to which we want to rename.
 **   The  number of rule headers and rules is not limited.
 **
 **   The caller is responsible for freeing the memory for rule.filter
 **   and rule.rename_to.
 **
 ** RETURN VALUES
 **   None. It will exit with INCORRECT if it fails to allocate memory
 **   or fails to open the rule_file.
 **
 ** AUTHOR
 **   H.Kiehl
 **
 ** HISTORY
 **   01.02.1997 H.Kiehl Created
 **   01.12.2002 H.Kiehl No need for two newlines before each rule header.
 **                      Added verbose flag and more info when set.
 **   12.05.2006 H.Kiehl Handle case the rename rule starts without a
 **                      newline as first line.
 **   08.11.2006 H.Kiehl Allow for spaces in the rule and rename_to part.
 **                      The space must then be preceeded by a \.
 **
 */
DESCR__E_M3

#include <stdio.h>
#include <string.h>          /* strcpy(), strerror()                     */
#include <stdlib.h>          /* calloc(), free()                         */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>          /* read(), close()                          */
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <errno.h>
#include "amgdefs.h"

/* External global variables */
extern int         no_of_rule_headers;
extern struct rule *rule;


/*########################## get_rename_rules() #########################*/
void
get_rename_rules(char *rule_file, int verbose)
{
   static time_t last_read = 0;
   static int    first_time = YES;
   struct stat   stat_buf;

   if (stat(rule_file, &stat_buf) == -1)
   {
      if (errno == ENOENT)
      {
         /*
          * Only tell user once that the rules file is missing. Otherwise
          * it is anoying to constantly receive this message.
          */
         if (first_time == YES)
         {
            if (verbose == YES)
            {
               system_log(INFO_SIGN, __FILE__, __LINE__,
                          "There is no renaming rules file %s", rule_file);
            }
            first_time = NO;
         }
      }
      else
      {
         system_log(WARN_SIGN, __FILE__, __LINE__,
                    "Failed to stat() %s : %s", rule_file, strerror(errno));
      }
   }
   else
   {
      if (stat_buf.st_mtime != last_read)
      {
         register int i;
         int          fd;
         char         *last_ptr,
                      *ptr,
                      *buffer;

         if (first_time == YES)
         {
            first_time = NO;
         }
         else
         {
            if (verbose == YES)
            {
               system_log(INFO_SIGN, NULL, 0, "Rereading renaming rules file.");
            }
         }

         if (last_read != 0)
         {
            /*
             * Since we are rereading the whole rules file again
             * lets release the memory we stored for the previous
             * structure of rule.
             */
            for (i = 0; i < no_of_rule_headers; i++)
            {
               if (rule[i].filter != NULL)
               {
                  FREE_RT_ARRAY(rule[i].filter);
               }
               if (rule[i].rename_to != NULL)
               {
                  FREE_RT_ARRAY(rule[i].rename_to);
               }
            }
            free(rule);
            no_of_rule_headers = 0;
         }
         last_read = stat_buf.st_mtime;

         /* Allocate memory to store file */
         if ((buffer = malloc(1 + stat_buf.st_size + 1)) == NULL)
         {
            system_log(FATAL_SIGN, __FILE__, __LINE__,
                       "malloc() error : %s", strerror(errno));
            exit(INCORRECT);
         }

         /* Open file. */
         if ((fd = open(rule_file, O_RDONLY)) == -1)
         {
            system_log(FATAL_SIGN, __FILE__, __LINE__,
                       "Failed to open() %s : %s", rule_file, strerror(errno));
            free(buffer);
            exit(INCORRECT);
         }

         /* Read file into buffer. */
         buffer[0] = '\n';
         if (read(fd, &buffer[1], stat_buf.st_size) != stat_buf.st_size)
         {
            system_log(FATAL_SIGN, __FILE__, __LINE__,
                       "Failed to read() %s : %s", rule_file, strerror(errno));
            free(buffer);
            (void)close(fd);
            exit(INCORRECT);
         }
         if (close(fd) == -1)
         {
            system_log(DEBUG_SIGN, __FILE__, __LINE__,
                       "close() error : %s", strerror(errno));
         }
         buffer[stat_buf.st_size] = '\0';

         /*
          * Now that we have the contents in the buffer lets first see
          * how many rules there are in the buffer so we can allocate
          * memory for the rules.
          */
         ptr = buffer;
         last_ptr = buffer + stat_buf.st_size;
         if (buffer[0] == '[')
         {
            no_of_rule_headers++;
         }
         while ((ptr = posi(ptr, "\n[")) != NULL)
         {
            no_of_rule_headers++;
         }

         if (no_of_rule_headers > 0)
         {
            register int j, k;
            int          no_of_rules,
                         max_rule_length,
                         count,
                         total_no_of_rules = 0;
            char         *end_ptr,
                         *search_ptr;

            if ((rule = calloc(no_of_rule_headers,
                               sizeof(struct rule))) == NULL)
            {
               system_log(FATAL_SIGN, __FILE__, __LINE__,
                          "calloc() error : %s (%s %d)\n", strerror(errno));
               exit(INCORRECT);
            }
            ptr = buffer;

            for (i = 0; i < no_of_rule_headers; i++)
            {
               if (((ptr == buffer) && (buffer[0] == '[')) ||
                   ((ptr = posi(ptr, "\n[")) != NULL))
               {
                  /*
                   * Lets first determine how many rules there are.
                   * This is simply done by counting the \n characters.
                   * While doing this, lets also find the longest rule or
                   * rename_to string.
                   */
                  if ((ptr == buffer) && (buffer[0] == '['))
                  {
                     ptr++;
                  }
                  no_of_rules = max_rule_length = 0;
                  search_ptr = ptr;
                  while (*search_ptr != '\n')
                  {
                     search_ptr++;
                  }
                  do
                  {
                     search_ptr++;

                     /* Ignore any comments. */
                     if (*search_ptr == '#')
                     {
                        while ((*search_ptr != '\n') && (*search_ptr != '\0'))
                        {
                           search_ptr++;
                        }
                     }
                     else
                     {
                        count = 0;
                        while ((*search_ptr != ' ') && (*search_ptr != '\t') &&
                               (search_ptr < last_ptr))
                        {
                           if ((*search_ptr == '\\') &&
                               (*(search_ptr + 1) == ' '))
                           {
                              count++; search_ptr++;
                           }
                           count++; search_ptr++;
                        }
                        if (search_ptr == last_ptr)
                        {
                           break;
                        }
                        if (count > max_rule_length)
                        {
                           max_rule_length = count;
                        }
                        while ((*search_ptr == ' ') || (*search_ptr == '\t'))
                        {
                           search_ptr++;
                        }
                        count = 0;
                        while ((*search_ptr != '\n') && (*search_ptr != '\0'))
                        {
                           count++; search_ptr++;
                        }
                        if (count > max_rule_length)
                        {
                           max_rule_length = count;
                        }
                        no_of_rules++;
                     }
                  } while ((*(search_ptr + 1) != '\n') &&
                           (*(search_ptr + 1) != '[') &&
                           (*search_ptr != '\0') &&
                           (*(search_ptr + 1) != '\0'));

                  /* Allocate memory for filter and rename_to */
                  /* part of struct rule.                     */
                  rule[i].filter = NULL;
                  RT_ARRAY(rule[i].filter, no_of_rules,
                           max_rule_length + 1, char);
                  rule[i].rename_to = NULL;
                  RT_ARRAY(rule[i].rename_to, no_of_rules,
                           max_rule_length + 1, char);

                  ptr--;
                  end_ptr = ptr;
                  while ((*end_ptr != ']') && (*end_ptr != '\n') &&
                         (*end_ptr != '\0'))
                  {
                     end_ptr++;
                  }
                  if ((end_ptr - ptr) <= MAX_RULE_HEADER_LENGTH)
                  {
                     if (*end_ptr == ']')
                     {
                        *end_ptr = '\0';
                        (void)strcpy(rule[i].header, ptr);
                        ptr = end_ptr + 1;

                        while ((*ptr != '\n') && (*ptr != '\0'))
                        {
                           ptr++;
                        }
                        if (*ptr == '\n')
                        {
                           j = 0;

                           do
                           {
                              ptr++;
                              if (*ptr == '#') /* Ignore any comments. */
                              {
                                 while ((*ptr != '\n') && (*ptr != '\0'))
                                 {
                                    ptr++;
                                 }
                              }
                              else
                              {
                                 k = 0;
                                 end_ptr = ptr;
                                 while ((*end_ptr != ' ') && (*end_ptr != '\t') &&
                                        (*end_ptr != '\n') && (*end_ptr != '\0'))
                                 {
                                    if ((*end_ptr == '\\') &&
                                        (*(end_ptr + 1) == ' '))
                                    {
                                       end_ptr++;
                                    }
                                    rule[i].filter[j][k] = *end_ptr;
                                    end_ptr++; k++;
                                 }
                                 if ((*end_ptr == ' ') || (*end_ptr == '\t'))
                                 {
                                    /*
                                     * Store the filter part.
                                     */
                                    rule[i].filter[j][k] = '\0';
                                    end_ptr++;
                                    while ((*end_ptr == ' ') || (*end_ptr == '\t'))
                                    {
                                       end_ptr++;
                                    }

                                    /*
                                     * Store the renaming part.
                                     */
                                    ptr = end_ptr;
                                    k = 0;
                                    while ((*end_ptr != ' ') && (*end_ptr != '\t') &&
                                           (*end_ptr != '\n') && (*end_ptr != '\0'))
                                    {
                                       if ((*end_ptr == '\\') &&
                                           (*(end_ptr + 1) == ' '))
                                       {
                                          end_ptr++;
                                       }
                                       rule[i].rename_to[j][k] = *end_ptr;
                                       end_ptr++; k++;
                                    }
                                    rule[i].rename_to[j][k] = '\0';
                                    if ((*end_ptr == ' ') || (*end_ptr == '\t'))
                                    {
                                       int more_data = NO;

                                       end_ptr++;
                                       while ((*end_ptr != '\n') &&
                                              (*end_ptr != '\0'))
                                       {
                                          if ((more_data == NO) &&
                                              ((*end_ptr != ' ') || (*end_ptr != '\t')))
                                          {
                                             more_data = YES;
                                          }
                                          end_ptr++;
                                       }
                                       if (more_data == YES)
                                       {
                                          system_log(WARN_SIGN, __FILE__, __LINE__,
                                                     "In rule [%s] the rule %s %s has data after the rename-to-part. Ignoring it!",
                                                     rule[i].header,
                                                     rule[i].filter[j],
                                                     rule[i].rename_to[j]);
                                       }
                                    }
                                    ptr = end_ptr;
                                    j++;
                                 }
                                 else
                                 {
                                    if (end_ptr != ptr)
                                    {
                                       system_log(WARN_SIGN, __FILE__, __LINE__,
                                                  "A filter is specified for the rule header %s but not a rule.",
                                                  rule[i].header);
                                       ptr = end_ptr;
                                    }
                                    else if (*ptr == '\n')
                                         {
                                            ptr++;
                                         }
                                 }
                              }
                           } while ((*ptr == '\n') && (j < no_of_rules));

                           rule[i].no_of_rules = j;
                           total_no_of_rules += j;
                        }
                        else
                        {
                           system_log(WARN_SIGN, __FILE__, __LINE__,
                                      "Rule header %s specified, but could not find any rules.",
                                      rule[i].header);
                        }
                     }
                     else
                     {
                        system_log(WARN_SIGN, __FILE__, __LINE__,
                                   "Failed to determine the end of the rule header.");
                     }
                  }
                  else
                  {
                     system_log(WARN_SIGN, __FILE__, __LINE__,
                                "Rule header to long. May not be longer then %d Bytes [MAX_RULE_HEADER_LENGTH].",
                                MAX_RULE_HEADER_LENGTH);
                  }
               }
               else
               {
                  /* Impossible! We just did find it and now it's gone?!? */
                  system_log(DEBUG_SIGN, __FILE__, __LINE__,
                             "Could not get start of rule header %d [%d].",
                             i, no_of_rule_headers);
                  rule[i].filter = NULL;
                  rule[i].rename_to = NULL;
                  break;
               }
            } /* for (i = 0; i < no_of_rule_headers; i++) */

            if (verbose == YES)
            {
               system_log(INFO_SIGN, NULL, 0,
                          "Found %d rename rule headers with %d rules.",
                          no_of_rule_headers, total_no_of_rules);
            }
         } /* if (no_of_rule_headers > 0) */
         else
         {
            if (verbose == YES)
            {
               system_log(INFO_SIGN, NULL, 0,
                          "No rename rules found in %s", rule_file);
            }
         }

         /* The buffer holding the contents of the rule file */
         /* is no longer needed.                             */
         free(buffer);

#ifdef _DEBUG_RULES
         {
            register int j;

            for (i = 0; i < no_of_rule_headers; i++)
            {
               system_log(DEBUG_SIGN, NULL, 0, "[%s]", rule[i].header);
               for (j = 0; j < rule[i].no_of_rules; j++)
               {
                  system_log(DEBUG_SIGN, NULL, 0, "%s  %s",
                             rule[i].filter[j], rule[i].rename_to[j]);
               }
            }
         }
#endif /* _DEBUG_RULES */
      } /* if (stat_buf.st_mtime != last_read) */
   }


   return;
}
