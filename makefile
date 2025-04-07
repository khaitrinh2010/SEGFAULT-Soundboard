CC = gcc
LD = ld
CFLAGS = -Wall -Wextra -Werror -Wvla -std=c11 -fPIC -fsanitize=address -g

TARGET = sound_seg.o
OBJS = sound_seg_io.o node_memory_management.o sound_seg.o

all: $(TARGET)

sound_seg.o: sound_seg.c sound_seg.h
	$(CC) $(CFLAGS) -c sound_seg.c -o sound_seg.o

sound_seg_io.o: sound_seg_io.c sound_seg.h
	$(CC) $(CFLAGS) -c sound_seg_io.c -o sound_seg_io.o

node_memory_management.o: node_memory_management.c sound_seg.h
	$(CC) $(CFLAGS) -c node_memory_management.c -o node_memory_management.o

$(TARGET): sound_seg.o sound_seg_io.o node_memory_management.o
	$(LD) -r sound_seg.o sound_seg_io.o node_memory_management.o -o $(TARGET)

.PHONY: clean
clean:
	rm -f *.o
