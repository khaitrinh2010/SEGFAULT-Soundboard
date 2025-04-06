#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "sound_seg.h"
struct sound_seg_node* node_pool[MAX_NODES] = {0};
uint16_t node_count = 0;


struct sound_seg* tr_init(void) {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) {
        return NULL;
    }
    track->head_id = LARGEST_ID; //Initialize a track, no head yet, basically this head is serves as the end of the linked list, just like the '\0' in a string, so that is the intention of mine, using a linked list but not using the next ptr to reduce memory usage
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    uint16_t id = track->head_id;
    while (id != LARGEST_ID) {
        struct sound_seg_node* node = get_node(id);
        uint16_t next_id = node->next_id;
        if (node) {
            free_node(id);
        }
        id = next_id;
    }
    free(track);
}

size_t tr_length(struct sound_seg* track) {
    size_t length = 0;
    uint16_t current_id = track->head_id;
    while (current_id != LARGEST_ID) {
        struct sound_seg_node* current = get_node(current_id);
        if (!current) {
            while (current_id != LARGEST_ID) {
                current_id += 1;
                current = get_node(current_id);
                if (current) {
                    break;
                }
            }
        }
        if (current) {
            length++;
            current_id = current->next_id;
        } else {
            break;
        }
    }
    return length;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    uint16_t id = track->head_id;
    size_t elements_have_passed = 0;
    while (id != LARGEST_ID && elements_have_passed < pos) {
        struct sound_seg_node* node = get_node(id);
        if (!node) {
            while (id != LARGEST_ID) {
                id += 1;
                node = get_node(id);
                if (node) {
                    break;
                }
            }
        }
        if (node) {
            id = node->next_id;
            elements_have_passed++;
        } else {
            break;
        }
    }
    for (size_t i = 0; i < len; i++) {
        struct sound_seg_node* current = get_node(id);
        if (!current) {
            while (id != LARGEST_ID) {
                id += 1;
                current = get_node(id);
                if (current) {
                    break;
                }
            }
        }
        if (current) {
            dest[i] = get_sample(id);
            id = current->next_id;
        } else {
            break;
        }
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    uint16_t current_id = track->head_id;
    uint16_t prev_id = LARGEST_ID;
    bool start_of_linked_list = false;
    size_t i = 0;
    while (current_id != LARGEST_ID && i < pos) {
        struct sound_seg_node* current = get_node(current_id);
        if (!current) {
            while (current_id != LARGEST_ID) {
                current_id += 1;
                current = get_node(current_id);
                if (current) {
                    break;
                }

            }
        }
        if (current) {
            prev_id = current_id;
            current_id = current->next_id;
            i++;
        } else {
            break;
        }
    }
    if (prev_id != LARGEST_ID) {
        start_of_linked_list = true;
    }

    if (i < pos) {
        while (i < pos) {
            uint16_t new_node_id = alloc_node();
            if (new_node_id == LARGEST_ID) {
                return;
            }
            struct sound_seg_node* new_node = get_node(new_node_id);
            new_node->flags.isParent = 1;
            new_node->flags.isAncestor = 1;
            new_node->A.parent_data.sample = 0;
            new_node->refCount = 0;
            new_node->next_id = LARGEST_ID;
            if (start_of_linked_list) {
                get_node(prev_id)->next_id = new_node_id;
            } else {
                track->head_id = new_node_id;
            }
            prev_id = new_node_id;
            i++;
        }
    }

    for (size_t j = 0; j < len; j++) {
        if (current_id != LARGEST_ID) {
            struct sound_seg_node* current = get_node(current_id);
            if (!current) {
                while (current_id != LARGEST_ID) {
                    current_id += 1;
                    current = get_node(current_id);
                    if (current) {
                        break;
                    }
                }
            }
            if (current) {
                set_sample(current_id, src[j]);
                prev_id = current_id;
                current_id = current->next_id;
            }
        }
        else { // have to create spaces for new nodes
            uint16_t new_node_id = alloc_node();
            if (new_node_id == LARGEST_ID) {
                return;
            }
            struct sound_seg_node* new_node = get_node(new_node_id);
            new_node->flags.isParent = 1;
            new_node->flags.isAncestor = 1;
            new_node->A.parent_data.sample = src[j];
            new_node->refCount = 0;
            new_node->next_id = LARGEST_ID;
            if (prev_id != LARGEST_ID) {
                get_node(prev_id)->next_id = new_node_id;
            } else {
                track->head_id = new_node_id;
            }
            prev_id = new_node_id;
        }
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    uint16_t current_id = track->head_id;
    uint16_t prev_id = LARGEST_ID;
    size_t i = 0;
    while (current_id != LARGEST_ID && i < pos) {
        struct sound_seg_node* current = get_node(current_id);
        if (!current) {
            while (current_id != LARGEST_ID) {
                current_id += 1;
                current = get_node(current_id);
                if (current) {
                    break;
                }
            }
        }
        if (current) {
            prev_id = current_id;
            current_id = current->next_id;
            i++;
        } else {
            break;
        }
    }
    uint16_t check_id = current_id;
    for (size_t j = 0; j < len && check_id != LARGEST_ID; j++) {
        struct sound_seg_node* check = get_node(check_id);
        if (!check) {
            while (check_id != LARGEST_ID) {
                check_id += 1;
                check = get_node(check_id);
                if (check) {
                    break;
                }
            }
        }
        if (check) {
            if (check->flags.isParent && check->refCount > 0) {
                return false;
            }
            check_id = check->next_id;
        } else {
            break;
        }
    }
    for (size_t j = 0; j < len && current_id != LARGEST_ID; j++) {
        struct sound_seg_node* current = get_node(current_id);
        if (!current) {
            while (current_id != LARGEST_ID) {
                current_id += 1;
                current = get_node(current_id);
                if (current) {
                    break;
                }
            }
        }
        if (current) {
            uint16_t next_id = current->next_id;
            if (!current->flags.isParent && current->A.child_data.parent_id != LARGEST_ID) {
                struct sound_seg_node* parent = get_node(current->A.child_data.parent_id);
                if (parent) {
                    parent->refCount--;
                }
            }
            free_node(current_id);
            current_id = next_id;
        } else {
            break;
        }
    }
    if (prev_id != LARGEST_ID) {
        get_node(prev_id)->next_id = current_id;
    }
    else {
        track->head_id = current_id;
    }
    return true;
}

double compute_cross_correlation(const int16_t* target, const int16_t* ad, size_t len) {
    double sum1 = 0.0;
    double sum2 = 0.0;
    for (size_t i = 0; i < len; i++) {
        sum1 += (double)target[i] * (double)ad[i];
        sum2 += (double)ad[i] * (double)ad[i];
    }
    return sum1 / sum2;
}
char* tr_identify(struct sound_seg* track, struct sound_seg* ad) {
    char* empty_result = "";
    size_t samples_in_track = tr_length(track);
    size_t samples_in_ad = tr_length(ad);
    int16_t* track_samples = malloc(samples_in_track * sizeof(int16_t));
    int16_t* ad_samples = malloc(samples_in_ad * sizeof(int16_t));
    tr_read(track, track_samples, 0, samples_in_track);
    tr_read(ad, ad_samples, 0, samples_in_ad);
    char* result_string = malloc(256);
    size_t result_capacity = 256;
    size_t result_length = 0;
    result_string[0] = '\0';
    for (size_t start_index = 0; start_index <= samples_in_track - samples_in_ad; start_index++) {
        int16_t* samples_in_track_to_check = track_samples + start_index;
        double correlation = compute_cross_correlation(samples_in_track_to_check, ad_samples, samples_in_ad);
        if (correlation >= 0.95) {
            size_t match_start = start_index;
            size_t match_end = start_index + samples_in_ad - 1;
            char line[32];
            sprintf(line, "%zu,%zu\n", match_start, match_end);
            size_t line_length = strlen(line);
            if (result_length + line_length + 1 > result_capacity) {
                result_capacity *= 2;
                char* new_result_string = realloc(result_string, result_capacity);
                if (!new_result_string) {
                    free(result_string);
                    free(track_samples);
                    free(ad_samples);
                    return empty_result;
                }
                result_string = new_result_string;
            }

            for (size_t i = 0; i < line_length; i++) {
                result_string[result_length++] = line[i];
            }
            result_string[result_length] = '\0';
            start_index += samples_in_ad - 1;
        }
    }
    free(track_samples);
    free(ad_samples);
    if (result_length == 0) {
        free(result_string);
        return empty_result;
    }
    if (result_string[result_length - 1] == '\n') {
        result_string[result_length - 1] = '\0';
    }
    return result_string;
}

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len) {
    uint16_t src_current_id = src_track->head_id;
    size_t i = 0;
    while (src_current_id != LARGEST_ID && i < srcpos) {
        struct sound_seg_node* src_current = get_node(src_current_id);
        if (!src_current) return;
        src_current_id = src_current->next_id;
        i++;
    }
    if (src_current_id == LARGEST_ID) return;
    uint16_t dest_current_id = dest_track->head_id;
    uint16_t dest_prev_id = LARGEST_ID;
    i = 0;

    while (dest_current_id != LARGEST_ID && i < destpos) {
        struct sound_seg_node* dest_current = get_node(dest_current_id);
        if (!dest_current) return;
        dest_prev_id = dest_current_id;
        dest_current_id = dest_current->next_id;
        i++;
    }
    //Now dest_current_id stops at the position we want to insert
    uint16_t insert_head_id = LARGEST_ID;
    uint16_t insert_tail_id = LARGEST_ID;
    uint16_t src_temp_id = src_current_id;
    for (size_t j = 0; j < len && src_temp_id != LARGEST_ID; j++) {
        uint16_t new_id = alloc_node();
        if (new_id == LARGEST_ID) return;
        struct sound_seg_node* new_node = get_node(new_id);
        struct sound_seg_node* parent_node = get_node(src_temp_id);
        new_node->A.child_data.parent_id = src_temp_id;
        new_node->next_id = LARGEST_ID;
        new_node->flags.isParent = 0;
        new_node->flags.isAncestor = 0;
        parent_node->refCount++;
        parent_node->flags.isParent = 1;
        if (insert_head_id == LARGEST_ID) {
            insert_head_id = insert_tail_id = new_id;
        } else {
            get_node(insert_tail_id)->next_id = new_id;
            insert_tail_id = new_id;
        }
        struct sound_seg_node* src_temp = get_node(src_temp_id);
        if (src_temp) {
            src_temp_id = src_temp->next_id;
        }
    }
    if (insert_head_id != LARGEST_ID) {
        get_node(insert_tail_id)->next_id = dest_current_id;
        if (dest_prev_id != LARGEST_ID) {
            get_node(dest_prev_id)->next_id = insert_head_id;
        }
        else {
            dest_track->head_id = insert_head_id;
        }
    }
}