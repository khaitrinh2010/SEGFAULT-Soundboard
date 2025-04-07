CC = gcc
LD = ld
CFLAGS = -Wall -Wextra -Werror -Wvla -std=c11 -fPIC -fsanitize=address -g

# Intermediate object files
SRCS = sound_seg.c sound_seg_io.c node_memory_management.c
OBJS = $(SRCS:.c=.o)

# Final merged object file
TARGET = sound_seg.o

# Default rule: build sound_seg.o by merging all .o files
$(TARGET): $(OBJS)
	$(LD) -r $(OBJS) -o $(TARGET)

# Compile each source file to .o
sound_seg.o: sound_seg.c sound_seg.h
	$(CC) $(CFLAGS) -c sound_seg.c -o sound_seg.o

sound_seg_io.o: sound_seg_io.c sound_seg.h
	$(CC) $(CFLAGS) -c sound_seg_io.c -o sound_seg_io.o

node_memory_management.o: node_memory_management.c sound_seg.h
	$(CC) $(CFLAGS) -c node_memory_management.c -o node_memory_management.o

.PHONY: clean
clean:
	rm -f *.o
