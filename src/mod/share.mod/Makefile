# Makefile for src/mod/share.mod/
# $Id: Makefile,v 1.11 2000/07/09 14:10:49 fabian Exp $

srcdir = .


doofus:
	@echo ""
	@echo "Let's try this from the right directory..."
	@echo ""
	@cd ../../../ && make

static: ../share.o

modules: ../../../share.$(MOD_EXT)

../share.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -DMAKING_MODS -c $(srcdir)/share.c
	@rm -f ../share.o
	mv share.o ../

../../../share.$(MOD_EXT): ../share.o
	$(LD) -o ../../../share.$(MOD_EXT) ../share.o
	$(STRIP) ../../../share.$(MOD_EXT)

depend:
	$(CC) $(CFLAGS) $(CPPFLAGS) -MM $(srcdir)/share.c > .depend

clean:
	@rm -f .depend *.o *.$(MOD_EXT) *~
distclean: clean

#safety hash
../share.o: .././share.mod/share.c ../../../src/mod/module.h \
 ../../../src/main.h ../../../src/lang.h ../../../src/eggdrop.h \
 ../../../src/flags.h ../../../src/proto.h ../../../lush.h \
 ../../../src/misc_file.h ../../../src/cmdt.h ../../../src/tclegg.h \
 ../../../src/tclhash.h ../../../src/chan.h ../../../src/users.h \
 ../../../src/compat/compat.h ../../../src/compat/inet_aton.h \
 ../../../src/compat/snprintf.h ../../../src/compat/memset.h \
 ../../../src/compat/memcpy.h ../../../src/compat/strcasecmp.h \
 ../../../src/mod/modvals.h ../../../src/tandem.h \
 ../../../src/mod/transfer.mod/transfer.h \
 ../../../src/mod/channels.mod/channels.h ../share.mod/share.h \
 ../share.mod/uf_features.c
