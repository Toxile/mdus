CC=gcc
# Used to use strdup(), then started using other
# C23 features "because we could".  The strdup()
# is gone, but there's no looking back now.
# It's for the best, really.
OPTIONS=-g -std=gnu23 -I include/
LIB=-lpthread -levent -levent_pthreads
TARGET=mdus

SRCS = src/main.c src/mdus.c src/util.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LIB) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CC) $(LIB) $(OPTIONS) -c $< -o $@

clean:
	rm src/*.o
