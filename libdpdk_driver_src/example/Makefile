# build commands
CC := gcc

ifeq ($(OPTIMIZE), Y)
CX_FLAGS := -Os
endif

ifeq ($(DEBUG), Y)
CX_FLAGS := -g -DDEBUG
endif

DPDK_LIB := -ldpdk_driver -lpthread

.PHONY: all clean

all:demo
demo:main.c
	$(CC) $(CX_FLAGS)  -o $@ $^  $(DPDK_LIB)

clean:
	rm -rf ./demo

