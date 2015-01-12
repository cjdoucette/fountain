CC = gcc
CFLAGS = -D_GNU_SOURCE -Wall -Wextra -g -MMD -I ../xiaconf/kernel-include \
-I ../xiaconf/include
LDFLAGS = -g -L ../xiaconf/libxia -lxia -lJerasure -lgf_complete

all: encoder decoder spray drink

spray: spray.o fountain.o
	$(CC) -o $@ $^ $(LDFLAGS)

drink: drink.o fountain.o
	$(CC) -o $@ $^ $(LDFLAGS)

encoder: encoder.o timing.o
	$(CC) -o $@ $^ $(LDFLAGS)

decoder: decoder.o timing.o
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: install clean cscope encoder decoder

clean:
	rm -f *.o *.d cscope.out spray drink encoder decoder

cscope:
	cscope -b *.c *.h
