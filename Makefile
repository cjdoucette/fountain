CC = gcc
CFLAGS = -D_GNU_SOURCE -Wall -Wextra -g -MMD -I ../xiaconf/kernel-include \
-I ../xiaconf/include
LDFLAGS = -g -L ../xiaconf/libxia -lxia

all: spray drink gf-complete jerasure

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

LIBS = gf-complete jerasure

-include *.d

.PHONY: install clean cscope gf-complete jerasure

install: $(TARGETS)
	echo 'IMPORTANT: make sure that libxia is installed!'
	install -o root -g root -m 711 $(TARGETS) /bin

clean:
	rm -f *.o *.d cscope.out spray drink

cscope:
	cscope -b *.c *.h
