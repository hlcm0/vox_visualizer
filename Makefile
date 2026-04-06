CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -std=c99
LDFLAGS ?= -lm

TARGET  = vox_visualizer
SRCDIR  = src
SRCS    = $(SRCDIR)/main.c \
          $(SRCDIR)/parser.c \
          $(SRCDIR)/metrics.c \
          $(SRCDIR)/draw.c \
          $(SRCDIR)/renderer.c

OBJS    = $(SRCS:.c=.o)

INCLUDES = -I$(SRCDIR) -Ivendor

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
