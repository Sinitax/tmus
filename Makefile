CFLAGS = -I src -g $(shell pkg-config --cflags glib-2.0 dbus-1)
CFLAGS += -I lib/liblist/include -Wunused-variable -Wmissing-prototypes
LDLIBS = -lcurses -lmpdclient $(shell pkg-config --libs glib-2.0 dbus-1)
DEPFLAGS = -MT $@ -MMD -MP -MF build/$*.d

ifeq "$(PROF)" "YES"
	CFLAGS += -pg
endif

BACKEND ?= mpd

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:src/%.c=build/%.o) build/player_$(BACKEND).o
DEPS = $(OBJS:%.o=%.d)

LIBLIST_A = lib/liblist/build/liblist.a

.PHONY: all tmus clean cleanlibs install uninstall

all: tmus

clean:
	rm -rf build

cleanlibs:
	rm -rf lib/liblist/build

build:
	mkdir build

build/%.o: src/%.c build/%.d | build
	$(CC) -c -o $@ $(DEPFLAGS) $(CFLAGS) $<

build/%.d: | build;

include $(DEPS)

$(LIBLIST_A):
	make -C lib/liblist build/liblist.a

tmus: $(OBJS) $(LIBLIST_A)
	$(CC) -o tmus $^ $(CFLAGS) $(LDLIBS)

install:
	install -m 755 tmus /usr/bin

uninstall:
	rm -f /usr/bin/tmus

