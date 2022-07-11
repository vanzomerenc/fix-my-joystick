SHELL = /bin/sh
.RECIPEPREFIX = >
.PHONY: all install uninstall dist clean distclean

VERSION := 0.1
PACKAGE := evremap-$(VERSION)

CFLAGS ?= -Wall -Wextra
PREFIX ?= /usr/local

DIST = Makefile evremap.c
PROGRAMS = evremap

all: $(PROGRAMS)

evremap: override CFLAGS += $(shell pkg-config --cflags libevdev)
evremap: override LDLIBS += $(shell pkg-config --libs libevdev)

install: $(PROGRAMS)
> install -d $(DESTDIR)$(PREFIX)/bin
> install $(PROGRAMS) $(DESTDIR)$(PREFIX)/bin

uninstall:
> rm -f $(foreach PROGRAM, $(PROGRAMS), $(DESTDIR)$(PREFIX)/bin/$(PROGRAM))

dist: $(DIST)
> rm -rf $(PACKAGE)
> mkdir $(PACKAGE)
> cp $(DIST) $(PACKAGE)
> tar -czf $(PACKAGE).tar.gz $(PACKAGE)

clean distclean:
> rm -rf *.o $(PROGRAMS) $(PACKAGE) $(PACKAGE).tar.gz
