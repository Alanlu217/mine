CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra

SDL_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null)

minesweeper: main.c solver.c solver.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) main.c solver.c -o minesweeper $(SDL_LIBS) -lm

test: solver.c test_solver.c solver.h
	$(CC) $(CFLAGS) solver.c test_solver.c -o test_solver -lm
	./test_solver

install: minesweeper
	cp minesweeper ~/dotfiles/bin/minesweeper

clean:
	rm -f minesweeper test_solver

.PHONY: test clean
