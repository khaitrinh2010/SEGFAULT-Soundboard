#include "node_management.h"
#include <stdlib.h>

struct sound_seg_node* node_pool[MAX_NODES] = {0};
uint16_t node_count = 0;

uint16_t alloc_node() {
    struct sound_seg_node *newly_created_node = (struct sound_seg_node*)malloc(sizeof(struct sound_seg_node));
    if (!newly_created_node) {
        return 65535;
    }
    node_pool[node_count] = newly_created_node;
    uint16_t new_id = node_count;
    node_count++;
    return new_id;
}

void free_node(uint16_t id) {
    struct sound_seg_node* node = node_pool[id];
    if ( node ) {
        free(node_pool[id]);
    	node_pool[id] = NULL;
    };
}

struct sound_seg_node* get_node(uint16_t id) {
    struct sound_seg_node* node = node_pool[id];
    if (node) {
        return node;
    }
    return NULL;
}

int16_t get_sample(uint16_t node_id) {
    struct sound_seg_node* node = get_node(node_id);
    if (!node) {
        return 0;
    }
    int16_t result = 0;
    while (!node->flags.isAncestor) {
        node = get_node(node->A.child_data.parent_id);
    }
    result =  node->A.parent_data.sample;
    return result;
}

void set_sample(uint16_t node_id, int16_t value) {
    struct sound_seg_node* node = get_node(node_id);
    if (!node) {
      	return;
     }
    while (!node->flags.isAncestor) {
        node = get_node(node->A.child_data.parent_id);
    }
    node->A.parent_data.sample = value;
}