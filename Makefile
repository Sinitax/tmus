CFLAGS = -I . -g
LDLIBS = -lcurses -lreadline -lportaudio -lsndfile

.PHONY: all main

all: main

clean:
	rm main

%.o: %.c %.h
	$(CC) -c -o $@ $< $(CFLAGS) $(LDLIBS)

main: main.c util.o history.o link.o player.o tag.o track.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)
