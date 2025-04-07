#ifndef WAV_IO_H
#define WAV_IO_H
#include <stdint.h>
#include <stddef.h>

#define OFFSET 40
#define OFFSET_TO_AUDIO_DATA 44

void wav_load(const char* filename, int16_t* dest);
void wav_save(const char* fname, int16_t* src, size_t len);

#endif
