##
# Temporary makefile for LC
#
.PHONY: all clean
toolsdir = tools
mkdelim = $(toolsdir)/mkdelim
all: qc
qc: $(wildcard *.c) qcchar.c
	$(CC) -Wall -o $@ $^
qcchar.c: $(mkdelim) qc.h
	$(mkdelim) > qcchar.c
$(mkdelim): $(mkdelim).c qc.h
clean:
	$(RM) -f qcchar.c qc $(mkdelim)
