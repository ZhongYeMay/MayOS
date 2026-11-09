#ifndef MAYOS_SUMMER_ENGINE_H
#define MAYOS_SUMMER_ENGINE_H
#include <stddef.h>
#include <stdint.h>
#define SUMMER_HISTORY_MAX 4096
#define SUMMER_ID_MAX 80
#define SUMMER_SAVE_VERSION 1
typedef struct {
 const char *id, *route, *bg, *who, *text, *next, *mature_next;
 const char *choices[3], *targets[3];
 unsigned char pose, choice_count, ending;
 const char *action;
} SummerNode;
typedef struct {
 const SummerNode *nodes;
 size_t count;
 size_t index;
 size_t history[SUMMER_HISTORY_MAX];
 size_t history_count;
 unsigned char mature, auto_play, reduced_motion;
 unsigned char unlocked[6];
 unsigned char seen[4096];
} SummerGame;
typedef struct {
 uint32_t version, node_index, history_count, flags;
 uint32_t history[SUMMER_HISTORY_MAX];
 unsigned char seen[4096], unlocked[6];
 uint32_t checksum;
} SummerSave;
int summer_init(SummerGame *game, const SummerNode *nodes, size_t count);
int summer_go(SummerGame *game, const char *id, int record);
int summer_next(SummerGame *game);
int summer_choose(SummerGame *game, unsigned index);
int summer_save(const SummerGame *game, SummerSave *out);
int summer_load(SummerGame *game, const SummerSave *in);
const SummerNode *summer_current(const SummerGame *game);
#endif
