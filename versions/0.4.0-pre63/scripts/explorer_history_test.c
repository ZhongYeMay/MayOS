#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/explorer_history.h"
int main(void) {
    ExplorerHistory h = {0};
    assert(!explorer_history_move(&h, -1));
    explorer_history_push(&h, "http://a/");
    explorer_history_push(&h, "http://b/");
    explorer_history_push(&h, "http://b/");
    assert(h.count == 2);
    assert(!strcmp(explorer_history_move(&h, -1), "http://a/"));
    assert(!explorer_history_move(&h, -1));
    assert(!strcmp(explorer_history_move(&h, 1), "http://b/"));
    assert(!strcmp(explorer_history_move(&h, -1), "http://a/"));
    explorer_history_push(&h, "http://c/");
    assert(!explorer_history_move(&h, 1));
    for (int i = 0; i < 20; i++) {
        char url[72];
        snprintf(url, sizeof(url), "http://host/%d", i);
        explorer_history_push(&h, url);
    }
    assert(h.count == 8 && h.position == 7);
    assert(!strcmp(h.urls[0], "http://host/12"));
    assert(!strcmp(h.urls[7], "http://host/19"));
    puts("PASS: navigation bounds, duplicate suppression, branching and bounded history");
}
