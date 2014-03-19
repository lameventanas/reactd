# $Id$

CFLAGS = -pipe -DDEBUG -ggdb

all: reactd keylist_test threshold_test subst_test

debug: CFLAGS += -DDEBUG -ggdb
debug: all

objects_reactd = reactd_conf.tab.o reactd_conf.lex.yy.o reactd.o keylist.o pcre_subst.o threshold.o
objects_keylist_test = keylist_test.o keylist.o
objects_threshold_test = threshold_test.o threshold.o keylist.o
objects_subst_test = subst_test.o pcre_subst.o

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $*.c -o $*.o

reactd_conf.tab.c reactd_conf.tab.h: reactd_conf.y
	bison -d reactd_conf.y

reactd_conf.lex.yy.c: reactd_conf.l reactd_conf.tab.h
	flex -o reactd_conf.lex.yy.c reactd_conf.l

reactd: $(objects_reactd)
	$(CC) $(CFLAGS) $(objects_reactd) -lfl -lpcre -o reactd

keylist_test: $(objects_keylist_test)
	$(CC) $(CFLAGS) $(objects_keylist_test) -o keylist_test
	
threshold_test: $(objects_threshold_test)
	$(CC) $(CFLAGS) $(objects_threshold_test) -o threshold_test -lpthread

subst_test: $(objects_subst_test)
	$(CC) $(CFLAGS) $(objects_subst_test) -lpcre -o subst_test

clean:
	rm -f reactd $(objects_reactd) keylist_test $(objects_keylist_test) threshold_test $(objects_threshold_test) subst_test $(objects_subst_test)

