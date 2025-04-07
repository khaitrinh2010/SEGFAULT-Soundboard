#ifndef SOUND_SEG_H
#define SOUND_SEG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "node_management.h"

struct sound_seg* tr_init(void);
void tr_destroy(struct sound_seg* track);
size_t tr_length(struct sound_seg* track);
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len);
void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len);
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len);
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len);
char* tr_identify(struct sound_seg* target, struct sound_seg* ad);

#endif
