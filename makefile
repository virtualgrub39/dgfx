dgfx: dgfx.c
	$(CC) dgfx.c -o dgfx $(shell pkg-config --cflags --libs luajit) -lm -O3 -lpthread
