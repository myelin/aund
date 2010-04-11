AUND = aund.o conf_lex.o fileserver.o fs_cli.o fs_examine.o fs_fileio.o \
	fs_misc.o fs_handle.o fs_util.o fs_error.o fs_nametrans.o \
	fs_filetype.o aun.o beebem.o pw.o

# Warnings to help with coding style
WARNINGS = -Wall \
	-Wdeclaration-after-statement -Wold-style-definition \
	-Wmissing-prototypes -Wredundant-decls \
	-Wno-pointer-sign -Wno-uninitialized

aund: $(AUND)
	gcc -o $@ $(AUND) -lcrypt

.l.c:
	flex -t $*.l > $*.c

.c.o:
	gcc -MM $*.c > $*.d && gcc -g -O2 $(WARNINGS) -c $*.c

-include *.d

clean:
	rm -f *.o conf_lex.c aund

spotless: clean
	rm -f *.d

