dgfx: dgfx.c
	$(CC) dgfx.c -o dgfx $(shell pkg-config --cflags --libs luajit sdl3 sdl3-ttf) -lm -O3 -lpthread
