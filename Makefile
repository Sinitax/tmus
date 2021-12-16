CFLAGS = -I . -g
LDLIBS = -lcurses -lreadline -lmpdclient

.PHONY: all tmus clean install uninstall

all: tmus

clean:
	rm tmus

%.o: %.c %.h
	$(CC) -c -o $@ $< $(CFLAGS) $(LDLIBS)

tmus: main.c util.o history.o link.o player.o tag.o track.o listnav.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)

install:
	install -m 755 tmus /usr/bin

uninstall:
	rm /usr/bin/tmus
