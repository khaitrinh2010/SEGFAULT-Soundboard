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
struct sound_seg_node {
    struct sound_seg_node* next;
    size_t length_of_the_segment;  // How many samples from this segment
    int16_t* audio_data; //start element in the buffer
    bool owns_data; //if this node should free the memory
    int ref_count; //how many segments share this
    struct sound_seg_node* parent_node;
};

struct sound_seg {
    struct sound_seg_node* head;
    size_t total_number_of_segments;
};

struct wav_header {
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
 };

void print_track_metadata(struct sound_seg* track, const char* track_name) {
    printf("{\n");
    printf("  \"track_name\": \"%s\",\n", track_name);
    printf("  \"total_number_of_segments\": %zu,\n", track->total_number_of_segments);
    printf("  \"segments\": [\n");

    struct sound_seg_node* node = track->head;
    int index = 0;
    while (node) {
        printf("    {\n");
        printf("      \"segment_index\": %d,\n", index);
        printf("      \"length_of_the_segment\": %zu,\n", node->length_of_the_segment);
        printf("      \"owns_data\": %s,\n", node->owns_data ? "true" : "false");
        printf("      \"ref_count\": %d,\n", node->ref_count);
        printf("      \"audio_data\": [");
        for (size_t i = 0; i < node->length_of_the_segment; i++) {
            printf("%d", node->audio_data[i]);
            if (i != node->length_of_the_segment - 1) printf(", ");
        }
        printf("],\n");
        if (node->parent_node != NULL) {
            printf("      \"parent\": {\n");
            printf("        \"length_of_the_segment\": %zu,\n", node->parent_node->length_of_the_segment);
            printf("        \"ref_count\": %d\n", node->parent_node->ref_count);
            printf("      }\n");
        } else {
            printf("      \"parent\": null\n");
        }

        printf("    }");
        node = node->next;
        if (node) printf(",");
        printf("\n");
        index++;
    }
    printf("  ]\n");
    printf("}\n");
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

// Create/write a WAV file from buffer
void wav_save(const char* fname, int16_t* src, size_t len){
    // The songs will always be PCM, 16 bits per sample, mono, 8000Hz Sample rate
    FILE *file;
    file = fopen(fname, "wb");
    if (file == NULL){
      printf("Error opening file\n");
      return;
    }
    //WRITE HEADER FIRST
    struct wav_header header;
    strncpy(header.riff, "RIFF", 4);
    strncpy(header.wave, "WAVE", 4);
    strncpy(header.fmt, "fmt ", 4);
    strncpy(header.data, "data", 4);

    header.chunk_size = 16;
    header.format_tag = 1;
    header.num_chans = 1;
    header.sample_rate = 8000;
    header.bits_per_sample = 16;
    header.bytes_per_sample = header.bits_per_sample / 8;
    header.bytes_per_sec = header.bytes_per_sample * header.sample_rate;
    // len is the number of samples
    header.dlength = len * sizeof(int16_t);
    header.flength = header.dlength + OFFSET_TO_AUDIO_DATA;
    fwrite(&header, sizeof(struct wav_header), 1, file);
    fseek(file, OFFSET_TO_AUDIO_DATA, SEEK_SET);
    fwrite(src, sizeof(int16_t), len, file);
    fclose(file);
    return;
}

// Initialize a new sound_seg object
struct sound_seg* tr_init() {
    //When init, only needs 1 segment, more segment will be added in the insertion
    struct sound_seg_node *node = (struct sound_seg_node *)malloc(sizeof(struct sound_seg_node));
    node->next = NULL;
    node->length_of_the_segment = 0;
    node->audio_data = NULL;
    node->ref_count = 0;
    node->owns_data = true;
    node->parent_node = NULL;

    struct sound_seg *seg = (struct sound_seg *)malloc(sizeof(struct sound_seg));
    seg->head = node;
    seg->total_number_of_segments = 1; //initially, there is only 1 segment in a track
    return seg;

}
void tr_destroy(struct sound_seg* obj) {
    struct sound_seg_node* current = obj->head;
    while (current != NULL) {
        struct sound_seg_node* next = current->next;
        if (current->owns_data && current->audio_data != NULL) {
            free(current->audio_data);
        }
        free(current);
        current = next;
    }
    free(obj);
}

// Return the length of the segment
size_t tr_length(struct sound_seg* seg) {
    size_t result = 0;
    struct sound_seg_node *current = seg->head;
    while (current != NULL) {
        result += current->length_of_the_segment;
        current = current->next;
    }
    return result;
}

void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    size_t skipped = 0, written = 0;
    struct sound_seg_node* current = track->head;
    while (current && written < len) {
        if (skipped + current->length_of_the_segment <= pos) {
            skipped += current->length_of_the_segment;
            current = current->next;
            continue;
        }
        size_t start = (pos > skipped) ? (pos - skipped) : 0;
        for (size_t i = start; i < current->length_of_the_segment && written < len; i++) {
            dest[written++] = current->audio_data[i];
        }
        skipped += current->length_of_the_segment;
        current = current->next;
    }
    // for (size_t i = 0; i < len; i++) {
    //     printf("%d ", dest[i]);
    // }
    // printf("\n");
}

void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    // int16_t* test = (int16_t*)malloc(sizeof(int16_t) * tr_length(track));
    // tr_read(track, test, 0, tr_length(track));
    // free(test);
    size_t skipped = 0, written = 0;
    struct sound_seg_node* cur = track->head;

    while (cur && written < len) {
        size_t seg_len = cur->length_of_the_segment;
        size_t seg_start = skipped;
        size_t seg_end = seg_start + seg_len;
        if (seg_end <= pos) {
            skipped = seg_end;
            cur = cur->next;
            continue;
        }
        size_t offset = (pos > skipped) ? (pos - skipped) : 0;
        size_t available = seg_len > offset ? seg_len - offset : 0;
        size_t to_write = (len - written < available) ? (len - written) : available;
        for (size_t i = 0; i < to_write; i++) {
            cur->audio_data[offset + i] = src[written++];
        }
        skipped = seg_end;
        cur = cur->next;
    }
    if (written < len) {
        struct sound_seg_node* last = track->head;
        while (last->next) last = last->next;
        size_t last_offset = 0;
        struct sound_seg_node* walk = track->head;
        while (walk && walk != last) {
            last_offset += walk->length_of_the_segment;
            walk = walk->next;
        }
        size_t rel_pos = (pos > last_offset) ? pos - last_offset : 0;
        size_t offset = pos - last_offset;
        size_t required_len = offset + len;
        if (required_len > last->length_of_the_segment) {
            int16_t* new_data = realloc(last->audio_data, required_len * sizeof(int16_t));
            if (!new_data) return;

            for (size_t i = last->length_of_the_segment; i < required_len; i++) {
                new_data[i] = 0;
            }

            last->audio_data = new_data;
            last->length_of_the_segment = required_len;
        }

        //NOTICEABLE
        for (; written < len; written++) {
            last->audio_data[rel_pos + written] = src[written];
        }
    }

}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    size_t skipped = 0;
    struct sound_seg_node* current = track->head;
    struct sound_seg_node* prev = NULL;
    while (current && len > 0) {
        size_t seg_start = skipped;
        size_t seg_end = seg_start + current->length_of_the_segment;
        if (seg_end <= pos) {
            skipped = seg_end;
            prev = current;
            current = current->next;
            continue;
        }
        if (current->ref_count > 0) return false;
        size_t offset = (pos > skipped) ? pos - skipped : 0;
        size_t deletable = current->length_of_the_segment - offset;
        size_t to_delete = (len < deletable) ? len : deletable;
        for (size_t i = offset; i + to_delete < current->length_of_the_segment; i++) {
            current->audio_data[i] = current->audio_data[i + to_delete];
        }
        current->length_of_the_segment -= to_delete;
        len -= to_delete;
        skipped = seg_end;
        if (current->length_of_the_segment == 0 && current != track->head) {
            if (prev) prev->next = current->next;

            if (current->owns_data && current->audio_data != NULL) {
                free(current->audio_data);
            }
            free(current);
            current = (prev) ? prev->next : track->head;
            track->total_number_of_segments--;
        }
    }
    return true;
}

double compute_cross_relation(int16_t *array1 , int16_t *array2 , size_t len) {
    double sum1 = 0;
    double sum2 = 0;
    int16_t *ptr1 = array1;
    int16_t *ptr2 = array2;
    for (size_t i = 0; i < len; i++) {
        sum1 += (double) (*ptr1)*(*ptr2);
        sum2 += (double) (*ptr2)*(*ptr2);
        ptr1++;
        ptr2++;
    }
    return sum1/sum2;
}

char* tr_identify(const struct sound_seg* target, const struct sound_seg* ad) {
    size_t target_len = tr_length((struct sound_seg*)target);
    size_t ad_len = tr_length((struct sound_seg*)ad);
    if (target_len < ad_len) return strdup("");

    int16_t* target_data = malloc(target_len * sizeof(int16_t));
    int16_t* ad_data = malloc(ad_len * sizeof(int16_t));
    tr_read((struct sound_seg*)target, target_data, 0, target_len);
    tr_read((struct sound_seg*)ad, ad_data, 0, ad_len);
    char* result = malloc(256);
    size_t capacity = 256;
    size_t used = 0;
    result[0] = '\0';
    for (size_t i = 0; i <= target_len - ad_len; i++) {
        double corr = compute_cross_relation(target_data + i, ad_data, ad_len);
        if (corr >= 0.95) {
            char temp[32];
            snprintf(temp, sizeof(temp), "%zu,%zu\n", i, i + ad_len - 1);
            size_t len = strlen(temp);
            if (used + len + 1 > capacity) {
                capacity *= 2;
                result = realloc(result, capacity);
            }
            strcat(result, temp);
            used += len;
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

void read_from_node(struct sound_seg_node* node) {
    printf("  Address: %p\n", &node);
    printf("  Length: %zu\n", node->length_of_the_segment);
    printf("  Owns Data: %s\n", node->owns_data ? "true" : "false");
    printf("  Ref Count: %d\n", node->ref_count);
    printf("  Audio Data: [");
    for (size_t i = 0; i < node->length_of_the_segment; ++i) {
        printf("%d", node->audio_data[i]);
        if (i + 1 != node->length_of_the_segment) {
            printf(", ");
        }
    }
    printf("]\n");
    if (node->parent_node) {
        printf("  Parent: [Length: %zu, Ref Count: %d, Address: %p]\n",
               node->parent_node->length_of_the_segment,
               node->parent_node->ref_count,
               &node->parent_node);
    } else {
        printf("  Parent: NULL\n");
    }
}

void handle_self_insert(struct sound_seg* src, struct sound_seg* dest, size_t destpos, size_t srcpos, size_t len) {
    struct sound_seg_node* src_node = src->head;
    struct sound_seg_node* src_prev = NULL;
    size_t skipped = 0;
    while (src_node && skipped + src_node->length_of_the_segment <= srcpos) {
        skipped += src_node->length_of_the_segment;
        src_prev = src_node;
        src_node = src_node->next;
    }

    size_t src_offset = srcpos - skipped;
    struct sound_seg_node* src_before = NULL;
    struct sound_seg_node* middle = NULL;
    struct sound_seg_node* src_after = NULL;

    if (src_offset > 0) {
        src_before = malloc(sizeof(struct sound_seg_node));
        if (!src_before) return;
        src_before->audio_data = malloc(src_offset * sizeof(int16_t));
        if (!src_before->audio_data) { free(src_before); return; }
        memcpy(src_before->audio_data, src_node->audio_data, src_offset * sizeof(int16_t));
        src_before->length_of_the_segment = src_offset;
        src_before->owns_data = true;
        src_before->ref_count = 0;
        src_before->parent_node = NULL;
        src_before->next = NULL;
    }

    size_t middle_len = len;
    middle = malloc(sizeof(struct sound_seg_node));
    if (!middle) {
        if (src_before) { free(src_before->audio_data); free(src_before); }
        return;
    }
    middle->audio_data = malloc(middle_len * sizeof(int16_t));
    if (!middle->audio_data) {
        if (src_before) { free(src_before->audio_data); free(src_before); }
        free(middle);
        return;
    }
    memcpy(middle->audio_data, src_node->audio_data + src_offset, middle_len * sizeof(int16_t));
    middle->length_of_the_segment = middle_len;
    middle->owns_data = true;
    middle->ref_count = 1;
    middle->parent_node = NULL;
    middle->next = NULL;
    if (src_before) {
        src_before->next = middle;
    }

    size_t remaining = src_node->length_of_the_segment - (src_offset + len);
    if (remaining > 0) {
        src_after = malloc(sizeof(struct sound_seg_node));
        if (!src_after) {
            if (src_before) { free(src_before->audio_data); free(src_before); }
            free(middle->audio_data);
            free(middle);
            return;
        }
        src_after->audio_data = malloc(remaining * sizeof(int16_t));
        if (!src_after->audio_data) {
            if (src_before) { free(src_before->audio_data); free(src_before); }
            free(middle->audio_data);
            free(middle);
            free(src_after);
            return;
        }
        memcpy(src_after->audio_data, src_node->audio_data + src_offset + len, remaining * sizeof(int16_t));
        src_after->length_of_the_segment = remaining;
        src_after->owns_data = true;
        src_after->ref_count = 0;
        src_after->parent_node = NULL;
        src_after->next = NULL;
    }

    // Step 2: Copy the middle part
    struct sound_seg_node* new_node = malloc(sizeof(struct sound_seg_node));
    if (!new_node) {
        if (src_before) { free(src_before->audio_data); free(src_before); }
        if (middle) { free(middle->audio_data); free(middle); }
        if (src_after) { free(src_after->audio_data); free(src_after); }
        return;
    }
    new_node->audio_data = (int16_t*) malloc(len * sizeof(int16_t));
    if (!new_node->audio_data) {
        if (src_before) { free(src_before->audio_data); free(src_before); }
        if (middle) { free(middle->audio_data); free(middle); }
        if (src_after) { free(src_after->audio_data); free(src_after); }
        free(new_node);
        return;
    }
    memcpy(new_node->audio_data, middle->audio_data, len * sizeof(int16_t));
    new_node->length_of_the_segment = len;
    new_node->owns_data = false;
    new_node->ref_count = 0;
    new_node->parent_node = middle;
    new_node->next = NULL;


    if (src_prev) {
        src_prev->next = src_before ? src_before : middle;
    } else {
        src->head = src_before ? src_before : middle;
    }
    if (src_before) {
        src_before->next = middle;
        src->total_number_of_segments++;
    }
    middle->next = src_after ? src_after : src_node->next;
    src->total_number_of_segments++;
    if (src_after) {
        src_after->next = src_node->next;
        src->total_number_of_segments++;
    }

    //FREE
    if (src_node->owns_data && src_node->ref_count == 0) free(src_node->audio_data);
    free(src_node);
    struct sound_seg_node* current = dest->head;
    struct sound_seg_node* prev = NULL;
    skipped = 0;
    while (current && skipped + current->length_of_the_segment <= destpos) {
        skipped += current->length_of_the_segment;
        prev = current;
        current = current->next;
    }
    if (!current) {
        if (prev) prev->next = new_node;
        else dest->head = new_node;
        dest->total_number_of_segments++;
        return;
    }

    size_t dest_offset = destpos - skipped;
    struct sound_seg_node* dest_before = NULL;
    struct sound_seg_node* dest_after = NULL;

    if (dest_offset > 0) {
        dest_before = malloc(sizeof(struct sound_seg_node));
        if (!dest_before) {
            free(new_node->audio_data);
            free(new_node);
            return;
        }
        dest_before->audio_data = malloc(dest_offset * sizeof(int16_t));
        if (!dest_before->audio_data) {
            free(dest_before);
            free(new_node->audio_data);
            free(new_node);
            return;
        }
        memcpy(dest_before->audio_data, current->audio_data, dest_offset * sizeof(int16_t));
        dest_before->length_of_the_segment = dest_offset;
        dest_before->owns_data = true;
        dest_before->ref_count = 0;
        dest_before->parent_node = NULL;
        dest_before->next = NULL;
    }
    size_t dest_remaining = current->length_of_the_segment - dest_offset;
    if (dest_remaining > 0) {
        dest_after = malloc(sizeof(struct sound_seg_node));
        if (!dest_after) {
            if (dest_before) { free(dest_before->audio_data); free(dest_before); }
            free(new_node->audio_data);
            free(new_node);
            return;
        }
        dest_after->audio_data = malloc(dest_remaining * sizeof(int16_t));
        if (!dest_after->audio_data) {
            free(dest_after);
            if (dest_before) { free(dest_before->audio_data); free(dest_before); }
            free(new_node->audio_data);
            free(new_node);
            return;
        }
        memcpy(dest_after->audio_data, current->audio_data + dest_offset, dest_remaining * sizeof(int16_t));
        dest_after->length_of_the_segment = dest_remaining;
        dest_after->owns_data = true;
        dest_after->ref_count = 0;
        dest_after->parent_node = NULL;
        dest_after->next = NULL;
    }

    // read_from_node(prev);
    // read_from_node(dest_before);
    // read_from_node(new_node);
    // read_from_node(dest_after);

    if (prev) prev->next = dest_before ? dest_before : new_node;
    else dest->head = dest_before ? dest_before : new_node;
    if (dest_before) {
        dest_before->next = new_node;
        dest->total_number_of_segments++;
    }
    new_node->next = dest_after ? dest_after : current->next;
    dest->total_number_of_segments++;
    if (dest_after) {
        dest_after->next = current->next;
        dest->total_number_of_segments++;
    }

    //FREE
    if (current->owns_data && current->ref_count == 0) {
        free(current->audio_data);
    }
    free(current);
    print_track_metadata(dest, "dest");
}
void handle_self_insert_overlap(struct sound_seg* src, struct sound_seg* dest, size_t destpos, size_t srcpos, size_t len) {
    // Initial traversal to srcpos (for Case 1)
    struct sound_seg_node* src_node = src->head;
    struct sound_seg_node* src_prev = NULL;
    size_t skipped = 0;

    while (src_node && skipped + src_node->length_of_the_segment <= srcpos) {
        skipped += src_node->length_of_the_segment;
        src_prev = src_node;
        src_node = src_node->next;
    }

    if (!src_node) return;
    size_t src_offset = srcpos - skipped;
    size_t total_len = tr_length(src);
    if (srcpos + len > total_len || destpos > total_len) return;

    // Case 1: srcpos < destpos < srcpos + len
    if (srcpos < destpos && destpos < srcpos + len) {
        size_t before_src_len = src_offset;                // Before srcpos
        size_t src_to_dest_len = destpos - srcpos;     // srcpos to destpos-1
        size_t dest_to_end_len = len - src_to_dest_len; // destpos to end of inserted segment

        // Node 1: Before srcpos (e.g., [20])
        struct sound_seg_node* node1 = NULL;
        if (before_src_len > 0) {
            node1 = malloc(sizeof(struct sound_seg_node));
            if (!node1) return;
            node1->audio_data = malloc(before_src_len * sizeof(int16_t));
            if (!node1->audio_data) { free(node1); return; }
            memcpy(node1->audio_data, src_node->audio_data, before_src_len * sizeof(int16_t));
            node1->length_of_the_segment = before_src_len;
            node1->owns_data = true;
            node1->ref_count = 0;
            node1->parent_node = NULL;
            node1->next = NULL;
        }

        // Node 2: srcpos to destpos-1 (e.g., [-16, 1, -5]), parent
        struct sound_seg_node* node2 = malloc(sizeof(struct sound_seg_node));
        if (!node2) {
            if (node1) { free(node1->audio_data); free(node1); }
            return;
        }
        node2->audio_data = malloc(src_to_dest_len * sizeof(int16_t));
        if (!node2->audio_data) {
            if (node1) { free(node1->audio_data); free(node1); }
            free(node2);
            return;
        }
        memcpy(node2->audio_data, src_node->audio_data + src_offset, src_to_dest_len * sizeof(int16_t));
        node2->length_of_the_segment = src_to_dest_len;
        node2->owns_data = true;
        node2->ref_count = 1; // Parent of node3
        node2->parent_node = NULL;
        node2->next = NULL;

        // Node 3: Inserted srcpos to destpos-1 (e.g., [-16, 1, -5]), child of node2
        struct sound_seg_node* node3 = malloc(sizeof(struct sound_seg_node));
        if (!node3) {
            if (node1) { free(node1->audio_data); free(node1); }
            free(node2->audio_data); free(node2);
            return;
        }
        node3->audio_data = node2->audio_data; // Share with node2
        node3->length_of_the_segment = src_to_dest_len;
        node3->owns_data = false;
        node3->ref_count = 0;
        node3->parent_node = node2;
        node3->next = NULL;

        // Node 4: Inserted destpos to end (e.g., [-15]), parent
        struct sound_seg_node* node4 = malloc(sizeof(struct sound_seg_node));
        if (!node4) {
            if (node1) { free(node1->audio_data); free(node1); }
            free(node2->audio_data); free(node2);
            free(node3);
            return;
        }
        node4->audio_data = malloc(dest_to_end_len * sizeof(int16_t));
        if (!node4->audio_data) {
            if (node1) { free(node1->audio_data); free(node1); }
            free(node2->audio_data); free(node2);
            free(node3);
            free(node4);
            return;
        }
        memcpy(node4->audio_data, src_node->audio_data + src_offset + src_to_dest_len, dest_to_end_len * sizeof(int16_t));
        node4->length_of_the_segment = dest_to_end_len;
        node4->owns_data = false;
        node4->ref_count = 0; // Parent of node5
        node4->parent_node = NULL;
        node4->next = NULL;

        // Node 5: Original at destpos (e.g., [-15]), child of node4
        struct sound_seg_node* node5 = malloc(sizeof(struct sound_seg_node));
        if (!node5) {
            if (node1) { free(node1->audio_data); free(node1); }
            free(node2->audio_data); free(node2);
            free(node3);
            free(node4->audio_data); free(node4);
            return;
        }
        node5->audio_data = node4->audio_data; // Share with node4
        node5->length_of_the_segment = dest_to_end_len;
        node5->owns_data = true;
        node5->ref_count = 1;
        node4->parent_node = node5;
        node5->parent_node = NULL;
        node5->next = NULL;

        if (src_prev) {
            src_prev->next = node1 ? node1 : node2;
        } else {
            src->head = node1 ? node1 : node2;
        }
        if (node1) {
            node1->next = node2;
            src->total_number_of_segments++;
        }
        node2->next = node3;
        node3->next = node4;
        node4->next = node5;
        node5->next = src_node->next;
        src->total_number_of_segments += 4;

        // Free original node
        if (src_node->owns_data) free(src_node->audio_data);
        free(src_node);
    }
    // Case 2: destpos < srcpos < destpos + len
    else if (destpos < srcpos && srcpos < destpos + len) {
        // Traverse to destpos (insertion point)
        struct sound_seg_node* dest_node = src->head;
        struct sound_seg_node* dest_prev = NULL;
        size_t skipped_to_dest = 0;

        while (dest_node && skipped_to_dest + dest_node->length_of_the_segment <= destpos) {
            skipped_to_dest += dest_node->length_of_the_segment;
            dest_prev = dest_node;
            dest_node = dest_node->next;
        }

        if (!dest_node) return;
        size_t dest_offset = destpos - skipped_to_dest;

        // Traverse to srcpos (source data point)
        struct sound_seg_node* src_node_for_copy = src->head;
        size_t skipped_to_src = 0;

        while (src_node_for_copy && skipped_to_src + src_node_for_copy->length_of_the_segment <= srcpos) {
            skipped_to_src += src_node_for_copy->length_of_the_segment;
            src_node_for_copy = src_node_for_copy->next;
        }

        if (!src_node_for_copy) return;
        size_t src_offset_for_copy = srcpos - skipped_to_src;

        // Calculate segment lengths
        size_t before_dest_len = dest_offset;              // Before destpos
        size_t dest_to_src_len = srcpos - destpos;     // destpos to srcpos-1
        size_t src_to_end_len = len - dest_to_src_len; // srcpos to end of inserted segment
        size_t remaining_len = dest_node->length_of_the_segment - dest_offset - dest_to_src_len;

        // Node 1: Before destpos (e.g., [20])
        struct sound_seg_node* node1 = NULL;
        if (before_dest_len > 0) {
            node1 = malloc(sizeof(struct sound_seg_node));
            if (!node1) return;
            node1->audio_data = malloc(before_dest_len * sizeof(int16_t));
            if (!node1->audio_data) { free(node1); return; }
            memcpy(node1->audio_data, src->head->audio_data, before_dest_len * sizeof(int16_t));
            node1->length_of_the_segment = before_dest_len;
            node1->owns_data = true;
            node1->ref_count = 0;
            node1->parent_node = NULL;
            node1->next = NULL;
        }

        // Node 2: Inserted segment from srcpos (e.g., [1, -5, -15]), parent
        struct sound_seg_node* node2 = malloc(sizeof(struct sound_seg_node));
        if (!node2) {
            if (node1) { free(node1->audio_data); free(node1); }
            return;
        }
        node2->audio_data = malloc(len * sizeof(int16_t));
        if (!node2->audio_data) {
            if (node1) { free(node1->audio_data); free(node1); }
            free(node2);
            return;
        }
        memcpy(node2->audio_data, src_node_for_copy->audio_data + src_offset_for_copy, len * sizeof(int16_t));
        node2->length_of_the_segment = len;
        node2->owns_data = true;
        node2->ref_count = 1;
        node2->parent_node = NULL;
        node2->next = NULL;

        // Node 3: destpos to srcpos-1 (e.g., [-16])
        struct sound_seg_node* node3 = malloc(sizeof(struct sound_seg_node));
        if (!node3) {
            if (node1) { free(node1->audio_data); free(node1); }
            free(node2->audio_data); free(node2);
            return;
        }
        node3->audio_data = malloc(dest_to_src_len * sizeof(int16_t));
        if (!node3->audio_data) {
            if (node1) { free(node1->audio_data); free(node1); }
            free(node2->audio_data); free(node2);
            free(node3);
            return;
        }
        memcpy(node3->audio_data, dest_node->audio_data + dest_offset, dest_to_src_len * sizeof(int16_t));
        node3->length_of_the_segment = dest_to_src_len;
        node3->owns_data = true;
        node3->ref_count = 0;
        node3->parent_node = NULL;
        node3->next = NULL;

        // Node 4: Original srcpos to end (e.g., [1, -5, -15]), child of node2
        struct sound_seg_node* node4 = malloc(sizeof(struct sound_seg_node));
        if (!node4) {
            if (node1) { free(node1->audio_data); free(node1); }
            free(node2->audio_data); free(node2);
            free(node3->audio_data); free(node3);
            return;
        }
        node4->audio_data = node2->audio_data; // Share with node2
        node4->length_of_the_segment = len;
        node4->owns_data = false;
        node4->ref_count = 0;
        node4->parent_node = node2;
        node4->next = NULL;

        // Node 5: Remaining after destpos + dest_to_src_len (e.g., [-5, -15])
        struct sound_seg_node* node5 = NULL;
        if (remaining_len > 0) {
            node5 = malloc(sizeof(struct sound_seg_node));
            if (!node5) {
                if (node1) { free(node1->audio_data); free(node1); }
                free(node2->audio_data); free(node2);
                free(node3->audio_data); free(node3);
                free(node4);
                return;
            }
            node5->audio_data = malloc(remaining_len * sizeof(int16_t));
            if (!node5->audio_data) {
                if (node1) { free(node1->audio_data); free(node1); }
                free(node2->audio_data); free(node2);
                free(node3->audio_data); free(node3);
                free(node4);
                free(node5);
                return;
            }
            memcpy(node5->audio_data, dest_node->audio_data + dest_offset + dest_to_src_len, remaining_len * sizeof(int16_t));
            node5->length_of_the_segment = remaining_len;
            node5->owns_data = true;
            node5->ref_count = 0;
            node5->parent_node = NULL;
            node5->next = NULL;
        }

        // Update linked list
        if (dest_prev) {
            dest_prev->next = node1 ? node1 : node2;
        } else {
            src->head = node1 ? node1 : node2;
        }
        if (node1) {
            node1->next = node2;
            src->total_number_of_segments++;
        }
        node2->next = node3;
        node3->next = node4;
        node4->next = node5 ? node5 : dest_node->next;
        src->total_number_of_segments += (node5 ? 4 : 3);

        // Free original node (dest_node)
        if (dest_node->owns_data) free(dest_node->audio_data);
        free(dest_node);
    }

    print_track_metadata(src, "after_overlap_insert");
}

void tr_insert(struct sound_seg* src, struct sound_seg* dest, size_t destpos, size_t srcpos, size_t len) {
    if (!src || !dest || len == 0) return;
    if (src == dest) {
        if (srcpos + len <=destpos || destpos + len <= srcpos) { //no overlap
            handle_self_insert(src, dest, destpos, srcpos, len);
        }
        else {
            handle_self_insert_overlap(src, dest, destpos, srcpos, len);
        }
        return;
    }
    struct sound_seg_node* src_node = src->head;
    size_t skipped_src = 0;
    while (src_node && skipped_src + src_node->length_of_the_segment <= srcpos) {
        skipped_src += src_node->length_of_the_segment;
        src_node = src_node->next;
    }

    if (!src_node) return;
    size_t src_offset = srcpos - skipped_src;
    int16_t* segment_start = src_node->audio_data + src_offset;
    size_t src_remaining = src_node->length_of_the_segment - src_offset;
    if (src_remaining < len) return; // HANDLE LATER
    struct sound_seg_node* src_before = NULL;
    if (src_offset > 0) {
        src_before = malloc(sizeof(struct sound_seg_node));
        src_before->audio_data = malloc(src_offset * sizeof(int16_t));
        memcpy(src_before->audio_data, src_node->audio_data, src_offset * sizeof(int16_t));
        src_before->length_of_the_segment = src_offset;
        src_before->owns_data = true;
        src_before->ref_count = 0;
        src_before->parent_node = NULL;
    }

    struct sound_seg_node* middle_src_segment = malloc(sizeof(struct sound_seg_node));
    middle_src_segment->audio_data = malloc(len * sizeof(int16_t));
    memcpy(middle_src_segment->audio_data, segment_start, len * sizeof(int16_t));
    middle_src_segment->length_of_the_segment = len;
    middle_src_segment->owns_data = true;
    middle_src_segment->ref_count = 1;
    middle_src_segment->parent_node = NULL;

    struct sound_seg_node* src_after = NULL;
    size_t after_len = src_node->length_of_the_segment - (src_offset + len);
    if (after_len > 0) {
        src_after = malloc(sizeof(struct sound_seg_node));
        src_after->audio_data = malloc(after_len * sizeof(int16_t));
        memcpy(src_after->audio_data, segment_start + len, after_len * sizeof(int16_t));
        src_after->length_of_the_segment = after_len;
        src_after->owns_data = true;
        src_after->ref_count = 0;
        src_after->parent_node = NULL;
    }
    struct sound_seg_node* prev_src = NULL;
    struct sound_seg_node* current_src = src->head;
    while (current_src != src_node) {
        prev_src = current_src;
        current_src = current_src->next;
    }
    if (prev_src) {
        prev_src->next = src_before ? src_before : middle_src_segment;
    } else {
        src->head = src_before ? src_before : middle_src_segment;
    }
    if (src_before) {
        src_before->next = middle_src_segment;
        src->total_number_of_segments++;
    }
    middle_src_segment->next = src_after ? src_after : src_node->next;
    src->total_number_of_segments++;
    if (src_after) {
        src_after->next = src_node->next;
        src->total_number_of_segments++;
    }
    //FREE
    free(src_node->audio_data);
    free(src_node);
    struct sound_seg_node* shared_with_dest = malloc(sizeof(struct sound_seg_node));
    shared_with_dest->audio_data = middle_src_segment->audio_data;
    shared_with_dest->length_of_the_segment = len;
    shared_with_dest->owns_data = false;
    shared_with_dest->ref_count = 0;
    shared_with_dest->parent_node = middle_src_segment;
    shared_with_dest->next = NULL;
    middle_src_segment->ref_count++;
    struct sound_seg_node* dest_node = dest->head;
    struct sound_seg_node* prev_dest = NULL;
    size_t skipped_dest = 0;
    while (dest_node && skipped_dest + dest_node->length_of_the_segment <= destpos) {
        skipped_dest += dest_node->length_of_the_segment;
        prev_dest = dest_node;
        dest_node = dest_node->next;
    }
    if (!dest_node) {
        if (!prev_dest) {
            dest->head = shared_with_dest;
        } else {
            prev_dest->next = shared_with_dest;
        }
        dest->total_number_of_segments++;
        return;
    }
    size_t dest_offset = destpos - skipped_dest;
    struct sound_seg_node* dest_before = NULL;
    if (dest_offset > 0) {
        dest_before = malloc(sizeof(struct sound_seg_node));
        dest_before->audio_data = malloc(dest_offset * sizeof(int16_t));
        memcpy(dest_before->audio_data, dest_node->audio_data, dest_offset * sizeof(int16_t));
        dest_before->length_of_the_segment = dest_offset;
        dest_before->owns_data = true;
        dest_before->ref_count = 0;
        dest_before->parent_node = NULL;
    }
    struct sound_seg_node* dest_after = NULL;
    size_t dafter_len = dest_node->length_of_the_segment - dest_offset;
    if (dafter_len > 0) {
        dest_after = malloc(sizeof(struct sound_seg_node));
        dest_after->audio_data = malloc(dafter_len * sizeof(int16_t));
        memcpy(dest_after->audio_data, dest_node->audio_data + dest_offset, dafter_len * sizeof(int16_t));
        dest_after->length_of_the_segment = dafter_len;
        dest_after->owns_data = true;
        dest_after->ref_count = 0;
        dest_after->parent_node = NULL;
    }
    if (prev_dest) {
        prev_dest->next = dest_before ? dest_before : shared_with_dest;
    } else {
        dest->head = dest_before ? dest_before : shared_with_dest;
    }
    if (dest_before) {
        dest_before->next = shared_with_dest;
        dest->total_number_of_segments++;
    }
    shared_with_dest->next = dest_after ? dest_after : dest_node->next;
    dest->total_number_of_segments++;
    if (dest_after) {
        dest_after->next = dest_node->next;
        dest->total_number_of_segments++;
    }

    //FREE
    free(dest_node->audio_data);
    free(dest_node);
}


int main(int argc, char** argv) {
}