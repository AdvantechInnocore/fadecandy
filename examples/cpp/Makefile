PROGRAMS = simple rings spokes dot particle_trail mixer

# Important optimization options
CFLAGS = -O3 -ffast-math -fno-rtti

# Standard libraries
LFLAGS = -lm -lstdc++ -lpthread

# Debugging
CFLAGS += -g -Wall -D__CMATH__ -fpermissive
LFLAGS += -g

# Annoying warnings on by default on Mac OS
CXXFLAGS += -Wno-class-memaccess -Wno-maybe-uninitialized


all: $(PROGRAMS)

.cpp:
	$(CC) $(CFLAGS) $< -o $@ $(LFLAGS)

.PHONY: clean all

clean:
	rm -f $(PROGRAMS)
