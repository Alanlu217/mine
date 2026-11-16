#include "solver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rng_seed(Rng *r, uint64_t seed) { r->s = seed ? seed : 1; }

uint64_t rng_next(Rng *r) { /* xorshift64* */
  uint64_t x = r->s;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  r->s = x;
  return x * 0x2545F4914F6CDD1DULL;
}

int rng_below(Rng *r, int n) { return (int)(rng_next(r) % (uint64_t)n); }

void topo_init(Topo *t, int rows, int cols) {
  t->rows = rows;
  t->cols = cols;
  t->n = rows * cols;
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      int i = r * cols + c, k = 0;
      for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
          int rr = r + dr, cc = c + dc;
          if ((dr || dc) && rr >= 0 && rr < rows && cc >= 0 && cc < cols)
            t->nb[i][k++] = rr * cols + cc;
        }
      }
      t->nbn[i] = k;
    }
  }
}

void count_numbers(const Topo *t, const u8 *mines, u8 *num) {
  for (int i = 0; i < t->n; i++) {
    int c = 0;
    for (int k = 0; k < t->nbn[i]; k++)
      c += mines[t->nb[i][k]] ? 1 : 0;
    num[i] = (u8)c;
  }
}

int flood_reveal(const Topo *t, const u8 *mines, const u8 *num, u8 *revealed,
                 const u8 *blocked, int start) {
  int stack[MAX_CELLS], sp = 0, added = 0;
  if (revealed[start] || mines[start] || (blocked && blocked[start]))
    return 0;
  revealed[start] = 1;
  added++;
  if (num[start] == 0)
    stack[sp++] = start;
  while (sp) {
    int x = stack[--sp];
    for (int k = 0; k < t->nbn[x]; k++) {
      int y = t->nb[x][k];
      if (revealed[y] || mines[y] || (blocked && blocked[y]))
        continue;
      revealed[y] = 1;
      added++;
      if (num[y] == 0)
        stack[sp++] = y;
    }
  }
  return added;
}

/* One constraint: exactly `rem` mines among the `len` unknown cells in `cell`.
 */
typedef struct {
  int len;
  int cell[8]; /* ascending */
  int rem;
  int src; /* the revealed cell this came from */
} Con;

static int is_subset(const Con *a, const Con *b) {
  int i = 0, j = 0;
  while (i < a->len) {
    while (j < b->len && b->cell[j] < a->cell[i])
      j++;
    if (j >= b->len || b->cell[j] != a->cell[i])
      return 0;
    i++;
    j++;
  }
  return 1;
}

/* big minus small holds (big.rem - small.rem) mines: decide if that settles it.
 */
static int mark_diff(const Con *big, const Con *small, u8 *safe, u8 *mine) {
  int d[8], nd = 0, j = 0, rd = big->rem - small->rem;
  for (int i = 0; i < big->len; i++) {
    int x = big->cell[i];
    while (j < small->len && small->cell[j] < x)
      j++;
    if (j < small->len && small->cell[j] == x)
      continue;
    d[nd++] = x;
  }
  if (rd == 0) {
    for (int i = 0; i < nd; i++)
      safe[d[i]] = 1;
    return nd > 0;
  }
  if (rd == nd) {
    for (int i = 0; i < nd; i++)
      mine[d[i]] = 1;
    return nd > 0;
  }
  return 0;
}

int is_solvable(const Topo *t, const u8 *mines, int start, int check) {
  const int total = t->n;
  u8 num[MAX_CELLS], revealed[MAX_CELLS], known[MAX_CELLS];
  u8 safe[MAX_CELLS], mine[MAX_CELLS];
  Con cons[MAX_CELLS];

  int mine_total = 0;
  for (int i = 0; i < total; i++)
    mine_total += mines[i] ? 1 : 0;
  const int safe_total = total - mine_total;

  count_numbers(t, mines, num);
  memset(revealed, 0, sizeof revealed);
  memset(known, 0, sizeof known);
  int rev_count = flood_reveal(t, mines, num, revealed, NULL, start);
  int known_count = 0;

  while (rev_count < safe_total) {
    /* Build constraints from revealed numbers that still touch unknown cells.
     */
    int ncons = 0;
    for (int cell = 0; cell < total; cell++) {
      if (!revealed[cell] || num[cell] == 0)
        continue;
      Con *c = &cons[ncons];
      c->len = 0;
      int known_n = 0;
      for (int k = 0; k < t->nbn[cell]; k++) {
        int x = t->nb[cell][k];
        if (known[x])
          known_n++;
        else if (!revealed[x])
          c->cell[c->len++] = x;
      }
      if (c->len == 0)
        continue;
      c->rem = num[cell] - known_n;
      c->src = cell;
      ncons++;
    }

    memset(safe, 0, sizeof safe);
    memset(mine, 0, sizeof mine);
    int any = 0;
    for (int i = 0; i < ncons; i++) {
      const Con *a = &cons[i];
      if (a->rem == 0) {
        for (int k = 0; k < a->len; k++)
          safe[a->cell[k]] = 1;
        any = 1;
      } else if (a->rem == a->len) {
        for (int k = 0; k < a->len; k++)
          mine[a->cell[k]] = 1;
        any = 1;
      }
      /* Subset rule against nearby constraints (they must be within 2 cells).
       */
      int ar = a->src / t->cols, ac = a->src % t->cols;
      for (int j = 0; j < ncons; j++) {
        const Con *b = &cons[j];
        if (b->len >= a->len)
          continue;
        int br = b->src / t->cols, bc = b->src % t->cols;
        if (abs(ar - br) > 2 || abs(ac - bc) > 2)
          continue;
        if (!is_subset(b, a))
          continue;
        if (mark_diff(a, b, safe, mine))
          any = 1;
      }
    }

    if (!any) { /* global mine-count rule */
      int unk = 0;
      for (int i = 0; i < total; i++)
        unk += (!revealed[i] && !known[i]) ? 1 : 0;
      int left = mine_total - known_count;
      if (left == 0) {
        for (int i = 0; i < total; i++)
          if (!revealed[i] && !known[i])
            safe[i] = 1;
      } else if (left == unk) {
        for (int i = 0; i < total; i++)
          if (!revealed[i] && !known[i])
            mine[i] = 1;
      } else {
        return 0; /* stuck: would need a guess */
      }
    }

    if (check) {
      for (int i = 0; i < total; i++) {
        if (safe[i] && mines[i]) {
          fprintf(stderr, "solver marked a mine as safe (cell %d)\n", i);
          abort();
        }
        if (mine[i] && !mines[i]) {
          fprintf(stderr, "solver marked a safe cell as a mine (cell %d)\n", i);
          abort();
        }
      }
    }
    for (int i = 0; i < total; i++) {
      if (mine[i] && !known[i]) {
        known[i] = 1;
        known_count++;
      }
    }
    for (int i = 0; i < total; i++)
      if (safe[i])
        rev_count += flood_reveal(t, mines, num, revealed, NULL, i);
  }
  return 1;
}

int generate(const Topo *t, int mine_count, int start, Rng *rng,
             u8 *mines_out) {
  u8 prot[MAX_CELLS];
  int pool[MAX_CELLS], np = 0;
  memset(prot, 0, sizeof prot);
  prot[start] = 1;
  for (int k = 0; k < t->nbn[start]; k++)
    prot[t->nb[start][k]] = 1;
  for (int i = 0; i < t->n; i++)
    if (!prot[i])
      pool[np++] = i;
  if (mine_count > np)
    return -1;

  int attempts = 0;
  for (;;) {
    int p[MAX_CELLS];
    attempts++;
    memcpy(p, pool, sizeof(int) * (size_t)np);
    memset(mines_out, 0, (size_t)t->n);
    for (int i = 0; i < mine_count; i++) {
      int j = i + rng_below(rng, np - i);
      int tmp = p[i];
      p[i] = p[j];
      p[j] = tmp;
      mines_out[p[i]] = 1;
    }
    if (is_solvable(t, mines_out, start, 0))
      return attempts;
  }
}
