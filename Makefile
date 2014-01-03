# $Id$

CFLAGS = -pipe

all: reactd

debug: CFLAGS += -DDEBUG -ggdb
debug: all

reactd_conf.tab.c reactd_conf.tab.h: reactd_conf.y
	bison -d reactd_conf.y

reactd_conf.lex.yy.c: reactd_conf.l reactd_conf.tab.h
	flex -o reactd_conf.lex.yy.c reactd_conf.l

reactd: reactd_conf.lex.yy.c reactd_conf.tab.c reactd_conf.tab.h reactd.c
	gcc $(CFLAGS) reactd_conf.tab.c reactd_conf.lex.yy.c reactd.c -lfl -o reactd

clean:
	rm -f reactd_conf.tab.c reactd_conf.tab.h reactd_conf.lex.yy.c reactd
