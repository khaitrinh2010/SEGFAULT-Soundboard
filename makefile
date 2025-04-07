CC = gcc
LD = ld

CFLAGS = -Wall -Wextra -Werror -Wvla -std=c99 -fPIC -fsanitize=address -g

SRCS = sound_seg.c sound_seg_io.c node_memory_management.c
OBJS = sound_seg_tmp.o sound_seg_io.o node_management.o
TARGET = sound_seg.o

all: $(TARGET)

sound_seg_tmp.o: sound_seg.c sound_seg.h
	$(CC) $(CFLAGS) -c sound_seg.c -o sound_seg_tmp.o

sound_seg_io.o: sound_seg_io.c file_io.h
	$(CC) $(CFLAGS) -c sound_seg_io.c -o sound_seg_io.o

node_management.o: node_memory_management.c node_management.h
	$(CC) $(CFLAGS) -c node_memory_management.c -o node_management.o

$(TARGET): $(OBJS)
	$(LD) -r $(OBJS) -o $(TARGET)

.PHONY: clean
clean:
	rm -f *.o
