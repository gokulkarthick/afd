## Makefile.am -- Process this file with automake to produce Makefile.in
## Copyright (C) 2001 - 2013 Holger Kiehl
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2, or (at your option)
## any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
##

MAINTAINERCLEANFILES = Makefile.in
CLEANFILES           = gmon.out core core.*
noinst_HEADERS       = dir_ctrl.h
AM_CPPFLAGS          = @AFD_MOTIF_INCLUDES@ -I../../.. -I../../common\
                       -I../common -I../../../init_afd -I../mshow_log
bin_PROGRAMS         = dir_ctrl
dir_ctrl_SOURCES     = check_dir_status.c\
                       dir_ctrl.c\
                       dir_expose_handler.c\
                       dir_window_size.c\
                       draw_dir_line.c\
                       mouse_handler.c\
                       redraw_all.c\
                       resize_dir_window.c\
                       select_dir_dialog.c\
                       setup_dir_window.c
dir_ctrl_LDADD       = @AFD_MATH_LIB@ ../common/libmotifafd.a\
                       ../../common/libuiafd.a ../../../common/libafd.a\
                       @AFD_MOTIF_LIBS@

if WITH_SETUID_PROGS
install-exec-local:
	@if [ "`find $(DESTDIR)$(bindir) -user 0 -name dir_ctrl -print`" = "" ] ; \
	then \
		chmod 4755 $(DESTDIR)$(bindir)/dir_ctrl ; \
	fi
else
install-exec-local:
endif
