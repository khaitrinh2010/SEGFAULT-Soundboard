#ifndef SOUND_SEG_H
#define SOUND_SEG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44
#define MAX_NODES 30000
#define LARGEST_ID 65535

// Struct declarations
#pragma pack(push, 1)
struct sound_seg_node {
    union A {
        struct {
            int16_t sample;
        } parent_data;
        struct {
            uint16_t parent_id;
        } child_data;
    } A;
    uint8_t refCount;
    uint16_t next_id;
    struct {
        unsigned isAncestor : 1;
        unsigned isParent : 1;
        unsigned dont_do_anything : 6;
    } flags;
};

struct sound_seg {
    uint16_t head_id;
};
#pragma pack(pop)
extern struct sound_seg_node* node_pool[MAX_NODES];
extern uint16_t node_count;

uint16_t alloc_node(void);
void free_node(uint16_t id);
struct sound_seg_node* get_node(uint16_t id);
int16_t get_sample(uint16_t node_id);
void set_sample(uint16_t node_id, int16_t value);
struct sound_seg* tr_init(void);
void tr_destroy(struct sound_seg* track);
size_t tr_length(struct sound_seg* track);
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len);
void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len);
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len);
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len);
char* tr_identify(struct sound_seg* target, struct sound_seg* ad);
void wav_load(const char* filename, int16_t* dest);
void wav_save(const char* fname, int16_t* src, size_t len);

#endif
