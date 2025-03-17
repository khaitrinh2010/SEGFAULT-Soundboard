#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

//NOTE: Buffer is int16
#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

struct sound_seg {
    //TODO
    // Attributes of a sound_seg (track)
    int start_pos; //start position in the buffer
    int length; // length of the track in the buffer
    int16_t *ptr; //pointer to the buffer array
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
    free(obj);
    return;
}

// Return the length of the segment
size_t tr_length(struct sound_seg* seg) {
    return (size_t)-1;
}

// Read len elements from position pos into dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    return;
}

// Write len elements from src into position pos
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    return;
}

// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    return true;
}

// Returns a string containing <start>,<end> ad pairs in target
char* tr_identify(struct sound_seg* target, struct sound_seg* ad){
    return NULL;
}

// Insert a portion of src_track into dest_track at position destpos
void tr_insert(struct sound_seg* src_track,
            struct sound_seg* dest_track,
            size_t destpos, size_t srcpos, size_t len) {
    return;
}

int main(void){
  //wav_load("sound.wav", dest);
  return 0;
}

