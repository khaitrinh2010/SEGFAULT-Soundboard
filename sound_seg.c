#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Node structure for the linked list representing a track segment
struct node {
    int16_t* data;          // Pointer to the actual audio samples (shared or owned)
    size_t len;             // Length of this segment
    bool is_shared;         // True if this is a shared reference (parent/child)
    struct node* parent;    // Pointer to parent node if this is a child
    size_t ref_count;       // Number of children referencing this node
    struct node* next;      // Next segment in the track
};

// Track structure
struct sound_seg {
    struct node* head;      // Head of the linked list of segments
    size_t total_len;       // Total length of the track in samples
};

// WAV file interaction (simplified for this example, actual implementation depends on WAV format)
void wav_load(const char* fname, int16_t* dest) {
    // Placeholder: In a real scenario, read WAV file into dest, skipping header
    FILE* fp = fopen(fname, "rb");
    if (!fp) return;
    fseek(fp, 44, SEEK_SET); // Skip WAV header (assuming 44-byte header)
    fread(dest, sizeof(int16_t), 8000, fp); // Example: read 8000 samples
    fclose(fp);
}

void wav_save(const char* fname, const int16_t* src, size_t len) {
    // Placeholder: Write src to a WAV file with a proper header
    FILE* fp = fopen(fname, "wb");
    if (!fp) return;
    // Write a minimal WAV header (PCM, 16-bit, mono, 8000Hz)
    uint32_t sample_rate = 8000;
    uint32_t byte_rate = sample_rate * 2;
    uint32_t data_size = len * 2;
    fwrite("RIFF", 1, 4, fp);
    uint32_t chunk_size = 36 + data_size;
    fwrite(&chunk_size, 4, 1, fp);
    fwrite("WAVEfmt ", 1, 8, fp);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, fp);
    uint16_t audio_format = 1; // PCM
    fwrite(&audio_format, 2, 1, fp);
    uint16_t num_channels = 1; // Mono
    fwrite(&num_channels, 2, 1, fp);
    fwrite(&sample_rate, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    uint16_t block_align = 2;
    fwrite(&block_align, 2, 1, fp);
    uint16_t bits_per_sample = 16;
    fwrite(&bits_per_sample, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);
    fwrite(src, sizeof(int16_t), len, fp);
    fclose(fp);
}

// Track management functions
struct sound_seg* tr_init(void) {
    struct sound_seg* track = malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    track->head = NULL;
    track->total_len = 0;
    return track;
}

void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    struct node* current = track->head;
    while (current) {
        struct node* next = current->next;
        if (!current->is_shared || current->ref_count == 0) {
            free(current->data); // Free data only if not shared or no children
        }
        free(current);
        current = next;
    }
    free(track);
}

size_t tr_length(struct sound_seg* track) {
    return track ? track->total_len : 0;
}
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !track->head || pos >= track->total_len) return;
    size_t offset = 0;
    struct node* current = track->head;

    // Find the node containing pos
    while (current && offset + current->len <= pos) {
        offset += current->len;
        current = current->next;
    }
    if (!current) return;

    size_t dest_idx = 0;
    size_t remaining = len;

    size_t start = (pos > offset) ? (pos - offset) : 0;

    while (current && remaining > 0) {
        size_t available = current->len - start;
        size_t copy_len = available < remaining ? available : remaining;

        memcpy(dest + dest_idx, current->data + start, copy_len * sizeof(int16_t));
        dest_idx += copy_len;
        remaining -= copy_len;

        offset += current->len;
        current = current->next;
        start = 0; // After the first node, start at the beginning of each subsequent node
    }
}

void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src) return;
    if (track->total_len < pos + len) track->total_len = pos + len;

    if (!track->head) {
        struct node* new_node = malloc(sizeof(struct node));
        new_node->data = malloc((pos  + len) * sizeof(int16_t));
        for (size_t i = 0; i < pos; i++) new_node->data[i] = 0;
        memcpy(new_node->data + pos, src, len * sizeof(int16_t));
        new_node->len = len;
        new_node->is_shared = false;
        new_node->parent = NULL;
        new_node->ref_count = 0;
        new_node->next = NULL;
        track->head = new_node;
        return;
    }

    struct node* prev = NULL;
    struct node* current = track->head;
    size_t offset = 0;
    while (current && offset + current->len <= pos) {
        offset += current->len;
        prev = current;
        current = current->next;
    }

    if (offset == pos) {
        struct node* new_node = malloc(sizeof(struct node));
        new_node->data = malloc(len * sizeof(int16_t));
        memcpy(new_node->data, src, len * sizeof(int16_t));
        new_node->len = len;
        new_node->is_shared = false;
        new_node->parent = NULL;
        new_node->ref_count = 0;
        new_node->next = current;
        if (prev) prev->next = new_node;
        else track->head = new_node;
    } else {
        // Split the node if necessary
        size_t split_pos = pos - offset;
        struct node* left = malloc(sizeof(struct node));
        left->data = malloc(split_pos * sizeof(int16_t));
        memcpy(left->data, current->data, split_pos * sizeof(int16_t));
        left->len = split_pos;
        left->is_shared = current->is_shared;
        left->parent = current->parent;
        left->ref_count = 0;
        left->next = NULL;

        struct node* new_node = malloc(sizeof(struct node));
        new_node->data = malloc(len * sizeof(int16_t));
        memcpy(new_node->data, src, len * sizeof(int16_t));
        new_node->len = len;
        new_node->is_shared = false;
        new_node->parent = NULL;
        new_node->ref_count = 0;

        struct node* right = malloc(sizeof(struct node));
        right->data = malloc((current->len - split_pos) * sizeof(int16_t));
        memcpy(right->data, current->data + split_pos, (current->len - split_pos) * sizeof(int16_t));
        right->len = current->len - split_pos;
        right->is_shared = current->is_shared;
        right->parent = current->parent;
        right->ref_count = 0;

        left->next = new_node;
        new_node->next = right;
        right->next = current->next;

        if (current->is_shared && current->parent) current->parent->ref_count--;

        if (prev) prev->next = left;
        else track->head = left;
        free(current->data);
        free(current);
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos + len > track->total_len) return false;
    struct node* prev = NULL;
    struct node* current = track->head;
    size_t offset = 0;

    while (current && offset + current->len <= pos) {
        offset += current->len;
        prev = current;
        current = current->next;
    }

    if (!current) return false;

    if (current->ref_count > 0) return false; // Cannot delete if it has children

    if (offset == pos && current->len == len) {
        if (prev) prev->next = current->next;
        else track->head = current->next;
        if (!current->is_shared || current->ref_count == 0) free(current->data);
        free(current);
        track->total_len -= len;
        return true;
    }

    size_t start = pos - offset;
    if (start > 0) {
        struct node* left = malloc(sizeof(struct node));
        left->data = malloc(start * sizeof(int16_t));
        memcpy(left->data, current->data, start * sizeof(int16_t));
        left->len = start;
        left->is_shared = current->is_shared;
        left->parent = current->parent;
        left->ref_count = 0;
        left->next = NULL;

        size_t remaining = current->len - start - len;
        if (remaining > 0) {
            struct node* right = malloc(sizeof(struct node));
            right->data = malloc(remaining * sizeof(int16_t));
            memcpy(right->data, current->data + start + len, remaining * sizeof(int16_t));
            right->len = remaining;
            right->is_shared = current->is_shared;
            right->parent = current->parent;
            right->ref_count = 0;
            right->next = current->next;
            left->next = right;
        } else {
            left->next = current->next;
        }

        if (prev) prev->next = left;
        else track->head = left;
        if (current->is_shared && current->parent) current->parent->ref_count--;
        free(current->data);
        free(current);
    } else {
        size_t end = start + len;
        if (end < current->len) {
            struct node* right = malloc(sizeof(struct node));
            right->data = malloc((current->len - end) * sizeof(int16_t));
            memcpy(right->data, current->data + end, (current->len - end) * sizeof(int16_t));
            right->len = current->len - end;
            right->is_shared = current->is_shared;
            right->parent = current->parent;
            right->ref_count = 0;
            right->next = current->next;

            current->len = start;
            current->next = right;
        } else {
            if (prev) prev->next = current->next;
            else track->head = current->next;
            if (!current->is_shared || current->ref_count == 0) free(current->data);
            free(current);
        }
    }
    track->total_len -= len;
    return true;
}

char* tr_identify(const struct sound_seg* target, const struct sound_seg* ad) {
    size_t target_len = tr_length((struct sound_seg*)target);
    size_t ad_len = tr_length((struct sound_seg*)ad);
    if (target_len < ad_len) return strdup("");

    int16_t* target_data = malloc(target_len * sizeof(int16_t));
    int16_t* ad_data = malloc(ad_len * sizeof(int16_t));
    tr_read((struct sound_seg*)target, target_data, 0, target_len);
    tr_read((struct sound_seg*)ad, ad_data, 0, ad_len);

    // Compute autocorrelation of ad at zero delay (reference)
    double auto_corr = 0;
    for (size_t i = 0; i < ad_len; i++) {
        auto_corr += (double)ad_data[i] * ad_data[i];
    }

    char* result = malloc(1);
    result[0] = '\0';
    size_t result_len = 1;

    for (size_t i = 0; i <= target_len - ad_len; i++) {
        double cross_corr = 0;
        for (size_t j = 0; j < ad_len; j++) {
            cross_corr += (double)target_data[i + j] * ad_data[j];
        }
        if (cross_corr >= 0.95 * auto_corr) {
            char buffer[32];
            snprintf(buffer, 32, "%zu,%zu\n", i, i + ad_len - 1);
            size_t new_len = strlen(buffer);
            result = realloc(result, result_len + new_len);
            strcpy(result + result_len - 1, buffer);
            result_len += new_len - 1;
        }
    }

    free(target_data);
    free(ad_data);
    if (result_len > 1) result[result_len - 1] = '\0'; // Remove trailing newline
    return result;
}

void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
              size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || srcpos + len > src_track->total_len) return;

    // Find source node
    struct node* src_current = src_track->head;
    size_t src_offset = 0;
    while (src_current && src_offset + src_current->len <= srcpos) {
        src_offset += src_current->len;
        src_current = src_current->next;
    }
    size_t src_start = srcpos - src_offset;

    // Create a new node for the inserted portion
    struct node* insert_node = malloc(sizeof(struct node));
    insert_node->data = src_current->data + src_start;
    insert_node->len = len;
    insert_node->is_shared = true;
    insert_node->parent = src_current;
    insert_node->ref_count = 0;
    insert_node->next = NULL;
    src_current->ref_count++;

    // Insert into destination track
    if (!dest_track->head || destpos == 0) {
        insert_node->next = dest_track->head;
        dest_track->head = insert_node;
        dest_track->total_len += len;
    } else {
        struct node* prev = NULL;
        struct node* current = dest_track->head;
        size_t offset = 0;
        while (current && offset + current->len <= destpos) {
            offset += current->len;
            prev = current;
            current = current->next;
        }

        if (offset == destpos) {
            insert_node->next = current;
            if (prev) prev->next = insert_node;
            else dest_track->head = insert_node;
        } else {
            size_t split_pos = destpos - offset;
            struct node* left = malloc(sizeof(struct node));
            left->data = malloc(split_pos * sizeof(int16_t));
            memcpy(left->data, current->data, split_pos * sizeof(int16_t));
            left->len = split_pos;
            left->is_shared = current->is_shared;
            left->parent = current->parent;
            left->ref_count = 0;

            struct node* right = malloc(sizeof(struct node));
            right->data = malloc((current->len - split_pos) * sizeof(int16_t));
            memcpy(right->data, current->data + split_pos, (current->len - split_pos) * sizeof(int16_t));
            right->len = current->len - split_pos;
            right->is_shared = current->is_shared;
            right->parent = current->parent;
            right->ref_count = 0;
            right->next = current->next;

            left->next = insert_node;
            insert_node->next = right;
            if (prev) prev->next = left;
            else dest_track->head = left;

            if (current->is_shared && current->parent) current->parent->ref_count--;
            free(current->data);
            free(current);
        }
        dest_track->total_len += len;
    }
}

int main(void) {

}