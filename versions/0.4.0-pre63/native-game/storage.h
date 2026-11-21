#ifndef SUMMER_STORAGE_H
#define SUMMER_STORAGE_H
#include "engine.h"
int summer_save_file(const SummerGame *game,const char *filename);
int summer_load_file(SummerGame *game,const char *filename);
#endif
