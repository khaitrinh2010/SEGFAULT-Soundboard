#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

static uint16_t next_cluster_id = 0;
static uint16_t next_node_id = 0;

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
    struct sound_seg_node** nodes;
    size_t length;
    size_t capacity;
    struct sound_seg* next;
};

static struct sound_seg* all_tracks = NULL;

static void update_cluster(uint16_t cluster_id, int16_t new_sample) {
    struct sound_seg* track = all_tracks;
    while (track != NULL) {
        for (size_t i = 0; i < track->length; i++) {
            if (track->nodes[i]->cluster_id == cluster_id) {
                track->nodes[i]->sample = new_sample;
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
    track->next = all_tracks;
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
        track->nodes[i]->node_id = next_node_id;
        track->nodes[i]->parent_id = next_cluster_id;
        track->nodes[i]->cluster_id = next_cluster_id++;
        track->nodes[i]->ref_count = 1;
        track->nodes[i]->sample = 0;
        track->nodes[i]->is_parent = false;
        next_node_id++;
    }
    track->capacity = new_capacity;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || pos >= track->length || len == 0) return;
    size_t end = pos + len > track->length ? track->length : pos + len;
    for (size_t i = pos; i < end; i++) {
        dest[i - pos] = track->nodes[i]->sample;
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
            track->nodes[i]->node_id = next_node_id;
            track->nodes[i]->parent_id = next_node_id;
            track->nodes[i]->cluster_id = next_cluster_id++;
            track->nodes[i]->ref_count = 1;
            track->nodes[i]->sample = 0;
            track->nodes[i]->is_parent = false;
            next_node_id++;
        }
        track->length = new_length;
    }
    for (size_t i = 0; i < len; i++) {
        uint16_t cluster_id = track->nodes[pos + i]->cluster_id;
        update_cluster(cluster_id, src[i]);
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos >= track->length || len == 0) return false;
    size_t end = pos + len > track->length ? track->length : pos + len;
    for (size_t i = pos; i < end; i++) {
        if (track->nodes[i]->is_parent && track->nodes[i]->ref_count > 1) {
            return false;
        }
    }

    //Other cases, every nodes can be deleted now
    for (size_t i = pos; i < track->length - len; i++) {
        if (track->nodes[i]->parent_id != track->nodes[i]->node_id) {
            struct sound_seg *head = all_tracks;
            uint16_t parent_id = track->nodes[i]->parent_id;
            int found = 0;
            while (head != NULL) {
                for (size_t j = 0; j < head->length; j++) {
                    if (head->nodes[j]->node_id == parent_id) {
                        head->nodes[j]->ref_count--;
                        found = 1;
                    }
                }
                if (found) break;
                head = head->next;
            }
        }
        track->nodes[i] = track->nodes[i + len];
    }
    for (size_t i = track->length - len; i < track->length; i++) {
        free(track->nodes[i]); //free the rest
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
        dest_track->nodes[i + len - 1] = dest_track->nodes[i - 1]; //shift
    }
    dest_track->length = new_length;
    for (size_t i = 0; i < len; i++) {
        uint16_t src_idx = srcpos + i;
        uint16_t dest_idx = destpos + i;
        uint16_t src_cluster_id = src_track->nodes[src_idx]->cluster_id;
        dest_track->nodes[dest_idx]->node_id = next_node_id;
        dest_track->nodes[dest_idx]->parent_id = next_node_id;
        dest_track->nodes[dest_idx]->cluster_id = src_cluster_id;
        dest_track->nodes[dest_idx]->ref_count = 1;
        dest_track->nodes[dest_idx]->sample = src_track->nodes[src_idx]->sample;
        dest_track->nodes[dest_idx]->is_parent = false;
        src_track->nodes[src_idx]->ref_count++;
        src_track->nodes[src_idx]->is_parent = true;
    }
}

int main(int argc, char** argv) {
    struct sound_seg* s0 = tr_init();
    tr_write(s0, ((int16_t[]){-5,-17,-4,-20,-14,4,-4,-14,-11,18,5,-2,19,-3,8,-6,-1,1,11,-13,-14,-6,-12,1,12,-17,-5,-19,-5,1,4,6,9,-20,18,17,-1,-2,10,14,5,-18,16,-8,17,4,10,18,8,17,5,17,-10,12,-10,15,-5,-17,16,4,-4,15,16,13,13,0,-5,-18,-5,-8,-8,15,0,1,20,-18,17,19,-9,15,0,-1,-14,20,-14,-16,18,-13,18,-4,-14,2,-1,-17,20,-7,3,0,-10,1,8,-14,-20,-15,13,-11,-3,20,-19,6,-13,-3,-1,15,-11,1,19,19,-16,8}), 0, 120);
    struct sound_seg* s1 = tr_init();
    tr_write(s1, ((int16_t[]){-2,-7,-1,19,8,-15,5,20,11,0,-8,-13,-15,-9,-5,-9,9,16,-8,-13,0,15,-10,-16,-4,-18,12,7,-13,18,-8,-15,11,12,0,6,8,9,-18,3,0,5,11,19,8,8,-17,18,-10,-4,1,12,13,4,6,-14,-16,17,20,-9,-14,-16,18,17,-10,-17,-6,-17,-11,-4,9,-20,18,12,-15,-13,-9,0,-1,-15,-19,-13,-10,6,13,16,0,10,-5,15,14,-18,-8,-19,15,-10,-17,13,17,9,-9,16,-6,-15,17,13,12,-3,10,-17,14,-9,-9,-2,-11,-10,0,-7,10,-17}), 0, 120);
    struct sound_seg* s2 = tr_init();
    tr_write(s2, ((int16_t[]){10,10,7,20,-8,20,16,-2,9,-19,-4,-19,10,-12,-11,20,-12,-14,-9,-5,11,-4,1,6,-20,10,9,17,-3,15,-19,9,-14,-10,-9,20,8,-15,-3,-16,18,-5,20,16,-8,-20,2,4,-15,-17,11,2,5,-6,3,2,-3,5,10,0,0,-11,6,3,-3,11,-5,-16,14,13,-17,-1,-6,-8,-3,15,5,-3,13,-1,7,2,5,17,-19,20,-12,-3,-6,-18,11,17,-16,-18,-16,19,-20,-14,-2,-14,1,3,-11,16,-11,4,14,20,-16,16,-15,20,4,-8,1,8,3,13,-7,14}), 0, 120);
    tr_delete_range(s0, 103, 13);
    tr_write(s0, ((int16_t[]){14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14}), 0, 107);
    tr_insert(s1, s1, 76, 62, 34);
    tr_write(s0, ((int16_t[]){-3,-3,-3,-3,-3,10,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-8,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,16,-8,-3,-3,-3,-3,-3,-3,-3,-3,-17,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-5,-3,-3,-3,-3,-3,-3,-3,-3,-3}), 0, 107);
    tr_write(s2, ((int16_t[]){10,-19,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,-9,10,10,10,10,10,10,10,10,10,-3,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,-16,10,-8,10,10,10,10,10,10,10,10,10,10}), 0, 120);
    tr_write(s1, ((int16_t[]){-19,-19,-19,3,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-6,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,15,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-1,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-18,-19,-19,-19,-19,16,-10,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,17,6,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,11,-19,-12,-11,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19}), 0, 154);
    int16_t FAILING_READ[154];
    int16_t expected[] = {
        -19, -19, -19, 3, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19,
        -6, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19,
        -19, -19, -19, -19, -19, -19, 15, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19,
        -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -18, -19, -19,
        -19, -19, 16, -10, -19, -19, -19, -19, -19, -19, -19, -18, -19, -19, -19, -19, 16,
        -10, -19, -19, 17, 6, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, 11, -19,
        -12, -11, -19, -19, -19, -19, 17, 6, -19, -19, -19, -19, -19, -19, -19, -19, -19,
        -19, 11, -19, -12, -11, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19,
        19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19, -19
    };
    tr_read(s1, FAILING_READ, 0, 154);
    for (int i = 0; i < 154; i++) {
        if (FAILING_READ[i] != expected[i]) {
            printf("FAILURE: Expected %d, got %d at index %d\n", expected[i], FAILING_READ[i], i);
        }
    }
    printf("\n");
    tr_destroy(s0);
    tr_destroy(s1);
    tr_destroy(s2);
    return 0;
}