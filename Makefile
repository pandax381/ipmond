PROGRAM = ipmond

OBJECTS = ipmond.o

CFLAGS := $(CFLAGS) -g -O2 -W -Wall -Wno-unused-parameter -Wno-deprecated-declarations

.SUFFIXES:
.SUFFIXES: .c .o

.PHONY: all clean

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(PROGRAM) $(OBJECTS)
