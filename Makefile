CFLAGS = -pipe -ggdb

PROGS = reactd
TESTS = test_reset_list

all: $(PROGS)
tests: $(TESTS)
# all: reactd keylist_test threshold_test subst_test

debug: CFLAGS += -DDEBUG -ggdb
debug: all
	@true

ifeq ($(DEBUG),)
  CFLAGS += -ggdb -O3 -std=gnu99
else
  CFLAGS += -ggdb -std=gnu99 -DDEBUG -DDEBUG_RESET_LIST
endif

objects_reactd = reactd_conf.tab.o reactd_conf.lex.yy.o reactd.o keylist.o pcre_subst.o debug.o log.o ring.o reset_list.o
objects_keylist_test = keylist_test.o keylist.o debug.o
objects_threshold_test = threshold_test.o threshold.o keylist.o debug.o
objects_subst_test = subst_test.o pcre_subst.o debug.o

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $*.c -o $*.o

reactd_conf.tab.c reactd_conf.tab.h: reactd_conf.y
	bison -d reactd_conf.y

reactd_conf.lex.yy.c: reactd_conf.l reactd_conf.tab.h
	flex -o reactd_conf.lex.yy.c reactd_conf.l

reactd: $(objects_reactd)
	$(CC) $(CFLAGS) $(objects_reactd) -lfl -lpcre -lm -o reactd

keylist_test: $(objects_keylist_test)
	$(CC) $(CFLAGS) $(objects_keylist_test) -o keylist_test
	
threshold_test: $(objects_threshold_test)
	$(CC) $(CFLAGS) $(objects_threshold_test) -o threshold_test -lpthread

subst_test: $(objects_subst_test)
	$(CC) $(CFLAGS) $(objects_subst_test) -lpcre -o subst_test

clean:
	rm -f reactd reactd_conf.tab.c reactd_conf.tab.h reactd_conf.lex.yy.c $(objects_reactd) keylist_test $(objects_keylist_test) threshold_test $(objects_threshold_test) subst_test $(objects_subst_test) test_reset_list Test_reset_list.o Test_reset_list.c

# Unit Tests
Test_reset_list.c: reset_list_test.c
	./make-tests.sh $< > $@

test_reset_list: CuTest.c Test_reset_list.o reset_list_test.o reset_list.o
	$(CC) $(CFLAGS) -o $@ $^
