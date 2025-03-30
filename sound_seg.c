#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

static uint16_t next_cluster_id = 0;

#pragma pack(push, 1)
struct sound_seg_node {
    uint16_t node_id;
    uint16_t parent_id;
    uint16_t cluster_id;
    uint8_t ref_count;
    int16_t sample;
    bool is_parent;
};
#pragma pack(pop)

struct sound_seg {
    struct sound_seg_node* nodes;
    size_t length;
    size_t capacity;
    uint16_t next_node_id;
    struct sound_seg* next; // Linked list pointer to next track
};

static struct sound_seg* all_tracks = NULL; // Global list of all tracks

static uint16_t find_root(struct sound_seg_node* nodes, uint16_t node_id) {
    if (nodes[node_id].parent_id != node_id) {
        nodes[node_id].parent_id = find_root(nodes, nodes[node_id].parent_id);
    }
    return nodes[node_id].parent_id;
}

static void update_cluster(uint16_t root_id, int16_t new_sample) {
    struct sound_seg* track = all_tracks;
    while (track != NULL) {
        for (size_t i = 0; i < track->length; i++) {
            if (find_root(track->nodes, i) == root_id) {
                track->nodes[i].sample = new_sample;
            }
        }
        track = track->next;
    }
}

void wav_load(const char* filename, int16_t* dest) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
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

void wav_save(const char* fname, const int16_t* src, size_t len) {
    FILE *file = fopen(fname, "wb");
    if (!file) {
        printf("Error opening file\n");
        return;
    }
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
    } header = {
        .riff = "RIFF",
        .flength = len * sizeof(int16_t) + OFFSET_TO_AUDIO_DATA - 8,
        .wave = "WAVE",
        .fmt = "fmt ",
        .chunk_size = 16,
        .format_tag = 1,
        .num_chans = 1,
        .sample_rate = 8000,
        .bytes_per_sec = 8000 * 2,
        .bytes_per_sample = 2,
        .bits_per_sample = 16,
        .data = "data",
        .dlength = len * sizeof(int16_t)
    };
    fwrite(&header, sizeof(header), 1, file);
    fwrite(src, sizeof(int16_t), len, file);
    fclose(file);
}

struct sound_seg* tr_init(void) {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->nodes = NULL;
    track->length = 0;
    track->capacity = 0;
    track->next_node_id = 0;
    track->next = all_tracks; // Add to global list
    all_tracks = track;
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    struct sound_seg* current = all_tracks;
    struct sound_seg* prev = NULL;
    while (current != NULL && current != track) {
        prev = current;
        current = current->next;
    }
    if (current == track) {
        if (prev) {
            prev->next = current->next;
        } else {
            all_tracks = current->next;
        }
    }
    free(track->nodes);
    free(track);
}

size_t tr_length(struct sound_seg* track) {
    return track ? track->length : 0;
}

void tr_resize(struct sound_seg* track, size_t new_capacity) {
    if (!track || new_capacity <= track->capacity) return;
    struct sound_seg_node* new_nodes = realloc(track->nodes, new_capacity * sizeof(struct sound_seg_node));
    if (!new_nodes) return;
    track->nodes = new_nodes;
    for (size_t i = track->capacity; i < new_capacity; i++) {
        track->nodes[i].node_id = track->next_node_id++;
        track->nodes[i].parent_id = i;
        track->nodes[i].cluster_id = next_cluster_id++;
        track->nodes[i].ref_count = 1;
        track->nodes[i].sample = 0;
        track->nodes[i].is_parent = false;
    }
    track->capacity = new_capacity;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || pos >= track->length || len == 0) return;
    size_t end = pos + len > track->length ? track->length : pos + len;
    for (size_t i = pos; i < end; i++) {
        uint16_t root_id = find_root(track->nodes, i);
        dest[i - pos] = track->nodes[root_id].sample;
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;
    size_t new_length = pos + len;
    if (new_length > track->capacity) {
        tr_resize(track, new_length * 2 > track->capacity ? new_length * 2 : track->capacity + 1);
    }
    if (new_length > track->length) {
        for (size_t i = track->length; i < new_length; i++) {
            track->nodes[i].node_id = track->next_node_id++;
            track->nodes[i].parent_id = i;
            track->nodes[i].cluster_id = next_cluster_id++;
            track->nodes[i].ref_count = 1;
            track->nodes[i].sample = 0;
            track->nodes[i].is_parent = false;
        }
        track->length = new_length;
    }
    for (size_t i = 0; i < len; i++) {
        uint16_t root_id = find_root(track->nodes, pos + i);
        update_cluster(root_id, src[i]);
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos >= track->length || len == 0) return false;
    size_t end = pos + len > track->length ? track->length : pos + len;

    for (size_t i = pos; i < end; i++) {
        if (track->nodes[i].is_parent && track->nodes[i].ref_count > 1) return false;
    }

    for (size_t i = pos; i < end; i++) {
        if (track->nodes[i].parent_id != i) {
            uint16_t parent_id = track->nodes[i].parent_id;
            track->nodes[parent_id].ref_count--;
        }
    }

    for (size_t i = pos; i < track->length - (end - pos); i++) {
        track->nodes[i] = track->nodes[i + (end - pos)];
    }
    track->length -= (end - pos);
    for (size_t i = pos; i < track->length; i++) {
        if (track->nodes[i].parent_id >= end) track->nodes[i].parent_id -= (end - pos);
    }
    return true;
}

double compute_cross_correlation(const int16_t* target, const int16_t* ad, size_t len) {
    double sum_product = 0.0, sum_ad_sq = 0.0;
    for (size_t i = 0; i < len; i++) {
        sum_product += (double)target[i] * (double)ad[i];
        sum_ad_sq += (double)ad[i] * (double)ad[i];
    }
    return sum_ad_sq ? sum_product / sum_ad_sq : 0.0;
}

char* tr_identify(struct sound_seg* target, struct sound_seg* ad) {
    if (!target || !ad || tr_length(ad) > tr_length(target)) return strdup("");
    size_t target_len = tr_length(target), ad_len = tr_length(ad);
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
    size_t capacity = 256, used = 0;
    result[0] = '\0';

    double ad_auto = compute_cross_correlation(ad_data, ad_data, ad_len);
    if (ad_auto == 0.0) {
        free(target_data);
        free(ad_data);
        free(result);
        return strdup("");
    }

    for (size_t i = 0; i <= target_len - ad_len; i++) {
        double corr = compute_cross_correlation(target_data + i, ad_data, ad_len);
        if (corr / ad_auto >= 0.95) {
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
    if (used > 0 && result[used - 1] == '\n') result[used - 1] = '\0';
    return result;
}

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || srcpos + len > src_track->length || len == 0) return;
    size_t new_length = dest_track->length + len;
    if (new_length > dest_track->capacity) {
        tr_resize(dest_track, new_length * 2 > dest_track->capacity ? new_length * 2 : dest_track->capacity + 1);
    }
    for (size_t i = dest_track->length; i > destpos; i--) {
        dest_track->nodes[i + len - 1] = dest_track->nodes[i - 1];
    }
    dest_track->length = new_length;
    for (size_t i = 0; i < len; i++) {
        uint16_t src_idx = srcpos + i;
        uint16_t dest_idx = destpos + i;
        uint16_t src_root = find_root(src_track->nodes, src_idx);
        dest_track->nodes[dest_idx].node_id = dest_track->next_node_id++;
        dest_track->nodes[dest_idx].parent_id = src_root;
        dest_track->nodes[dest_idx].cluster_id = src_track->nodes[src_root].cluster_id;
        dest_track->nodes[dest_idx].ref_count = 1;
        dest_track->nodes[dest_idx].sample = src_track->nodes[src_root].sample;
        dest_track->nodes[dest_idx].is_parent = false;
        src_track->nodes[src_root].ref_count++;
        src_track->nodes[src_root].is_parent = true;
    }
    for (size_t i = destpos + len; i < dest_track->length; i++) {
        if (dest_track->nodes[i].parent_id >= destpos) {
            dest_track->nodes[i].parent_id += len;
        }
    }
}

int main(int argc, char** argv) {
    struct sound_seg* s0 = tr_init();
    tr_write(s0, ((int16_t[]){}), 0, 0);
    struct sound_seg* s1 = tr_init();
    tr_write(s1, ((int16_t[]){3,18,11,-8,5,-1,-18,-15,0,-6,-5,-14,4}), 0, 13);
    struct sound_seg* s2 = tr_init();
    tr_write(s2, ((int16_t[]){2,19,5,13,-10,-3}), 0, 6);
    struct sound_seg* s3 = tr_init();
    tr_write(s3, ((int16_t[]){-9,-5,20,-12,0,-18,-1,-19,-6}), 0, 9);
    tr_write(s3, ((int16_t[]){11,5,-2,7,-15,8,-13,-1,7}), 0, 9);
    tr_insert(s2, s1, 9, 1, 1);
    tr_write(s2, ((int16_t[]){-20,5,12,0,11,-11}), 0, 6);
    tr_write(s3, ((int16_t[]){-12,-18,-14,-10,5,-9,8,16,-6}), 0, 9);
    tr_delete_range(s3, 5, 3);
    tr_insert(s1, s0, 0, 7, 3);
    tr_write(s1, ((int16_t[]){-10,-6,-7,18,2,-12,12,16,-15,-13,20,-17,17,1}), 0, 14);
    tr_write(s0, ((int16_t[]){17,-16,-11}), 0, 3);
    int16_t FAILING_READ[14];
    tr_read(s1, FAILING_READ, 0, 14);
    for (int i = 0; i < 14; i++) {
        printf("%d ", FAILING_READ[i]);
    }
    printf("\n");
    tr_destroy(s0);
    tr_destroy(s1);
    tr_destroy(s2);
    tr_destroy(s3);
    return 0;
}