CFLAGS = -I src -g
LDLIBS = -lcurses -lreadline -lmpdclient
DEPFLAGS =  -MT $@ -MMD -MP -MF build/$*.d

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

install:
	install -m 755 tmus /usr/bin

uninstall:
	rm /usr/bin/tmus

