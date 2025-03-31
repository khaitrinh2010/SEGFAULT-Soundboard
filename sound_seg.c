#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44
#define MAX_NODES 65535

uint16_t node_count = 0;

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
    bool isParent;
    bool isAncestor;
};

struct sound_seg {
    uint16_t head_id;
};
#pragma pack(pop)
struct sound_seg_node* node_pool[MAX_NODES] = {0};
uint16_t alloc_node() {
    struct sound_seg_node *newly_created_node = (struct sound_seg_node*)malloc(sizeof(struct sound_seg_node));
    if (!newly_created_node) {
        return 65535;
    }
    node_pool[node_count] = newly_created_node;
    uint16_t allocated_id = node_count;
    node_count++;
    return allocated_id;
}

void free_node(uint16_t id) {
    struct sound_seg_node* node = node_pool[id];
    if ( !node ) {
        return;
    };
    free(node_pool[id]);
    node_pool[id] = NULL;
}
struct sound_seg_node* get_node(uint16_t id) {
    struct sound_seg_node* node = node_pool[id];
    if (!node) {
        return NULL;
    }
    return node;
}
int16_t get_sample(uint16_t node_id) {
    struct sound_seg_node* node = get_node(node_id);
    if (!node) return 0;
    int16_t result = 0;
    while (!node->isAncestor) {
        node = get_node(node->A.child_data.parent_id);
    }
    result =  node->A.parent_data.sample;
    return result;
}

void set_sample(uint16_t node_id, int16_t value) {
    struct sound_seg_node* node = get_node(node_id);
    if (!node) return;
    while (!node->isAncestor) {
        node = get_node(node->A.child_data.parent_id);
    }
    node->A.parent_data.sample = value;
}

void wav_load(const char* filename, int16_t* dest) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        printf("Error opening file\n");
        return;
    }
    fseek(file, OFFSET, SEEK_SET);
    uint32_t number_of_bytes;
    fread(&number_of_bytes, sizeof(uint32_t), 1, file);
    fseek(file, OFFSET_TO_AUDIO_DATA, SEEK_SET);
    fread(dest, sizeof(int16_t), number_of_bytes / sizeof(int16_t), file);
    fclose(file);
}

void wav_save(const char* fname, int16_t* src, size_t len) {
    FILE *file = fopen(fname, "wb");
    if (file == NULL) {
        printf("Error opening file\n");
        return;
    }
    union wav_header {
        struct {
            char riff[4];
            uint32_t flength;
            char wave[4];
            char fmt[4];
            int32_t chunk_size;
            int16_t format_tag;
            int16_t num_chans;
            int32_t sample_rate;
            int32_t bytes_per_sec;
            int16_t bytes_per_sample;
            int16_t bits_per_sample;
            char data[4];
            int32_t dlength;
        } fields;
        char bytes[OFFSET_TO_AUDIO_DATA];
    } header;

    memcpy(header.fields.riff, "RIFF", 4);
    header.fields.flength = len * sizeof(int16_t) + OFFSET_TO_AUDIO_DATA;
    memcpy(header.fields.wave, "WAVE", 4);
    memcpy(header.fields.fmt, "fmt ", 4);
    header.fields.chunk_size = 16;
    header.fields.format_tag = 1;
    header.fields.num_chans = 1;
    header.fields.sample_rate = 8000;
    header.fields.bytes_per_sec = 8000 * 2;
    header.fields.bytes_per_sample = 2;
    header.fields.bits_per_sample = 16;
    memcpy(header.fields.data, "data", 4);
    header.fields.dlength = len * sizeof(int16_t);

    fwrite(&header, sizeof(header), 1, file);
    fwrite(src, sizeof(int16_t), len, file);
    fclose(file);
}

struct sound_seg* tr_init(void) {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->head_id = UINT16_MAX; //Initialize a track, no head yet, basically this head is serves as the end of the linked list, just like the '\0' in a string
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    uint16_t id = track->head_id;
    while (id != 65535) {
        struct sound_seg_node* node = get_node(id);
        if (!node) break;
        uint16_t next_id = node->next_id;
        free_node(id);
        id = next_id;
    }
    free(track);
}

size_t tr_length(struct sound_seg* track) {
    size_t length = 0;
    uint16_t current_id = track->head_id;
    while (current_id != UINT16_MAX) {
        length++;
        struct sound_seg_node* current = get_node(current_id);
        if (!current) break;
        current_id = current->next_id;
    }
    return length;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || len == 0) return;
    uint16_t id = track->head_id;
    size_t elements_have_passed = 0;
    while (id != 65535 && elements_have_passed < pos) {
        struct sound_seg_node* node = get_node(id);
        if (!node) break;
        id  = node->next_id;
        elements_have_passed++;
    }
    //Now id stop at the elements we want to read
    for (size_t i = 0; i < len; i++) {
        struct sound_seg_node* current = get_node(id);
        if (!current) break;
        dest[i] = get_sample(id);
        id  = current->next_id;
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;
    uint16_t current_id = track->head_id;
    uint16_t prev_id = UINT16_MAX;
    bool start_of_linked_list = false;
    size_t i = 0;
    while (current_id != UINT16_MAX && i < pos) {
        struct sound_seg_node* current = get_node(current_id);
        if (!current) return;
        prev_id = current_id;
        current_id = current->next_id;
        i++;
    }
    if (prev_id != UINT16_MAX) {
        start_of_linked_list = true;
    }

    if (i < pos) {
    // cannot reach the position we want to write
        while (i < pos) {
            uint16_t new_node_id = alloc_node();
            if (new_node_id == UINT16_MAX) return;
            struct sound_seg_node* new_node = get_node(new_node_id);
            new_node->isParent = true;
            new_node->isAncestor = true; // When creating new nodes, it is the ancestor
            new_node->A.parent_data.sample = 0;
            new_node->refCount = 0;
            new_node->next_id = UINT16_MAX;
            if (start_of_linked_list) {
                get_node(prev_id)->next_id = new_node_id;
            } else {
                track->head_id = new_node_id;
            }
            prev_id = new_node_id;
            i++;;
        }
    }

    for (size_t j = 0; j < len; j++) {
        if (current_id != UINT16_MAX) {
            struct sound_seg_node* current = get_node(current_id);
            if (!current) return;
            set_sample(current_id, src[j]);
            prev_id = current_id;
            current_id = current->next_id;
        }
        else { // have to create spaces for new nodes
            uint16_t new_node_id = alloc_node();
            if (new_node_id == UINT16_MAX) return;
            struct sound_seg_node* new_node = get_node(new_node_id);
            new_node->isParent = true;
            new_node->isAncestor = true; // When creating new nodes, it is the ancestor
            new_node->A.parent_data.sample = src[j];
            new_node->refCount = 0;
            new_node->next_id = UINT16_MAX;
            if (prev_id != UINT16_MAX) {
                get_node(prev_id)->next_id = new_node_id;
            } else {
                track->head_id = new_node_id;
            }
            prev_id = new_node_id;

        }
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || len == 0 || pos >= tr_length(track)) return false;
    uint16_t current_id = track->head_id;
    uint16_t prev_id = UINT16_MAX;
    size_t i = 0;
    while (current_id != UINT16_MAX && i < pos) {
        struct sound_seg_node* current = get_node(current_id);
        if (!current) return false;
        prev_id = current_id;
        current_id = current->next_id;
        i++;
    }
    //current_id stops at the position we want to delete
    if (current_id == UINT16_MAX) return false;

    uint16_t check_id = current_id;
    for (size_t j = 0; j < len && check_id != UINT16_MAX; j++) {
        struct sound_seg_node* check = get_node(check_id);
        if (!check) break;
        if (check->isParent && check->refCount > 0) {
            return false;
        }
        check_id = check->next_id;
    }
    for (size_t j = 0; j < len && current_id != UINT16_MAX; j++) {
        struct sound_seg_node* current = get_node(current_id);
        if (!current) break;
        uint16_t next_id = current->next_id;
        if (!current->isParent && current->A.child_data.parent_id != UINT16_MAX) {
            struct sound_seg_node* parent = get_node(current->A.child_data.parent_id);
            if (parent) parent->refCount--;
        }
        free_node(current_id);
        current_id = next_id;
    }
    if (prev_id != UINT16_MAX) get_node(prev_id)->next_id = current_id;
    else track->head_id = current_id;
    return true;
}

double compute_cross_correlation(const int16_t* target, const int16_t* ad, size_t len) {
    double sum_product = 0.0;
    double sum_ad_sq = 0.0;
    for (size_t i = 0; i < len; i++) {
        sum_product += (double)target[i] * (double)ad[i];
        sum_ad_sq += (double)ad[i] * (double)ad[i];
    }
    return sum_product / sum_ad_sq;
}

char* tr_identify(struct sound_seg* target, struct sound_seg* ad) {
    if (!target || !ad || tr_length(ad) > tr_length(target)) return strdup("");
    size_t target_len = tr_length(target);
    size_t ad_len = tr_length(ad);
    int16_t* target_data = malloc(target_len * sizeof(int16_t));
    int16_t* ad_data = malloc(ad_len * sizeof(int16_t));
    if (!target_data || !ad_data) {
        free(target_data);
        free(ad_data);
        return strdup("");
    }
    tr_read(target, target_data, 0, target_len);
    tr_read(ad, ad_data, 0, ad_len);

    char* result = malloc(256);
    if (!result) {
        free(target_data);
        free(ad_data);
        return strdup("");
    }
    size_t capacity = 256;
    size_t used = 0;
    result[0] = '\0';

    for (size_t i = 0; i <= target_len - ad_len; i++) {
        double corr = compute_cross_correlation(target_data + i, ad_data, ad_len);
        if (corr >= 0.95) {
            char temp[32];
            snprintf(temp, sizeof(temp), "%zu,%zu\n", i, i + ad_len - 1);
            size_t len = strlen(temp);
            if (used + len + 1 > capacity) {
                capacity *= 2;
                char* new_result = realloc(result, capacity);
                if (!new_result) {
                    free(result);
                    free(target_data);
                    free(ad_data);
                    return strdup("");
                }
                result = new_result;
            }
            strcpy(result + used, temp);
            used += len;
            i += ad_len - 1;
        }
    }
    free(target_data);
    free(ad_data);
    if (used == 0) {
        free(result);
        return strdup("");
    }
    if (used > 0 && result[used - 1] == '\n') result[used - 1] = '\0';
    return result;
}

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || len == 0 || srcpos + len > tr_length(src_track)) return;
    uint16_t src_current_id = src_track->head_id;
    size_t i = 0;
    while (src_current_id != UINT16_MAX && i < srcpos) {
        struct sound_seg_node* src_current = get_node(src_current_id);
        if (!src_current) return;
        src_current_id = src_current->next_id;
        i++;
    }
    //Now src_current_id stops at the position we want to insert
    if (src_current_id == UINT16_MAX) return;
    uint16_t dest_current_id = dest_track->head_id;
    uint16_t dest_prev_id = UINT16_MAX;
    i = 0;
    while (dest_current_id != UINT16_MAX && i < destpos) {
        struct sound_seg_node* dest_current = get_node(dest_current_id);
        if (!dest_current) return;
        dest_prev_id = dest_current_id;
        dest_current_id = dest_current->next_id;
        i++;
    }
    //Now dest_current_id stops at the position we want to insert

    uint16_t insert_head_id = UINT16_MAX;
    uint16_t insert_tail_id = UINT16_MAX;
    uint16_t src_temp_id = src_current_id;

    for (size_t j = 0; j < len && src_temp_id != UINT16_MAX; j++) {
        uint16_t new_id = alloc_node();
        if (new_id == UINT16_MAX) return;
        struct sound_seg_node* new_node = get_node(new_id);
        struct sound_seg_node* parent_node = get_node(src_temp_id);
        new_node->A.child_data.parent_id = src_temp_id;
        new_node->next_id = UINT16_MAX;
        new_node->isParent = false;
        new_node->isAncestor = false;
        parent_node->refCount++;
        parent_node->isParent = true;
        if (insert_head_id == UINT16_MAX) {
            insert_head_id = insert_tail_id = new_id;
        } else {
            get_node(insert_tail_id)->next_id = new_id;
            insert_tail_id = new_id;
        }
        struct sound_seg_node* src_temp = get_node(src_temp_id);
        if (src_temp) src_temp_id = src_temp->next_id;
    }

    if (insert_head_id != UINT16_MAX) {
        get_node(insert_tail_id)->next_id = dest_current_id;
        if (dest_prev_id != UINT16_MAX) get_node(dest_prev_id)->next_id = insert_head_id;
        else dest_track->head_id = insert_head_id;
    }
}
int main(int argc, char** argv) {
    return 0;
}