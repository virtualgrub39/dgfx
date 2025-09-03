all: dgfx

DGFX_SRC = $(wildcard *.c)
DGFX_OBJ = $(DGFX_SRC:.c=.o)

DGFX_LIBS = $(shell pkg-config --libs luajit sdl3 sdl3-ttf) -lm -lpthread
DGFX_INCS = $(shell pkg-config --cflags luajit sdl3 sdl3-ttf) -Iextern

DGFX_LDFLAGS = $(DGFX_LIBS)
DGFX_CFLAGS  = $(DGFX_INCS) -std=c99 -Wall -Werror -Wextra -O2 -ggdb

dgfx.o: config.h extern/stb_image_write.h extern/ketopt.h extern/stb_ds.h

%.o: %.c
	$(CC) $(DGFX_CFLAGS) -c $< -o $@

dgfx: $(DGFX_OBJ)
	$(CC) $(DGFX_OBJ) $(DGFX_LDFLAGS) -o $@

config.h: config.def.h
	cp config.def.h config.h

clean:
	rm -rf $(DGFX_OBJ) dgfx

.PHONY: clean
