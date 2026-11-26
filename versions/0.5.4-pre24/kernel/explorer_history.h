#ifndef EXPLORER_HISTORY_H
#define EXPLORER_HISTORY_H
#define EXPLORER_HISTORY_LIMIT 8
typedef struct {
    char urls[EXPLORER_HISTORY_LIMIT][72];
    int count, position;
} ExplorerHistory;
static void explorer_history_copy(char *to, const char *from) {
    int i = 0;
    for (; i < 71 && from[i]; i++) to[i] = from[i];
    to[i] = 0;
}
static void explorer_history_push(ExplorerHistory *h, const char *url) {
    if (h->count) {
        const char *current = h->urls[h->position];
        int i = 0;
        while (current[i] && current[i] == url[i]) i++;
        if (current[i] == url[i]) return;
        h->count = h->position + 1;
    }
    if (h->count == EXPLORER_HISTORY_LIMIT) {
        for (int i = 1; i < h->count; i++)
            explorer_history_copy(h->urls[i - 1], h->urls[i]);
        h->count--;
    }
    explorer_history_copy(h->urls[h->count], url);
    h->position = h->count++;
}
static const char *explorer_history_move(ExplorerHistory *h, int delta) {
    int next = h->position + delta;
    if (!h->count || next < 0 || next >= h->count) return 0;
    h->position = next;
    return h->urls[next];
}
#endif
