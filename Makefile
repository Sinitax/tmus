CFLAGS = -I .
LDLIBS = -lcurses -lreadline

.PHONY: all main

all: main

clean:
	rm main

%.o: %.c %.h
	$(CC) -c -o $@ $< $(CFLAGS) $(LDLIBS)

main: main.c util.o history.o link.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)
