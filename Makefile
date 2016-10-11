# Makefile for upc2

DESTDIR ?= $(shell pwd)
BINDIR := $(DESTDIR)/bin
OBJDIR := $(DESTDIR)/obj

CFLAGS := -Iinclude -Wall -Werror -g
LDFLAGS :=
LIBS :=

COMMON_SRCS := grouch.c xmodem.c up_bio_serial.c up.c utils.c up_lineend.c
#COMMON_INCLUDES := up_bio.h up_bio_serial.h up.h

LOCATED_OBJS := $(COMMON_SRCS:%.c=$(OBJDIR)/src/%.o)
LOCATED_DEPS := $(LOCATED_OBJS:%.o=%.d)
#LOCATED_INCLUDES := $(COMMON_INCLUDES:%.h=include/%.h)

.PHONY: all
all: $(BINDIR)/upc2

# Pull in dependencies for existing object files
-include $(LOCATED_DEPS)

$(BINDIR)/upc2: $(OBJDIR)/progs/upc2.o $(LOCATED_OBJS)
	-mkdir -p $(BINDIR)
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $^ $(LIBS)

# Compile and generate dependency info
# See http://scottmcpeak.com/autodepend/autodepend.html for how
#  and why this works.  Essentially we post-process GCC's output to
#  create proper Makefile entries for all the dependent include files.
$(OBJDIR)/%.o: %.c
	-mkdir -p $(dir $@)
	$(CC) -o $@ $(CFLAGS) -c $<
	$(CC) -MM $(CFLAGS) $< > ${@:%.o=%.d.tmp}
	@sed -e 's|.*:|$@:|' < ${@:%.o=%.d.tmp} > ${@:%.o=%.d}
	@sed -e 's/.*://' -e 's/\\$$//' < ${@:%.o=%.d.tmp} | fmt -1 | \
	  sed -e 's/^ *//' -e 's/$$/:/' >> ${@:%.o=%.d}
	@rm -f ${@:%.o=%.d.tmp}

clean:
	-rm -rf $(BINDIR) $(OBJDIR)

tidy:
	-rm -rf src/*~ progs/*~

# End file.
