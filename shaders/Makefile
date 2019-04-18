SOURCES_VERT := $(wildcard *.vert)
SOURCES_FRAG := $(wildcard *.frag)
SOURCES_COMP := $(wildcard *.comp)
HEADERS = $(SOURCES_VERT:.vert=.vert.inc) $(SOURCES_FRAG:.frag=.frag.inc) $(SOURCES_COMP:.comp=.comp.inc)

%.inc: %
	glslc -mfmt=c -o $@ $<

all: $(HEADERS)

clean:
	rm -f $(HEADERS)

.PHONY: all clean
