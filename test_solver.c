/* Tests for the solver/generator. Build and run: make test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "solver.h"

typedef struct {
    const char *name;
    int cols, rows, mines;
} Preset;

static const Preset PRESETS[3] = {{"Easy", 9, 9, 10}, {"Medium", 16, 16, 40}, {"Expert", 30, 16, 99}};

#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAILED: %s (line %d)\n", #cond, __LINE__);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

int main(void) {
    Rng rng;
    rng_seed(&rng, 12345);

    for (int p = 0; p < 3; p++) {
        Topo t;
        topo_init(&t, PRESETS[p].rows, PRESETS[p].cols);
        const int boards = 30;
        int worst = 0;
        clock_t t0 = clock();
        for (int b = 0; b < boards; b++) {
            u8 mines[MAX_CELLS];
            int start = rng_below(&rng, t.n);
            int attempts = generate(&t, PRESETS[p].mines, start, &rng, mines);
            REQUIRE(attempts > 0);
            if (attempts > worst) worst = attempts;

            int count = 0;
            for (int i = 0; i < t.n; i++) count += mines[i] ? 1 : 0;
            REQUIRE(count == PRESETS[p].mines);
            REQUIRE(!mines[start]);
            for (int k = 0; k < t.nbn[start]; k++) REQUIRE(!mines[t.nb[start][k]]);
            REQUIRE(is_solvable(&t, mines, start, 1)); /* check=1 verifies every deduction */
        }
        double secs = (double)(clock() - t0) / CLOCKS_PER_SEC / boards;
        printf("%-7s %d boards OK, avg %.3fs each, worst %d attempts\n", PRESETS[p].name, boards,
               secs, worst);
    }

    /* Dense random 9x9 boards often need a guess; the solver must say so
     * and must never contradict the real layout while trying. */
    {
        Topo t;
        topo_init(&t, 9, 9);
        int ok = 0, bad = 0;
        for (int n = 0; n < 300; n++) {
            u8 mines[MAX_CELLS];
            memset(mines, 0, sizeof mines);
            int placed = 0;
            while (placed < 22) {
                int i = rng_below(&rng, 81);
                int near_start = (i == 40);
                for (int k = 0; k < t.nbn[40]; k++) near_start |= (t.nb[40][k] == i);
                if (!mines[i] && !near_start) {
                    mines[i] = 1;
                    placed++;
                }
            }
            if (is_solvable(&t, mines, 40, 1)) ok++;
            else bad++;
        }
        printf("dense 9x9: %d solvable, %d rejected\n", ok, bad);
        REQUIRE(bad > 0);
    }

    puts("all tests passed");
    return 0;
}
