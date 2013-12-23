# $Id$

CFLAGS = -ggdb -pipe

all: reactd

reactd.tab.c reactd.tab.h: reactd.y
	bison -d reactd.y
#	bison --verbose --debug -d reactd.y

reactd.lex.yy.c: reactd.l reactd.tab.h
	flex -o reactd.lex.yy.c reactd.l

reactd: reactd.lex.yy.c reactd.tab.c reactd.tab.h
	gcc $(CFLAGS) reactd.tab.c reactd.lex.yy.c -lfl -o reactd

clean:
	rm -f reactd.tab.c reactd.tab.h reactd.lex.yy.c reactd
