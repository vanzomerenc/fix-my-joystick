SHELL = /bin/sh
.RECIPEPREFIX = >
.PHONY: all install clean distclean

PKG_CONFIG ?= pkg-config
CFLAGS ?= -Wall -Wextra
PREFIX ?= /usr/local

PROGRAMS = evremap

all: $(PROGRAMS)

evremap: override CFLAGS += $(shell $(PKG_CONFIG) --libs --cflags libevdev)
evremap: evremap.c
> $(CC) $(CFLAGS) -c $< -o $@

install: 
> install -d $(DESTDIR)$(PREFIX)/bin
> install $(PROGRAMS) $(DESTDIR)$(PREFIX)/bin

clean distclean:
> rm evremap
