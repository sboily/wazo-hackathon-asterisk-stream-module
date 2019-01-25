#
# Makefile for Asterisk websocket stream resource
# Copyright (C) 2019, Sylvain Boily
#
# This program is free software, distributed under the terms of
# the GNU General Public License Version 3. See the COPYING file
# at the top of the source tree.
#

INSTALL = install
ASTETCDIR = $(INSTALL_PREFIX)/etc/asterisk
CONFNAME = $(basename $(SAMPLENAME))

TARGET = res_ari_stream.so
OBJECTS = res_ari_stream.o
CFLAGS += -Wall -Werror -Wextra -Wno-unused-parameter -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Winit-self -Wmissing-format-attribute \
          -Wformat=2 -g -fPIC -D_GNU_SOURCE -D'AST_MODULE="res_ari_stream"' -D'AST_MODULE_SELF_SYM=__internal_res_ari_stream_self'
LIBS += 
LDFLAGS = -Wall -shared

.PHONY: install clean

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LIBS)

%.o: %.c $(HEADERS)
	$(CC) -c $(CFLAGS) -o $@ $<

install: $(TARGET)
	mkdir -p $(DESTDIR)/usr/lib/asterisk/modules
	install -m 644 $(TARGET) $(DESTDIR)/usr/lib/asterisk/modules/
	@echo " +----- res_ari_stream installed ------+"
	@echo " +                                           +"
	@echo " + res_ari_stream has successfully           +"
	@echo " + been installed.                           +"
	@echo " +-------------------------------------------+"

clean:
	rm -f $(OBJECTS)
	rm -f $(TARGET)

samples:
	$(INSTALL) -m 644 $(SAMPLENAME) $(DESTDIR)$(ASTETCDIR)/$(CONFNAME)
	@echo " ------- res_ari_stream config installed ---------"
