AM_CFLAGS = -Wall -g \
    -DARIM_FILESDIR='"$(datadir)/$(PACKAGE_NAME)"' \
    -DARIM_DOCDIR='"$(datarootdir)/doc/$(PACKAGE_NAME)"' \
    -DARIM_NAME='"$(PACKAGE_NAME)"' \
    -DARIM_VERSION='"$(PACKAGE_VERSION)"'
if PORTABLE_BIN
AM_CFLAGS += -DPORTABLE_BIN
endif

if PORTABLE_BIN
exedir = $(prefix)
endif
if PORTABLE_BIN
exe_PROGRAMS = arim
else
bin_PROGRAMS = arim
endif
if PORTABLE_BIN
topdir = $(prefix)
top_DATA = arim.ini in.mbox out.mbox sent.mbox
filesdir = $(prefix)/files
files_DATA = files/test.txt
docsdir = $(prefix)/doc
docs_DATA = help/arim-help.pdf NEWS AUTHORS COPYING README
else
filesdir = $(datadir)/$(PACKAGE_NAME)
files_DATA = files/test.txt arim.ini in.mbox
docsdir = $(datarootdir)/doc/$(PACKAGE_NAME)
docs_DATA = help/arim-help.pdf NEWS AUTHORS COPYING README
dist_man_MANS = arim.1 arim.5
endif

EXTRA_DIST = files/test.txt help/arim-help.pdf ChangeLog in.mbox out.mbox sent.mbox arim.ini
if PORTABLE_BIN
EXTRA_DIST += $(PACKAGE_NAME)
endif

arim_SOURCES = \
    main.c main.h \
    arim.c arim.h \
    arim_arq.c arim_arq.h \
    arim_beacon.c arim_beacon.h \
    arim_message.c arim_message.h \
    arim_ping.c  arim_ping.h \
    arim_proto.c arim_proto.h \
    arim_query.c arim_query.h \
    bufq.c bufq.h \
    cmdproc.c cmdproc.h \
    cmdthread.c cmdthread.h \
    datathread.c datathread.h \
    ini.c ini.h \
    log.c log.h \
    mbox.c mbox.h \
    ui.c ui.h \
    ui_dialog.c ui_dialog.h \
    ui_fec_menu.c ui_fec_menu.h \
    ui_files.c ui_files.h \
    ui_help_menu.c ui_help_menu.h \
    ui_msg.c ui_msg.h \
    util.c util.h

if PORTABLE_BIN
uninstall-hook:
	if test -d $(topdir); then rm -rf $(topdir); fi
dist-hook:
	rm -f $(distdir)/*.h
	rm -f $(distdir)/*.c
	rm -f $(distdir)/compile
	rm -f $(distdir)/depcomp
	rm -f $(distdir)/install-sh
	rm -f $(distdir)/missing
	rm -f $(distdir)/config.*
	rm -f $(distdir)/configure*
	rm -f $(distdir)/aclocal.m4
	rm -f $(distdir)/Makefile*
	rm -f $(distdir)/INSTALL
	rm -f $(distdir)/ChangeLog
	mkdir $(distdir)/doc
	mv $(distdir)/help/arim-help.pdf $(distdir)/doc/arim-help.pdf
	mv $(distdir)/AUTHORS $(distdir)/doc/AUTHORS
	mv $(distdir)/README $(distdir)/doc/README
	mv $(distdir)/COPYING $(distdir)/doc/COPYING
	mv $(distdir)/NEWS $(distdir)/doc/NEWS
	rm -rf $(distdir)/help
	tar czhof $(distdir)-bin-$(target_os)-$(target_cpu).tar.gz $(distdir) 
	echo "rm -f $(distdir).tar.gz" > rm-unwanted.sh
	echo "tar tzvf $(distdir)-bin-$(target_os)-$(target_cpu).tar.gz" > list-archive.sh
distcheck:
	@:
else
uninstall-hook:
	if test -d $(docsdir); then rm -rf $(docsdir); fi
endif

