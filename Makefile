CFLAGS = -I src -g $(shell pkg-config --cflags glib-2.0 dbus-1)
CFLAGS += -I lib/clist/include
LDLIBS = -lcurses -lmpdclient $(shell pkg-config --libs glib-2.0 dbus-1)
DEPFLAGS = -MT $@ -MMD -MP -MF build/$*.d

ifeq "$(PROF)" "YES"
	CFLAGS += -pg
endif

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:src/%.c=build/%.o)
DEPS = $(OBJS:%.o=%.d)

LIBLIST_A = lib/clist/build/liblist.a

.PHONY: all tmus clean install uninstall

all: tmus

clean:
	rm -rf build

build:
	mkdir build

build/%.o: src/%.c build/%.d
	$(CC) -c -o $@ $(DEPFLAGS) $(CFLAGS) $<

build/%.d: | build;

include $(DEPS)

$(LIBLIST_A):
	make -C lib/clist build/liblist.a

tmus: $(OBJS) $(LIBLIST_A)
	$(CC) -o tmus $^ $(CFLAGS) $(LDLIBS)

install:
	install -m 755 tmus /usr/bin

uninstall:
	rm /usr/bin/tmus

