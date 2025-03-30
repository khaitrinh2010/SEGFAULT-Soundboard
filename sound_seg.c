#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

//NOTE: Buffer is int16
#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

//SIGNAL 11: occurs when program attempts to access memory it does not have permission to access
#pragma pack(push, 1)
struct sound_seg_node {
    uint16_t node_id;
    uint16_t parent_id;
    uint16_t cluster_id;
    uint8_t refCount;
    int16_t sample;
    bool isParent;
};
#pragma pack(pop)

struct sound_seg {
    struct sound_seg_node* nodes;
    size_t length;
    size_t capacity;
    uint16_t next_node_id;  // 2 bytes
};

static uint16_t find_root(struct sound_seg_node* nodes, uint16_t node_id) {
    while (nodes[node_id].parent_id != node_id) {
        nodes[node_id].parent_id = nodes[nodes[node_id].parent_id].parent_id;
        node_id = nodes[node_id].parent_id;
    }
    return node_id;
}

// Helper function to union two clusters
static void union_clusters(struct sound_seg_node* nodes, uint16_t root1, uint16_t root2) {
    if (root1 == root2) return;
    
    // Union by rank (using refCount as rank)
    if (nodes[root1].refCount < nodes[root2].refCount) {
        nodes[root1].parent_id = root2;
        nodes[root2].refCount += nodes[root1].refCount;
    } else {
        nodes[root2].parent_id = root1;
        nodes[root1].refCount += nodes[root2].refCount;
    }
}

// Load a WAV file into buffer
void wav_load(const char* filename, int16_t* dest){  //wav file header is discarded
    FILE *file;
    file = fopen(filename, "rb");
    if (file == NULL){
      printf("Error opening file\n");
      return;
    }
    //WAV File: RIFF header, FMT sub-chunk, DATA sub-chunk
    fseek(file, OFFSET, SEEK_SET); // Extract how many bytes in the data audio
    uint32_t number_of_bytes;
    fread(&number_of_bytes, sizeof(uint32_t), 1, file); //read how many number_of_bytes excluding the sample I should read
    fseek(file, OFFSET_TO_AUDIO_DATA, SEEK_SET);
    fread(dest, sizeof(int16_t), number_of_bytes / sizeof(int16_t), file); //Read the data and write to the stream
    fclose(file);
    return;
}

void wav_save(const char* fname, int16_t* src, size_t len) {
    FILE *file = fopen(fname, "wb");
    if (file == NULL) {
        printf("Error opening file\n");
        return;
    }

    // Define a union for the WAV header
    union wav_header {
        struct {
            // RIFF chunk
            char riff[4];
            uint32_t flength;
            char wave[4];
            // fmt sub-chunk
            char fmt[4];
            int32_t chunk_size;
            int16_t format_tag;
            int16_t num_chans;
            int32_t sample_rate;
            int32_t bytes_per_sec;
            int16_t bytes_per_sample;
            int16_t bits_per_sample;
            // data sub-chunk
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
    header.fields.format_tag = 1;         // PCM
    header.fields.num_chans = 1;          // Mono
    header.fields.sample_rate = 8000;     // 8000 Hz
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
    track->nodes = NULL;
    track->length = 0;
    track->capacity = 0;
    track->next_node_id = 0;  // Initialize node ID counter
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
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
    // Initialize new nodes
    for (size_t i = track->capacity; i < new_capacity; i++) {
        track->nodes[i].node_id = track->next_node_id++;
        track->nodes[i].parent_id = i;
        track->nodes[i].cluster_id = i;
        track->nodes[i].refCount = 0;
        track->nodes[i].sample = 0;
        track->nodes[i].isParent = true;
    }
    track->capacity = new_capacity;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || pos >= track->length || len == 0) return;
    size_t available = track->length - pos;
    size_t to_copy = len < available ? len : available;
    for (size_t i = 0; i < to_copy; i++) {
        struct sound_seg_node* node = &track->nodes[pos + i];
        int root_id = find_root(track->nodes, pos + i);
        dest[i] = track->nodes[root_id].sample;
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;
    size_t new_length = pos + len;
    if (new_length > track->capacity) {
        tr_resize(track, new_length * 2);
    }
    if (new_length > track->length) {
        // Initialize new nodes
        for (size_t i = track->length; i < new_length; i++) {
            track->nodes[i].node_id = track->next_node_id++;
            track->nodes[i].parent_id = i;
            track->nodes[i].cluster_id = i;
            track->nodes[i].refCount = 0;
            track->nodes[i].sample = 0;
            track->nodes[i].isParent = true;
        }
        track->length = new_length;
    }
    for (size_t i = 0; i < len; i++) {
        int root_id = find_root(track->nodes, pos + i);
        int16_t new_value = src[i];
        
        // Update all nodes in the cluster to share the same value
        for (size_t j = 0; j < track->length; j++) {
            if (find_root(track->nodes, j) == root_id) {
                track->nodes[j].sample = new_value;
            }
        }
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos >= track->length || len == 0) return false;
    size_t end = pos + len > track->length ? track->length : pos + len;
    
    // First pass: check if we can delete the range
    for (size_t i = pos; i < end; i++) {
        int root_id = find_root(track->nodes, i);
        if (track->nodes[root_id].refCount > 0) {
            return false;  // Cannot delete a node that has references
        }
    }
    
    // Second pass: perform the actual deletion by shifting elements
    for (size_t i = pos; i < track->length - (end - pos); i++) {
        track->nodes[i] = track->nodes[i + (end - pos)];
    }
    
    track->length -= end - pos;
    return true;
}

double compute_cross_correlation(const int16_t* target, const int16_t* ad, size_t len) {
    double sum_product = 0.0;
    double sum_ad_sq = 0.0;
    for (size_t i = 0; i < len; i++) {
        sum_product += (double)target[i] * (double)ad[i];
        sum_ad_sq += (double)ad[i] * (double)ad[i];
    }
    return sum_product / sum_ad_sq; // Normalize by ad's autocorrelation
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
    tr_read((struct sound_seg*)target, target_data, 0, target_len);
    tr_read((struct sound_seg*)ad, ad_data, 0, ad_len);

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

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || srcpos + len > src_track->length || len == 0) return;
    size_t new_length = dest_track->length + len;
    if (new_length > dest_track->capacity) {
        tr_resize(dest_track, new_length * 2);
    }
    
    // Shift existing elements to make space for insertion
    for (size_t i = dest_track->length; i > destpos; i--) {
        track->nodes[i + len - 1] = track->nodes[i - 1];
    }
    
    size_t offset = 0;
    if (src_track == dest_track) {
        offset = len;  // Handle self-insertion case
    }
    
    for (size_t i = 0; i < len; i++) {
        // Find the root of the source node (the parent)
        int src_root = find_root(src_track->nodes, srcpos + i + offset);
        int dest_idx = destpos + i;
        
        // Create new child node in destination track
        dest_track->nodes[dest_idx].node_id = dest_track->next_node_id++;
        dest_track->nodes[dest_idx].parent_id = src_root;  // Point to parent
        dest_track->nodes[dest_idx].cluster_id = src_track->nodes[src_root].cluster_id;  // Same cluster as parent
        dest_track->nodes[dest_idx].refCount = 0;  // New node starts with no references
        dest_track->nodes[dest_idx].sample = src_track->nodes[src_root].sample;  // Copy sample from parent
        dest_track->nodes[dest_idx].isParent = false;  // This is a child node
        
        // Increment parent's reference count
        src_track->nodes[src_root].refCount++;
    }
    
    dest_track->length = new_length;
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
    printf("\n");
    tr_write(s2, ((int16_t[]){-20,5,12,0,11,-11}), 0, 6);
    tr_write(s3, ((int16_t[]){-12,-18,-14,-10,5,-9,8,16,-6}), 0, 9);
    tr_delete_range(s3, 5, 3); //expect return True
    tr_insert(s1, s0, 0, 7, 3);
    tr_write(s1, ((int16_t[]){-10,-6,-7,18,2,-12,12,16,-15,-13,20,-17,17,1}), 0, 14);
    tr_write(s0, ((int16_t[]){17,-16,-11}), 0, 3);
    int16_t FAILING_READ[14];
    tr_read(s1, FAILING_READ, 0, 14);
    //expected [-10  -6  -7  18   2 -12  12  17 -16 -11  20 -17  17   1], actual [-10  -6  -7  18   2 -12  12  17 -16 -13  20 -17  17   1]!
    for (int i = 0; i < 14; i++) {
        printf("%d ", FAILING_READ[i]);
    }
    tr_destroy(s0);
    tr_destroy(s1);
    tr_destroy(s2);
    tr_destroy(s3);
}