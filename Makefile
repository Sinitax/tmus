CC = clang
CFLAGS = -I src -g
LDLIBS = -lcurses -lreadline -lmpdclient
WARNFLAGS = -Wno-pragma-once-outside-header

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:src/%.c=build/%.o)
DEPS = $(OBJS:%.o=%.d)

.PHONY: all tmus clean install uninstall

all: tmus

clean:
	rm tmus

build:
	mkdir build

build/%.o: src/%.c build/%.d
	@echo DEPS: $^
	$(CC) -c -o $@ $< $(CFLAGS)

build/main.d: src/main.c | build
	$(CC) -c -MT build/main.o -MMD -MP -MF $@ $<

build/%.d: src/%.c src/%.h | build
	$(CC) -c -MT build/$*.o -MMD -MP -MF $@ $^ $(WARNFLAGS)

-include $(DEPS)

tmus: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)

install:
	install -m 755 tmus /usr/bin

uninstall:
	rm /usr/bin/tmus

