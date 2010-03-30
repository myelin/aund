AUND = aund.o conf_lex.o fileserver.o fs_cli.o fs_examine.o fs_fileio.o \
	fs_misc.o fs_handle.o fs_util.o fs_error.o fs_nametrans.o \
	fs_filetype.o

aund: $(AUND)
	gcc -o $@ $(AUND)

.l.c:
	flex -t $*.l > $*.c

.c.o:
	gcc -MM $*.c > $*.d && gcc -g -O0 -c $*.c

-include *.d

clean:
	rm -f *.o conf_lex.c aund

spotless: clean
	rm -f *.d

