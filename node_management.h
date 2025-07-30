#ifndef NODE_MANAGEMENT_H
#define NODE_MANAGEMENT_H

#include <stdint.h>
#include <stddef.h>

#define MAX_NODES 30000
#define LARGEST_ID 65535

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
extern struct sound_seg_node* node_pool[MAX_NODES];
extern uint16_t node_count;

uint16_t alloc_node(void);
void free_node(uint16_t id);
struct sound_seg_node* get_node(uint16_t id);
int16_t get_sample(uint16_t node_id);
void set_sample(uint16_t node_id, int16_t value);

#endif
