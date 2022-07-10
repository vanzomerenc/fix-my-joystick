SHELL = /bin/sh
.RECIPEPREFIX = >
.PHONY: all install clean distclean

PKG_CONFIG ?= pkg-config
CFLAGS ?= -Wall -Wextra
PREFIX ?= /usr/local

PROGRAMS = evremap

all: $(PROGRAMS)

evremap: override CFLAGS += $(shell $(PKG_CONFIG) --cflags libevdev)
evremap: override LDLIBS += $(shell $(PKG_CONFIG) --libs libevdev)

install: $(PROGRAMS)
> install -d $(DESTDIR)$(PREFIX)/bin
> install $(PROGRAMS) $(DESTDIR)$(PREFIX)/bin

clean distclean:
> $(RM) *.o $(PROGRAMS)
