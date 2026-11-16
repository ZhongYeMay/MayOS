#ifndef MAYOS_MAYDOS_TOOLS_H
#define MAYOS_MAYDOS_TOOLS_H
/* Shared freestanding MayDOS compatibility tools; no Python, OS or libc ABI. */
#define MD_MAX_CELLS 480
typedef struct {
    int rows, cols, mines, opened, flags, finished, won, populated;
    unsigned seed, started_at;
    unsigned char values[MD_MAX_CELLS]; /* 9 = mine, 0..8 adjacent count */
    unsigned char states[MD_MAX_CELLS]; /* 0 hidden, 1 open, 2 flagged */
} MayDosMines;
int md_calc_eval(const char *expression, char *result, int capacity);
int md_note_encode(const char *text, int length, char *encoded, int capacity);
int md_note_decode(const char *encoded, int length, char *text, int capacity);
int md_mines_new(MayDosMines *game, int mode, int rows, int cols, int mines, unsigned seed);
int md_mines_reveal(MayDosMines *game, int row, int col, unsigned ticks);
int md_mines_flag(MayDosMines *game, int row, int col);
#endif
