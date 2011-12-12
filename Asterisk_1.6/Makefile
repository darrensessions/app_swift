#  app_swift -- A Cepstral Swift TTS engine interface
# 
#  Copyright (C) 2006 - 2010, Darren Sessions
# 
#  Darren Sessions <darrensessions@me.com>
# 
#  This program is free software, distributed under the
#  terms of the GNU General Public License Version 2. See
#  the LICENSE file at the top of the source tree for more
#  information.
 

NAME=app_swift
CONF=swift.conf

CC=gcc
OSARCH=$(shell uname -s)

SWIFT_DIR=/opt/swift
CFLAGS=-I${SWIFT_DIR}/include -g -Wall -D_REENTRANT -D_GNU_SOURCE -fPIC
LDFLAGS=-L${SWIFT_DIR}/lib -lswift $(patsubst ${SWIFT_DIR}/lib/lib%.so,-l%,$(wildcard ${SWIFT_DIR}/lib/libcep*.so))
SOLINK=-shared -Xlinker -x

RES=$(shell if [ -f /usr/include/asterisk/channel.h ]; then echo "$(NAME).so"; fi)

MODULES_DIR=/usr/lib/asterisk/modules

$(NAME).so : $(NAME).o
	$(CC) $(SOLINK) -o $@ $(LDFLAGS) $<

all: $(RES)

clean:
	rm -f $(NAME).o $(NAME).so

install: all
	if ! [ -f /etc/asterisk/$(CONF) ]; then \
		install -m 644 $(CONF).sample /etc/asterisk/$(CONF) ; \
	fi
	if [ -f $(NAME).so ]; then \
		install -m 755 $(NAME).so $(MODULES_DIR) ; \
	fi

reload: install
	asterisk -rx "module unload ${RES}"
	asterisk -rx "module load ${RES}"

