#include "maydos_tools.h"
#define MD_SCALE 1000LL
#define MD_LIMIT 1000000000LL
typedef struct { const char *p; int error; int depth; } Expr;
static void space(Expr *e) { while (*e->p == ' ' || *e->p == '\t') ++e->p; }
static long long expression(Expr *e);
static long long factor(Expr *e) {
    long long n = 0, fraction = 0;
    int digits = 0, precision = 0;
    space(e);
    if (e->depth++ > 16) { e->error = 1; --e->depth; return 0; }
    if (*e->p == '-' || *e->p == '+') {
        int neg = *e->p++ == '-';
        n = factor(e);
        --e->depth;
        return neg ? -n : n;
    }
    if (*e->p == '(') {
        ++e->p;
        n = expression(e);
        space(e);
        if (*e->p != ')') e->error = 1;
        else ++e->p;
        --e->depth;
        return n;
    }
    while (*e->p >= '0' && *e->p <= '9') {
        if (n > MD_LIMIT / MD_SCALE / 10) e->error = 1;
        if (!e->error) n = n * 10 + (*e->p - '0');
        ++e->p; ++digits;
    }
    if (*e->p == '.') {
        ++e->p;
        while (*e->p >= '0' && *e->p <= '9') {
            if (precision < 3) fraction = fraction * 10 + (*e->p - '0');
            else if (*e->p != '0') e->error = 1;
            ++precision; ++digits; ++e->p;
        }
    }
    if (!digits) e->error = 1;
    while (precision < 3) { fraction *= 10; ++precision; }
    n = n * MD_SCALE + fraction;
    if (n > MD_LIMIT) e->error = 1;
    --e->depth;
    return n;
}
static long long term(Expr *e) {
    long long n = factor(e);
    for (;;) {
        space(e);
        char op = *e->p;
        if (op != '*' && op != '/') break;
        ++e->p;
        long long b = factor(e);
        if (e->error) return 0;
        if (op == '*') n = n * b / MD_SCALE;
        else {
            if (!b) { e->error = 2; return 0; }
            n = n * MD_SCALE / b;
        }
        if (n > MD_LIMIT || n < -MD_LIMIT) { e->error = 3; return 0; }
    }
    return n;
}
static long long expression(Expr *e) {
    long long n = term(e);
    for (;;) {
        space(e);
        char op = *e->p;
        if (op != '+' && op != '-') break;
        ++e->p;
        long long b = term(e);
        if (e->error) return 0;
        n = op == '+' ? n + b : n - b;
        if (n > MD_LIMIT || n < -MD_LIMIT) { e->error = 3; return 0; }
    }
    return n;
}
static void append_digit(char *out, int *pos, int cap, int d) {
    if (*pos < cap - 1) out[(*pos)++] = (char)('0' + d);
}
static void append_text(char *out, int cap, const char *s) {
    int i = 0;
    while (s[i] && i < cap - 1) { out[i] = s[i]; ++i; }
    if (cap > 0) out[i] = 0;
}
int md_calc_eval(const char *input, char *out, int cap) {
    if (!out || cap < 3 || !input) return 0;
    Expr e = { input, 0, 0 };
    long long n = expression(&e);
    space(&e);
    if (*e.p) e.error = 1;
    if (e.error) {
        append_text(out, cap, e.error == 2 ? "Division by zero" :
                    e.error == 3 ? "Number out of range" : "Invalid expression");
        return 0;
    }
    int pos = 0;
    if (n < 0) { if (pos < cap - 1) out[pos++] = '-'; n = -n; }
    long long whole = n / MD_SCALE, f = n % MD_SCALE;
    char reverse[24]; int len = 0;
    do { reverse[len++] = (char)('0' + whole % 10); whole /= 10; } while (whole);
    while (len && pos < cap - 1) out[pos++] = reverse[--len];
    if (f) {
        if (pos < cap - 1) out[pos++] = '.';
        int places[3] = { (int)(f / 100), (int)(f / 10 % 10), (int)(f % 10) };
        int count = 3;
        while (count > 0 && !places[count - 1]) --count;
        for (int i = 0; i < count; ++i) append_digit(out, &pos, cap, places[i]);
    }
    out[pos] = 0;
    return 1;
}
static unsigned randstep(unsigned *seed) { *seed = *seed * 1664525u + 1013904223u; return *seed; }
static int valid_cell(const MayDosMines *g, int r, int c) {
    return g && r >= 0 && r < g->rows && c >= 0 && c < g->cols;
}
int md_mines_new(MayDosMines *g, int mode, int rows, int cols, int mines, unsigned seed) {
    if (!g) return 0;
    if (mode == 1) { rows = 9; cols = 9; mines = 10; }
    else if (mode == 2) { rows = 16; cols = 16; mines = 40; }
    else if (mode == 3) { rows = 16; cols = 30; mines = 99; } /* landscape layout */
    else if (mode != 4) return 0;
    if (rows < 5 || rows > 20 || cols < 5 || cols > 30 || rows * cols > MD_MAX_CELLS ||
        mines < 1 || mines >= rows * cols) return 0;
    if (mode == 4 && (mines * 100 < rows * cols * 15 || mines * 100 > rows * cols * 25)) return 0;
    g->rows = rows; g->cols = cols; g->mines = mines;
    g->opened = g->flags = g->finished = g->won = g->populated = 0;
    g->seed = seed ? seed : 0x4d617944u;
    g->started_at = 0;
    for (int i = 0; i < MD_MAX_CELLS; ++i) { g->values[i] = g->states[i] = 0; }
    return 1;
}
static void populate(MayDosMines *g, int safe) {
    int total = g->rows * g->cols, placed = 0;
    while (placed < g->mines) {
        int index = (int)(randstep(&g->seed) % (unsigned)total);
        if (index == safe || g->values[index] == 9) continue;
        g->values[index] = 9;
        ++placed;
    }
    for (int r = 0; r < g->rows; ++r) for (int c = 0; c < g->cols; ++c) {
        int i = r * g->cols + c;
        if (g->values[i] == 9) continue;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            int nr = r + dy, nc = c + dx;
            if (valid_cell(g, nr, nc) && g->values[nr * g->cols + nc] == 9) ++g->values[i];
        }
    }
    g->populated = 1;
}
int md_mines_flag(MayDosMines *g, int r, int c) {
    if (!valid_cell(g, r, c) || g->finished) return 0;
    int i = r * g->cols + c;
    if (g->states[i] == 1) return 0;
    if (g->states[i] == 2) { g->states[i] = 0; --g->flags; }
    else { g->states[i] = 2; ++g->flags; }
    return 1;
}
/* 0 = ignored, 1 = revealed, 2 = won, -1 = mine. */
int md_mines_reveal(MayDosMines *g, int r, int c, unsigned ticks) {
    if (!valid_cell(g, r, c) || g->finished) return 0;
    int first = r * g->cols + c;
    if (g->states[first]) return 0;
    if (!g->populated) { populate(g, first); g->started_at = ticks; }
    if (g->values[first] == 9) { g->finished = 1; g->won = 0; g->states[first] = 1; return -1; }
    int queue[MD_MAX_CELLS], front = 0, back = 0;
    queue[back++] = first;
    g->states[first] = 1;
    while (front < back) {
        int at = queue[front++]; ++g->opened;
        if (g->values[at]) continue;
        int rr = at / g->cols, cc = at % g->cols;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            int nr = rr + dy, nc = cc + dx;
            if (!valid_cell(g, nr, nc)) continue;
            int ni = nr * g->cols + nc;
            if (!g->states[ni] && g->values[ni] != 9) {
                g->states[ni] = 1; queue[back++] = ni;
            }
        }
    }
    if (g->opened == g->rows * g->cols - g->mines) { g->finished = 1; g->won = 1; return 2; }
    return 1;
}
