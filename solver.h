/* Logic solver and solvable-board generator. Plain C, no dependencies.
 *
 * A board is "solvable" if, after the first click, every safe cell can be
 * revealed by deduction alone: single-cell rules, subset rules between
 * overlapping numbers, and the global mine count. generate() keeps trying
 * random layouts until one passes, so the player never has to guess.
 */
#ifndef SOLVER_H
#define SOLVER_H

#include <stdint.h>

#define MAX_CELLS 512 /* enough for 30x16 (480) */

typedef unsigned char u8;

typedef struct {
  uint64_t s;
} Rng;

typedef struct {
  int rows, cols, n;
  int nbn[MAX_CELLS];   /* neighbour count per cell */
  int nb[MAX_CELLS][8]; /* neighbour indices, ascending */
} Topo;

void rng_seed(Rng *r, uint64_t seed);
uint64_t rng_next(Rng *r);
int rng_below(Rng *r, int n);

/* Cell index = row * cols + col. */
void topo_init(Topo *t, int rows, int cols);

/* num[i] = number of mines adjacent to cell i. */
void count_numbers(const Topo *t, const u8 *mines, u8 *num);

/* Reveal `start` and flood through zero cells. Cells with blocked[i] set
 * (flags) are skipped; blocked may be NULL. Returns how many cells were newly
 * revealed. */
int flood_reveal(const Topo *t, const u8 *mines, const u8 *num, u8 *revealed,
                 u8 *blocked, int start);

/* 1 if pure logic, starting with a click on `start`, clears the whole board.
 * With check != 0, aborts if any deduction contradicts the real layout. */
int is_solvable(const Topo *t, const u8 *mines, int start, int check);

/* Fills mines_out with a random layout that is solvable from a first click on
 * `start`. The clicked cell and its neighbours are mine-free, so the first
 * click always opens an area. Returns the number of layouts tried (-1 if the
 * mine count cannot fit). */
int generate(const Topo *t, int mine_count, int start, Rng *rng, u8 *mines_out);

#endif
