CC = gcc
LD = ld
CFLAGS = -Wall -Wextra -Werror -Wvla -std=c99 -fPIC -fsanitize=address -g
SRCS = sound_seg.c wav_io.c node_management.c
OBJS = sound_seg_tmp.o wav_io.o node_management.o
TARGET = sound_seg.o
all: $(TARGET)
sound_seg_tmp.o: sound_seg.c sound_seg.h node_management.h
	$(CC) $(CFLAGS) -c sound_seg.c -o sound_seg_tmp.o

wav_io.o: wav_io.c wav_io.h
	$(CC) $(CFLAGS) -c wav_io.c -o wav_io.o

node_management.o: node_management.c node_management.h
	$(CC) $(CFLAGS) -c node_management.c -o node_management.o

$(TARGET): $(OBJS)
	$(LD) -r $(OBJS) -o $(TARGET)

.PHONY: clean
clean:
	rm -f *.o
