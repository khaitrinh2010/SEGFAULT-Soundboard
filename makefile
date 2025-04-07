CC = gcc
LD = ld
CFLAGS = -Wall -Wextra -Werror -Wvla -std=c11 -fPIC -fsanitize=address -g

SRCS = sound_seg.c sound_seg_io.c node_memory_management.c
OBJS = $(SRCS:.c=.o)

TARGET = sound_seg.o

# Default target
all: $(TARGET)

# Compile each .c file to .o
sound_seg.c.o: sound_seg.c
	$(CC) $(CFLAGS) -c sound_seg.c -o sound_seg.c.o

sound_seg_io.c.o: sound_seg_io.c
	$(CC) $(CFLAGS) -c sound_seg_io.c -o sound_seg_io.c.o

node_memory_management.c.o: node_memory_management.c
	$(CC) $(CFLAGS) -c node_memory_management.c -o node_memory_management.c.o

# Merge all .o files into one object file: sound_seg.o
$(TARGET): sound_seg.c.o sound_seg_io.c.o node_memory_management.c.o
	$(LD) -r sound_seg.c.o sound_seg_io.c.o node_memory_management.c.o -o $(TARGET)

.PHONY: clean
clean:
	rm -f *.o
