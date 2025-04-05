CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -fPIC -Werror -Wvla -fsanitize=address -g
REQ_OBJ = sound_seg.o
SRCS = sound_seg.c sound_seg_io.c node_memory_management.c
OBJS = $(SRCS:.c=.o)
$(REQ_OBJ): $(SRCS) sound_seg.h
	$(CC) $(CFLAGS) -c combination.c -o $(REQ_OBJ)
main: main.c $(OBJS)
	$(CC) $(CFLAGS) main.c $(OBJS) -o main
clean:
	rm -f *.o main

