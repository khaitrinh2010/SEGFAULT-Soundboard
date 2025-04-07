CC = gcc
LD = ld
CFLAGS = -Wall -Wextra -Werror -Wvla -std=c11 -fPIC -fsanitize=address -g

SRCS = sound_seg.c sound_seg_io.c node_memory_management.c
OBJS = $(SRCS:.c=.o)
TARGET = sound_seg.o

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(LD) -r $(OBJS) -o $(TARGET)

.PHONY: clean
clean:
	rm -f *.o $(TARGET)
