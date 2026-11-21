#ifndef MAYOS_HDA_H
#define MAYOS_HDA_H
#include "core.h"
int hda_init(void);
int hda_codec_count(void);
int hda_output_widget_count(void);
int hda_verb(u8 codec, u8 node, u16 verb, u8 payload, u32 *response);
#endif
