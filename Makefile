CFLAGS = -pipe -O3
FLEXFLAGS =
BISONFLAGS =

PROGS = reactd
TESTS = test_expire_list test_ring

all: $(PROGS)
tests: override CFLAGS += -DDEBUG_EXPIRE_LIST -DDEBUG_RING
tests: $(TESTS)
# all: reactd keylist_test threshold_test subst_test

debug: override CFLAGS += -DDEBUG -ggdb
debug: override FLEXFLAGS += --debug
debug: override BISONFLAGS += --debug
debug: all
	@true

# allow overriding automatic systemd detection by using:
# NOSYSTEMD=1 make
ifeq ($(NOSYSTEMD),)
ifneq (,$(wildcard /usr/lib/libsystemd.so))
  override M4FLAGS += -DSYSTEMD
  override CFLAGS  += -DSYSTEMD
  override LDFLAGS += -lsystemd
endif
endif

ifneq ($(STATIC),)
  override LDFLAGS+=-static
  override PKGCONFIG_ARGS+=--static
endif
# LDFLAGS_pcre = $(shell pcre-config --libs)
LDFLAGS_pcre = $(shell pkg-config --libs $(PKGCONFIG_ARGS) libpcre)

ifeq ($(DEBUG),)
  CFLAGS +=
else
  override CFLAGS += -ggdb -std=gnu99 -DDEBUG -DDEBUG_EXPIRE_LIST -DDEBUG_RING -DDEBUG_HASH -DDEBUG_BTREE -DDEBUG_PCRE_SUBST
  override M4FLAGS += -DDEBUG
endif

objects_reactd = reactd_conf.tab.o reactd_conf.lex.yy.o reactd.o avl.o pcre_subst.o debug.o log.o ring.o expire_list.o
objects_subst_test = subst_test.o pcre_subst.o debug.o
objects_expire_list_test = test_expire_list Test_expire_list.o expire_list_test.o expire_list.o Test_expire_list.c
objects_ring_test = test_ring ring_test.o Test_ring.o ring.o Test_ring.c

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $*.c -o $*.o

# process *.l.m4 files with m4 before flex
%.l: %.l.m4
	m4 $(M4FLAGS) $< > $@

# process *.y.m4 files with m4 before bison
%.y: %.y.m4
	m4 $(M4FLAGS) $< > $@

reactd_conf.tab.c reactd_conf.tab.h: reactd_conf.y
	bison $(BISONFLAGS) -d $<

reactd_conf.lex.yy.c: reactd_conf.l reactd_conf.tab.h
	flex $(FLEXFLAGS) -o $@ $<

reactd: $(objects_reactd)
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -lfl $(LDFLAGS_pcre) -lm -o $@

subst_test: $(objects_subst_test)
	$(CC) $(CFLAGS) $(objects_subst_test) -lpcre -o subst_test

clean:
	rm -f vgcore.* reactd reactd_conf.[ly] reactd_conf.tab.c reactd_conf.tab.h reactd_conf.lex.yy.c $(objects_reactd) keylist_test $(objects_keylist_test) threshold_test $(objects_threshold_test) subst_test $(objects_subst_test) $(objects_expire_list_test) $(objects_ring_test) $(objects_btree_test)

# Unit Tests
Test_expire_list.c: expire_list_test.c
	./make-tests.sh $< > $@

test_expire_list: CuTest.c Test_expire_list.o expire_list_test.o expire_list.o
	$(CC) $(CFLAGS) -o $@ $^

Test_ring.c: ring_test.c
	./make-tests.sh $< > $@

test_ring: CuTest.c Test_ring.o ring_test.o ring.o
	$(CC) $(CFLAGS) -o $@ $^
