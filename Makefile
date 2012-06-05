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

SWIFT_DIR=/opt/swift

SYS_LIB_DIR=/usr/lib
SYS_INC_DIR=/usr/include

AST_MOD_DIR=$(SYS_LIB_DIR)/asterisk/modules
AST_INC_DIR=$(SYS_INC_DIR)/asterisk
AST_CFG_DIR=/etc/asterisk
AST_VER_HDR=$(AST_INC_DIR)/version.h

CFLAGS=-I${SWIFT_DIR}/include -I${SYS_INC_DIR} -g -Wall -fPIC
LDFLAGS=-L${SWIFT_DIR}/lib -L${SYS_LIB_DIR} -lswift $(patsubst ${SWIFT_DIR}/lib/lib%.so,-l%,$(wildcard ${SWIFT_DIR}/lib/libcep*.so))
SOLINK=-shared -Xlinker -x

CFLAGS+=-D_SWIFT_VER_$(shell \
	swift --version | grep "Cepstral Swift " - | sed -e "s/Cepstral\ Swift\ //" - | awk -F. '{printf "%01d", $$1}' -; \
)

AST_INC_CHECK=$(shell if [ -f $(AST_INC_DIR)/channel.h ]; then echo "$(NAME).so"; else echo "ast_inc_fail"; fi)

AST_FULL_VER=$(shell \
	if [ -f $(AST_VER_HDR) ]; then \
		grep ASTERISK_VERSION_NUM $(AST_VER_HDR) | sed -e "s/\#define\ ASTERISK_VERSION_NUM\ //" - | awk -F. '{printf "%02d", $$1}' -; \
	else echo "Unable to locate the required Asterisk version.h include file at '$(AST_VER_HDR)'"; \
	fi \
)

AST_MAJOR_VER=$(shell \
	if [ $(findstring 104, $(AST_FULL_VER)) ]; then \
		echo "-D_AST_VER_1_4"; \
	elif [ $(findstring 106, $(AST_FULL_VER)) ]; then \
		echo "-D_AST_VER_1_6"; \
	elif [ $(findstring 108, $(AST_FULL_VER)) ]; then \
		echo "-D_AST_VER_1_8"; \
	elif [ $(findstring 100, $(AST_FULL_VER)) ]; then \
		echo "-D_AST_VER_10"; \
	fi \
)

ifeq ($(AST_MAJOR_VER),)
	AST_VER_CHECK=ast_ver_fail
else
	AST_VER_CHECK=
	CFLAGS+= $(AST_MAJOR_VER)
endif


all: banner $(AST_INC_CHECK) $(AST_VER_CHECK)
	@echo ""
	@echo "  ********************************************************"
	@echo "  *  Run 'make install' to install the app_swift module. *"
	@echo "  ********************************************************"
	@echo ""

$(NAME).so : $(NAME).o
	$(CC) $(SOLINK) -o $@ $(LDFLAGS) $<

banner:
	@echo ""
	@echo ""
	@echo "                                         _    ___         "
	@echo "                                        (_)  / __)  _     "
	@echo "    _____ ____  ____           ___ _ _ _ _ _| |__ _| |_   "
	@echo "   (____ |  _ \|  _ \         /___) | | | (_   __|_   _)  "
	@echo "   / ___ | |_| | |_| | ____  |___ | | | | | | |    | |_   "
	@echo "   \_____|  __/|  __/ (____) |___/ \___/|_| |_|     \__)  "
	@echo "         |_|   |_|                                        "
	@echo ""

ast_ver_fail:
	@echo "   An unsupported version of Asterisk has been detected. $(AST_FULL_VER)"
	@echo ""
	@exit 1

ast_inc_fail:
	@echo "   Could not locate Asterisk include files."
	@echo ""
	@echo "   Try re-running with:"
	@echo ""
	@echo "      'make SYS_INC_DIR=/system/include/directory'"
	@echo ""
	@exit 1

clean:
	rm -f $(NAME).o $(NAME).so

install: all
	if ! [ -f $(AST_CFG_DIR)/$(CONF) ]; then \
		install -m 644 $(CONF).sample /etc/asterisk/$(CONF) ; \
	fi
	if [ -f $(NAME).so ]; then \
		install -m 755 $(NAME).so $(AST_MOD_DIR) ; \
	fi

reload: install
	asterisk -rx "module unload ${RES}"
	asterisk -rx "module load ${RES}"
