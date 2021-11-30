CFLAGS = -I .
LDLIBS = -lcurses -lreadline

.PHONY: all main

all: main

clean:
	rm main

main: main.c
	$(CC) -o $@ $< $(CFLAGS) $(LDLIBS)
