#  app_swift -- A Cepstral Swift TTS engine interface
# 
#  Copyright (C) 2006 - 2011, Darren Sessions
# 
#  Darren Sessions <darrensessions@me.com>
#
#  http://www.twitter.com/darrensessions
#  http://www.linkedin.com/in/dsessions
# 
#  This program is free software, distributed under the
#  terms of the GNU General Public License Version 2. See
#  the LICENSE file at the top of the source tree for more
#  information.

NAME=app_swift
CONF=swift.conf

CC=gcc
OSARCH=$(shell uname -s)
ASTERISK_ROOT_DIR=/usr
ASTERISK_LIB_DIR=$(ASTERISK_ROOT_DIR)/lib
ASTERISK_MODULES_DIR=$(ASTERISK_LIB_DIR)/asterisk/modules
ASTERISK_INCLUDE_DIR=$(ASTERISK_ROOT_DIR)/include
ASTERISK_CONFIG_DIR=/etc/asterisk

SWIFT_DIR=/opt/swift
CFLAGS=-I${SWIFT_DIR}/include -I${ASTERISK_INCLUDE_DIR} -g -Wall -D_REENTRANT -D_GNU_SOURCE -fPIC
LDFLAGS=-L${SWIFT_DIR}/lib -L${ASTERISK_LIB_DIR} -lswift $(patsubst ${SWIFT_DIR}/lib/lib%.so,-l%,$(wildcard ${SWIFT_DIR}/lib/libcep*.so))
SOLINK=-shared -Xlinker -x

RES=$(shell if [ -f $(ASTERISK_INCLUDE_DIR)/asterisk/channel.h ]; then echo "$(NAME).so"; else echo "missing_includes"; fi)


all: $(RES)
	@echo ""
	@echo ""
	@echo "                                         _    ___         "
	@echo "                                        (_)  / __)  _     "
	@echo "    _____ ____  ____           ___ _ _ _ _ _| |__ _| |_   "
	@echo "   (____ |  _ \|  _ \         /___) | | | (_   __|_   _)  "
	@echo "   / ___ | |_| | |_| | ____  |___ | | | | | | |    | |_   "
	@echo "   \_____|  __/|  __/ (____) |___/ \___/|_| |_|     \__)  "
	@echo "         |_|   |_|                                        "
	@echo "  ********************************************************"
	@echo "  *  Run 'make install' to install the app_swift module. *"
	@echo "  ********************************************************"
	@echo ""

$(NAME).so : $(NAME).o
	$(CC) $(SOLINK) -o $@ $(LDFLAGS) $<

missing_includes:
	@echo "Could not locate Asterisk include files."
	@echo "Try re-running with 'make ASTERISK_ROOT_DIR=/path/to/asterisk'"
	@exit 1

clean:
	rm -f $(NAME).o $(NAME).so

install: all
	if ! [ -f $(ASTERISK_CONFIG_DIR)/$(CONF) ]; then \
		install -m 644 $(CONF).sample /etc/asterisk/$(CONF) ; \
	fi
	if [ -f $(NAME).so ]; then \
		install -m 755 $(NAME).so $(ASTERISK_MODULES_DIR) ; \
	fi

reload: install
	asterisk -rx "module unload ${RES}"
	asterisk -rx "module load ${RES}"
