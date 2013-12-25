# $Id$

CFLAGS = -ggdb -pipe

all: reactd

reactd_conf.tab.c reactd.tab.h: reactd_conf.y
	bison -d reactd_conf.y

reactd_conf.lex.yy.c: reactd_conf.l reactd.tab.h
	flex -o reactd_conf.lex.yy.c reactd_conf.l

reactd: reactd_conf.lex.yy.c reactd_conf.tab.c reactd_conf.tab.h reactd.c
	gcc $(CFLAGS) reactd_conf.tab.c reactd_conf.lex.yy.c reactd.c -lfl -o reactd

clean:
	rm -f reactd_conf.tab.c reactd_conf.tab.h reactd_conf.lex.yy.c reactd
