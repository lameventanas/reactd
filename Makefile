# $Id$

CFLAGS = -pipe -DDEBUG -ggdb

all: reactd

debug: CFLAGS += -DDEBUG -ggdb
debug: all

reactd_conf.tab.c reactd_conf.tab.h: reactd_conf.y
	bison -d reactd_conf.y

reactd_conf.lex.yy.c: reactd_conf.l reactd_conf.tab.h
	flex -o reactd_conf.lex.yy.c reactd_conf.l

reactd: reactd_conf.lex.yy.c reactd_conf.tab.c reactd_conf.tab.h reactd.c
	gcc $(CFLAGS) reactd_conf.tab.c reactd_conf.lex.yy.c reactd.c -lfl -lpcre -o reactd

clean:
	rm -f reactd_conf.tab.c reactd_conf.tab.h reactd_conf.lex.yy.c reactd keylist_test threshold_test subst_test

keylist_test: keylist_test.c keylist.c keylist.h
	gcc $(CFLAGS) keylist_test.c keylist.c -o keylist_test
	
threshold_test: threshold_test.c threshold.c threshold.h keylist.c keylist.h
	gcc -lpthread $(CFLAGS) threshold_test.c threshold.c keylist.c -o threshold_test

subst_test: subst_test.c pcre_subst.c pcre_subst.h
	gcc $(CFLAGS) subst_test.c pcre_subst.c -lpcre -o subst_test
