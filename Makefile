CFLAGS = -g -O3 -flto -fvisibility=hidden -Wall -Wextra
ppmd-mini: ppmd-mini.c lib/Ppmd*.c
	$(CC) -Ilib $(CFLAGS) -o $@ $^
