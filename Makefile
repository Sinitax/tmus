CFLAGS = -O2 -I src -g $(shell pkg-config --cflags glib-2.0 dbus-1)
LDLIBS = -lcurses -lmpdclient $(shell pkg-config --libs glib-2.0 dbus-1)
DEPFLAGS = -MT $@ -MMD -MP -MF build/$*.d

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:src/%.c=build/%.o)
DEPS = $(OBJS:%.o=%.d)

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

tmus: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)

tmus.prof: $(OBJS)
	$(CC) -o $@ $^ -pg $(CFLAGS) $(LDLIBS)

install:
	install -m 755 tmus /usr/bin

uninstall:
	rm /usr/bin/tmus

