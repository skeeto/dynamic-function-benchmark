CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -O3 -g3 -fpie
LDFLAGS = -pie
LDLIBS  = ./empty.so -ldl

benchmark: benchmark.c empty.so
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ benchmark.c $(LDLIBS)

empty.so: empty.c
	$(CC) -shared -fPIC -Os -s -o $@ empty.c

clean:
	rm -f benchmark empty.so
