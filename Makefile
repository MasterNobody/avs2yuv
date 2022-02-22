# Makefile

CC=gcc
LD=gcc -o 
CC_O=-o $@
DEPMM=-MM -g0
DEPMT=-MT

#EXE=.exe
EXE=

CFLAGS += -I. -std=gnu99 -O3 -ffast-math
LDFLAGS += -ldl

all: default
default: cli

SRCS = avs2yuv.c
OBJS =

OBJS += $(SRCS:%.c=%.o)

.PHONY: all default clean cli

cli: avs2yuv$(EXE)

ifneq ($(EXE),)
.PHONY: avs2yuv
avs2yuv: avs2yuv$(EXE)
endif

avs2yuv$(EXE): .depend $(OBJS)
	$(LD)$@ $(OBJS) $(LDFLAGS)

$(OBJS): .depend

%.o: %.c
	$(CC) $(CFLAGS) -c $< $(CC_O)

.depend:
	@rm -f .depend
	@echo 'dependency file generation...'
	@$(foreach SRC, $(SRCS), $(CC) $(CFLAGS) $(SRC) $(DEPMT) $(SRC:%.c=%.o) $(DEPMM) 1>> .depend;)

depend: .depend
ifneq ($(wildcard .depend),)
include .depend
endif

clean:
	rm -f $(OBJS) .depend
	rm -f avs2yuv avs2yuv$(EXE)
