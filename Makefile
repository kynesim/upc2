# Makefile for upc2

DESTDIR ?= $(shell pwd)
BINDIR := $(DESTDIR)/bin
OBJDIR := $(DESTDIR)/obj

CFLAGS := -Iinclude -Wall -Werror -g
LDFLAGS :=
LIBS :=

COMMON_SRCS := grouch.c xmodem.c up_bio_serial.c up.c utils.c
#COMMON_INCLUDES := up_bio.h up_bio_serial.h up.h

LOCATED_OBJS := $(COMMON_SRCS:%.c=$(OBJDIR)/src/%.o)
#LOCATED_INCLUDES := $(COMMON_INCLUDES:%.h=include/%.h)

.PHONY: all
all: $(BINDIR)/upc2

$(BINDIR)/upc2: $(OBJDIR)/progs/upc2.o $(LOCATED_OBJS)
	-mkdir -p $(BINDIR)
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $^ $(LIBS)

$(OBJDIR)/%.o: %.c
	-mkdir -p $(dir $@)
	$(CC) -o $@ $(CFLAGS) -c $<

clean:
	-rm -rf $(BINDIR) $(OBJDIR)

# End file.
