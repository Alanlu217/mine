#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_main.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "solver.h"

#define CELL 32
#define HEADER_H 90
#define PAD 10
#define MIN_W 376

typedef struct {
  const char *name;
  int cols, rows, mines;
} Preset;

static const Preset PRESETS[3] = {
    {"Easy", 9, 9, 10}, {"Medium", 16, 16, 40}, {"Expert", 30, 16, 99}};

typedef struct {
  Uint8 r, g, b;
} Color;

static const Color C_BG = {32, 37, 46};
static const Color C_TEXT = {235, 238, 243};
static const Color C_BTN = {58, 67, 80};
static const Color C_BTN_ON = {79, 124, 255};
static const Color C_HIDDEN = {184, 194, 207};
static const Color C_HIDDEN_HI = {227, 232, 238};
static const Color C_HIDDEN_LO = {123, 135, 148};
static const Color C_OPEN = {232, 232, 232};
static const Color C_OPEN_EDGE = {169, 178, 189};
static const Color C_BOOM = {224, 64, 64};
static const Color C_BLACK = {20, 20, 20};
static const Color C_FLAG = {211, 47, 47};
static const Color NUM_COLOR[9] = {
    {0, 0, 0},     {31, 79, 216}, {27, 138, 46}, {211, 47, 47},  {26, 35, 126},
    {142, 27, 27}, {0, 131, 143}, {34, 34, 34},  {102, 102, 102}};

typedef struct {
  int diff, rows, cols, mine_count;
  Topo topo;
  u8 mines[MAX_CELLS], num[MAX_CELLS], revealed[MAX_CELLS], flags[MAX_CELLS];
  int generated, over, won, boom, rev_count, flag_count;
  Uint64 start_ms;
  int final_secs;
  Rng rng;
} Game;

static int win_w(const Game *g) {
  return g->cols * CELL > MIN_W ? g->cols * CELL : MIN_W;
}
static int win_h(const Game *g) { return HEADER_H + g->rows * CELL + PAD; }
static int board_x(const Game *g) { return (win_w(g) - g->cols * CELL) / 2; }

/* ---------- game logic ---------- */

static void new_game(Game *g, int diff, SDL_Window *win, SDL_Renderer *ren) {
  g->diff = diff;
  g->cols = PRESETS[diff].cols;
  g->rows = PRESETS[diff].rows;
  g->mine_count = PRESETS[diff].mines;
  topo_init(&g->topo, g->rows, g->cols);
  memset(g->mines, 0, sizeof g->mines);
  memset(g->num, 0, sizeof g->num);
  memset(g->revealed, 0, sizeof g->revealed);
  memset(g->flags, 0, sizeof g->flags);
  g->generated = g->over = g->won = g->rev_count = g->flag_count = 0;
  g->boom = -1;
  g->start_ms = 0;
  g->final_secs = 0;
  if (win && ren) {
    int need_w = win_w(g), need_h = win_h(g);
    int cw, ch;
    SDL_GetWindowSize(win, &cw, &ch);
    if (cw < need_w || ch < need_h)
      SDL_SetWindowSize(win, cw < need_w ? need_w : cw,
                        ch < need_h ? need_h : ch);
    SDL_SetRenderLogicalPresentation(ren, need_w, need_h,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetWindowMinimumSize(win, need_w / 2, need_h / 2);
  }
}

static int elapsed_secs(const Game *g) {
  if (g->over)
    return g->final_secs;
  if (!g->generated)
    return 0;
  Uint64 s = (SDL_GetTicks() - g->start_ms) / 1000;
  return s > 999 ? 999 : (int)s;
}

static void reveal(Game *g, int i) {
  if (g->revealed[i] || g->flags[i])
    return;
  if (g->mines[i]) {
    g->boom = i;
    g->over = 1;
    return;
  }
  g->rev_count +=
      flood_reveal(&g->topo, g->mines, g->num, g->revealed, g->flags, i);
}

static void chord(Game *g, int i) {
  int n = g->num[i];
  if (n == 0)
    return;
  int f = 0;
  for (int k = 0; k < g->topo.nbn[i]; k++)
    f += g->flags[g->topo.nb[i][k]] ? 1 : 0;
  if (f != n)
    return;
  for (int k = 0; k < g->topo.nbn[i]; k++) {
    int x = g->topo.nb[i][k];
    if (!g->flags[x] && !g->revealed[x]) {
      reveal(g, x);
      if (g->over)
        return;
    }
  }
}

static void finish_check(Game *g) {
  if (!g->over && g->rev_count == g->topo.n - g->mine_count) {
    g->over = g->won = 1;
    for (int i = 0; i < g->topo.n; i++)
      g->flags[i] = g->mines[i];
    g->flag_count = g->mine_count;
  }
  if (g->over && g->final_secs == 0 && g->generated) {
    Uint64 s = (SDL_GetTicks() - g->start_ms) / 1000;
    g->final_secs = s > 999 ? 999 : (int)s;
  }
}

static void left_click(Game *g, int i) {
  if (g->over)
    return;
  if (g->revealed[i]) {
    chord(g, i);
  } else if (!g->flags[i]) {
    if (!g->generated) {
      generate(&g->topo, g->mine_count, i, &g->rng, g->mines);
      count_numbers(&g->topo, g->mines, g->num);
      g->generated = 1;
      g->start_ms = SDL_GetTicks();
    }
    reveal(g, i);
  }
  finish_check(g);
}

static void flag_click(Game *g, int i) {
  if (g->over || g->revealed[i])
    return;
  g->flags[i] ^= 1;
  g->flag_count += g->flags[i] ? 1 : -1;
}

/* ---------- drawing ---------- */

static void set_color(SDL_Renderer *r, Color c) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
}

static void fill_rect(SDL_Renderer *r, float x, float y, float w, float h,
                      Color c) {
  SDL_FRect rect = {x, y, w, h};
  set_color(r, c);
  SDL_RenderFillRect(r, &rect);
}

/* Text with SDL's 8x8 debug font, drawn at an integer scale. (x, y) is
 * top-left. */
static void draw_text(SDL_Renderer *r, float x, float y, int scale,
                      const char *s, Color c) {
  set_color(r, c);
  SDL_SetRenderScale(r, (float)scale, (float)scale);
  SDL_RenderDebugText(r, x / scale, y / scale, s);
  SDL_SetRenderScale(r, 1.0f, 1.0f);
}

static void draw_text_centered(SDL_Renderer *r, float cx, float y, int scale,
                               const char *s, Color c) {
  draw_text(r, cx - (float)strlen(s) * 8.0f * scale / 2.0f, y, scale, s, c);
}

static void draw_disc(SDL_Renderer *r, float cx, float cy, float rad, Color c) {
  set_color(r, c);
  for (int dy = (int)-rad; dy <= (int)rad; dy++) {
    float dx = sqrtf(rad * rad - (float)(dy * dy));
    SDL_RenderLine(r, cx - dx, cy + (float)dy, cx + dx, cy + (float)dy);
  }
}

static void draw_flag(SDL_Renderer *r, float x0, float y0) {
  for (int k = 0; k < 10; k++) {
    int w = (k <= 4 ? k + 1 : 10 - k) * 2;
    fill_rect(r, x0 + 14, y0 + 6 + (float)k, (float)w, 1, C_FLAG);
  }
  fill_rect(r, x0 + 13, y0 + 6, 2, 19, C_BLACK);
  fill_rect(r, x0 + 8, y0 + 24, 14, 3, C_BLACK);
}

static void draw_cell(SDL_Renderer *r, const Game *g, int i, float x0,
                      float y0) {
  int is_mine = g->generated && g->mines[i];
  int show_mine = g->over && !g->won && is_mine && !g->flags[i];
  if (g->revealed[i] || show_mine) {
    fill_rect(r, x0, y0, CELL, CELL, C_OPEN_EDGE);
    fill_rect(r, x0 + 1, y0 + 1, CELL - 2, CELL - 2,
              i == g->boom ? C_BOOM : C_OPEN);
    if (is_mine) {
      draw_disc(r, x0 + CELL / 2.0f, y0 + CELL / 2.0f, 8.0f, C_BLACK);
    } else if (g->num[i] > 0) {
      char s[2] = {(char)('0' + g->num[i]), 0};
      draw_text(r, x0 + 8, y0 + 8, 2, s, NUM_COLOR[g->num[i]]);
    }
    return;
  }
  fill_rect(r, x0, y0, CELL, CELL, C_HIDDEN_LO);
  fill_rect(r, x0, y0, CELL - 1, CELL - 1, C_HIDDEN_HI);
  fill_rect(r, x0 + 2, y0 + 2, CELL - 4, CELL - 4, C_HIDDEN);
  if (g->flags[i]) {
    draw_flag(r, x0, y0);
    if (g->over && !g->won && !is_mine) { /* wrong flag */
      set_color(r, C_BLACK);
      SDL_RenderLine(r, x0 + 5, y0 + 5, x0 + CELL - 5, y0 + CELL - 5);
      SDL_RenderLine(r, x0 + CELL - 5, y0 + 5, x0 + 5, y0 + CELL - 5);
    }
  }
}

typedef struct {
  float x, y, w, h;
} Btn;

static Btn diff_button(const Game *g, int k) {
  Btn b = {(float)((win_w(g) - 352) / 2. + k * 120), 8, 112, 30};
  return b;
}

static void draw_game(SDL_Renderer *r, const Game *g) {
  const int W = win_w(g);
  set_color(r, C_BG);
  SDL_RenderClear(r);

  for (int k = 0; k < 3; k++) {
    Btn b = diff_button(g, k);
    fill_rect(r, b.x, b.y, b.w, b.h, k == g->diff ? C_BTN_ON : C_BTN);
    draw_text_centered(r, b.x + b.w / 2, b.y + 7, 2, PRESETS[k].name, C_TEXT);
  }

  char buf[48];
  snprintf(buf, sizeof buf, "Mines %03d", g->mine_count - g->flag_count);
  draw_text(r, 12, 48, 2, buf, C_TEXT);
  snprintf(buf, sizeof buf, "Time %03d", elapsed_secs(g));
  draw_text(r, (float)W - 12 - 9 * 16, 48, 2, buf, C_TEXT);

  const char *status = g->won         ? "Solved! Press R"
                       : g->over      ? "Boom! Press R"
                       : g->generated ? ""
                                      : "Click any cell";
  draw_text_centered(r, W / 2.0f, 68, 2, status,
                     g->won ? (Color){120, 220, 140} : C_TEXT);

  const int bx = board_x(g);
  for (int row = 0; row < g->rows; row++)
    for (int col = 0; col < g->cols; col++)
      draw_cell(r, g, row * g->cols + col, (float)(bx + col * CELL),
                (float)(HEADER_H + row * CELL));

  SDL_RenderPresent(r);
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  static Game g;
  rng_seed(&g.rng, (uint64_t)SDL_GetPerformanceCounter() ^
                       ((uint64_t)time(NULL) * 0x9E3779B97F4A7C15ULL));
  new_game(&g, 0, NULL, NULL);

  SDL_Window *win = NULL;
  SDL_Renderer *ren = NULL;
  if (!SDL_CreateWindowAndRenderer("Minesweeper (always solvable)", win_w(&g),
                                   win_h(&g), SDL_WINDOW_RESIZABLE, &win,
                                   &ren)) {
    fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderVSync(ren, 1);
  SDL_SetRenderLogicalPresentation(ren, win_w(&g), win_h(&g),
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_SetWindowMinimumSize(win, win_w(&g) / 2, win_h(&g) / 2);

  int running = 1;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        running = 0;
      } else if (e.type == SDL_EVENT_KEY_DOWN) {
        switch (e.key.key) {
        case SDLK_ESCAPE:
          running = 0;
          break;
        case SDLK_R:
        case SDLK_F2:
          new_game(&g, g.diff, win, ren);
          break;
        case SDLK_1:
          new_game(&g, 0, win, ren);
          break;
        case SDLK_2:
          new_game(&g, 1, win, ren);
          break;
        case SDLK_3:
          new_game(&g, 2, win, ren);
          break;
        default:
          break;
        }
      } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        SDL_ConvertEventToRenderCoordinates(ren, &e);
        float mx = e.button.x, my = e.button.y;
        int clicked_button = 0;
        for (int k = 0; k < 3; k++) {
          Btn b = diff_button(&g, k);
          if (e.button.button == SDL_BUTTON_LEFT && mx >= b.x &&
              mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
            new_game(&g, k, win, ren);
            clicked_button = 1;
            break;
          }
        }
        if (clicked_button)
          continue;

        int col = (int)floorf((mx - (float)board_x(&g)) / CELL);
        int row = (int)floorf((my - HEADER_H) / CELL);
        if (col < 0 || col >= g.cols || row < 0 || row >= g.rows)
          continue;
        int i = row * g.cols + col;

        if (e.button.button == SDL_BUTTON_RIGHT) {
          flag_click(&g, i);
        } else if (e.button.button == SDL_BUTTON_MIDDLE) {
          if (!g.over && g.revealed[i]) {
            chord(&g, i);
            finish_check(&g);
          }
        } else if (e.button.button == SDL_BUTTON_LEFT) {
          if (SDL_GetModState() & SDL_KMOD_CTRL)
            flag_click(&g, i);
          else
            left_click(&g, i);
        }
      }
    }
    draw_game(ren, &g);
    SDL_Delay(8);
  }

  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
