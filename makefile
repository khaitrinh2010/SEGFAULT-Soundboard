CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -fPIC -Werror -Wvla -fsanitize=address -g

SRCS = sound_seg.c sound_seg_io.c node_memory_management.c
OBJS = $(SRCS:.c=.o)
REQ_OBJ = sound_seg.o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
$(REQ_OBJ): $(OBJS)
	ld -r $(OBJS) -o $(REQ_OBJ)
.PHONY: clean
clean:
	rm -f *.o $(REQ_OBJ)
