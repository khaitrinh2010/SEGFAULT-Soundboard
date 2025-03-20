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

struct sound_seg {
    //TODO
    // Attributes of a sound_seg (track)
    size_t start_pos; //start position in the buffer
    size_t length; // length of the track in the buffer
    int16_t *ptr; //pointer to the buffer array
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
    struct sound_seg* seg = (struct sound_seg*)malloc(sizeof(struct sound_seg)); //Alocate heap memory
    if (seg == NULL){
      return NULL;
    }
    seg->start_pos = 0;
    seg->length = 0;
    seg->ptr = NULL;
    return seg;
}

// Destroy a sound_seg object and free all allocated memory
void tr_destroy(struct sound_seg* obj) {
    free(obj->ptr);
    free(obj);
    return;
}

// Return the length of the segment
size_t tr_length(struct sound_seg* seg) {
    return seg->length;
}

// Read len elements from position pos into dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    //copies len audio samples from position pos in the track data structure to a buffer dest, the song is 2 bytes persample
    for (size_t i = 0; i < len; i++) {
        dest[i] = track->ptr[track->start_pos + pos + i];
    }
    return;
}

// Write len elements from src into position pos of the track
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    if (track->ptr == NULL){
      track->ptr = (int16_t*)malloc((pos + len) * sizeof(int16_t));
      track->length = pos + len;
    }
    if (pos + len > track->length){
      track->ptr = (int16_t*)realloc(track->ptr, (len + pos) * sizeof(int16_t));
      track->length = pos + len;
    }
    size_t start_pos_in_the_buffer = track->start_pos;
    size_t end_pos_in_the_buffer = start_pos_in_the_buffer + track->length;
    size_t start_in_the_buffer = start_pos_in_the_buffer + pos;
    for (size_t i = 0; i < len; i++) {
      track->ptr[track->start_pos + pos + i] = src[i];
    }
    for (size_t i = track->start_pos; i < end_pos_in_the_buffer; i++) {
        printf("%c", track->ptr[i]);
    }
    printf("\n");
    return;
}

// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    size_t start_pos_in_track = track->start_pos + pos;
    for (size_t i = start_pos_in_track; i < track->start_pos + track->length; i++) {
        track->ptr[i] = track->ptr[i + len];
    }
    track->length -= len;
    return true;
}

// Returns a string containing <start>,<end> ad pairs in target
char* tr_identify(struct sound_seg* target, struct sound_seg* ad){
    size_t temp_size = 256;
    char* ptr = (char*)malloc(temp_size);

    if (ptr == NULL) {
        return NULL;
    }
    ptr[0] = '\0';
    size_t current_length = 0;
    for (int i = 0; i <= target->length - ad->length; i++) {
        size_t index_in_target = 0;
        int found = 1;
        for (int j = i; j < i + ad->length; j++) {
            if (target->ptr[j] == ad->ptr[index_in_target++]) {
                continue;
            }
            else {
                found = 0;
                break;
            }
        }
        if (found == 1) { //found
            size_t last_index = i + ad->length - 1;
            char temp[32];
            snprintf(temp, sizeof(temp), "%zu,%zu\n", i, last_index);
            size_t length = strlen(temp);
            if (current_length + length + 1 >= temp_size) {
                temp_size *= 2;
                char* temp_ptr = (char*)realloc(ptr, temp_size);
                if (!temp_ptr) {
                    free(ptr);
                    return NULL;
                }
                ptr = temp_ptr;
            }
            strcat(ptr, temp);
            current_length += length;
        }
    }
    if (current_length == 0) {
        free(ptr);
        return strdup("");
    }
    return ptr;
}

// Insert a portion of src_track into dest_track at position destpos
void tr_insert(struct sound_seg* src_track,
            struct sound_seg* dest_track,
            size_t destpos, size_t srcpos, size_t len) {
    return;
}

int main(void){
  //wav_load("sound.wav", dest);
    struct sound_seg* s0 = tr_init();
    tr_write(s0, ((int16_t[]){}), 0, 0);
    struct sound_seg* s1 = tr_init();
    tr_write(s1, ((int16_t[]){-5,-10,3}), 0, 3);
    tr_write(s1, ((int16_t[]){-1,8,8}), 0, 3);
    tr_write(s1, ((int16_t[]){12,10,9}), 0, 3);
    tr_write(s1, ((int16_t[]){10,4,-3}), 0, 3);
    tr_write(s1, ((int16_t[]){17,-18,-5}), 0, 3);
    tr_delete_range(s1, 0, 1); //expect return True
    int16_t FAILING_READ[2];
    tr_read(s1, FAILING_READ, 0, 2);
    //expected [-18  -5], actual [-18 -18]!
    tr_destroy(s0);
    tr_destroy(s1);
    return 0;
}

