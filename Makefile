CC = gcc
CFLAGS = -D_GNU_SOURCE -Wall -Wextra -g -MMD -I ../xiaconf/kernel-include \
-I ../xiaconf/include
LDFLAGS = -g -L ../xiaconf/libxia -lxia

all: spray drink

install: gf-complete jerasure ldconfig

spray: spray.o fountain.o
	$(CC) -o $@ $^ $(LDFLAGS)

drink: drink.o fountain.o
	$(CC) -o $@ $^ $(LDFLAGS)

gf-complete:
	cd gf-complete; \
	./autogen.sh; \
	./configure; \
	make; \
	sudo make install; \
	cd ..

jerasure:
	cd jerasure; \
	./configure; \
	make; \
	sudo make install; \
	cd ..

ldconfig:
	sudo ldconfig

-include *.d

.PHONY: install clean cscope gf-complete jerasure

clean:
	rm -f *.o *.d cscope.out spray drink

cscope:
	cscope -b *.c *.h
