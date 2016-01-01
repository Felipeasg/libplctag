#
#   Copyright 2015, Process Control Engineers
#   Author: Kyle Hayes
#
#    This library is free software; you can redistribute it and/or
#    modify it under the terms of the GNU Library General Public
#    License as published by the Free Software Foundation; either
#    version 2 of the License, or (at your option) any later version.
#
#    This library is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#    Library General Public License for more details.
#
#    You should have received a copy of the GNU Library General Public
#    License along with this library; if not, write to the Free Software
#    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
#    USA
#

CC?=gcc
INSTALL=install

# determine the OS and target for Linux/Mac
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LIBPLC_LIB_SO=libplctag.so
endif
ifeq ($(UNAME_S),Darwin)
    LIBPLC_LIB_SO=libplctag.dylib
endif

LIBPLC_LIB_SRC=libplctag_tag.c \
				ab/ab.c ab/common.c ab/connection.c ab/session.c \
				ab/explicit/explicit.c \
				linux/platform.c util/attr.c util/platform_ext.c

LIBPLC_LIB_OBJ=$(LIBPLC_LIB_SRC:%.c=%.o)

DESTDIR=/usr/local/
libdir=lib
incdir=include

CFLAGS += -std=c99 -fPIC -fno-strict-aliasing -g -I. -fms-extensions -Wall -I./linux -D__USE_POSIX=1 -D_POSIX_C_SOURCE=200809L

TARGETS = $(LIBPLC_LIB_SO)

all: $(TARGETS)

$(LIBPLC_LIB_SO): $(LIBPLC_LIB_OBJ)
	$(CC) -o $@ -shared $(LIBPLC_LIB_OBJ)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -rf $(TARGETS) $(LIBPLC_LIB_OBJ) linux/*.o ab/*.o *.o *.so *~ Makefile.depends

install: all
	$(INSTALL) -m 644 libplctag.so $(DESTDIR)$(libdir)/
	$(INSTALL) -m 644 libplctag.h $(DESTDIR)$(incdir)/

newdepend: killdepend
	@echo "*******************************************"
	@echo "** Building dependencies..."
	@for mdir in $(SUBDIRS); \
		do $(MAKE) -C $$mdir depend; \
	done

killdepend:
	@echo "*******************************************"
	@echo "** Removing Old dependencies..."
	@find . -name "Makefile.depends" -print | $(XARGS) rm %

