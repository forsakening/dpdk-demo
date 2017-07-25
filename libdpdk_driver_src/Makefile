#   BSD LICENSE
#
#   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# build commands
CC := gcc

ifeq ($(OPTIMIZE), Y)
CX_FLAGS := -Os
endif

ifeq ($(DEBUG), Y)
CX_FLAGS := -g -DDEBUG
endif

CX_FLAGS += -mssse3

SO_FLAGS := -fPIC -shared
RM := rm -fr
INSTALL := cp -fr
# build variables
BASE_INC_DIR := $(CURDIR)/inc
INC_FLAGS += -I$(BASE_INC_DIR)

DPDK_INC_DIR := /usr/local/include/dpdk
INC_FLAGS += -I$(DPDK_INC_DIR)

DRV_SRCDIR := $(CURDIR)/src
DRV_SRC = $(DRV_SRCDIR)/dpdk_driver.c \


DRV_EXPORT_LIB := $(CURDIR)/lib

DRV_INSTALL_INC := /usr/include
DRV_INSTALL_LIB := /usr/lib64


DPDK_LIB += -lpthread -Wl,-rpath,/usr/local/lib \
            -lrte_eal -lrte_ring -lrte_mbuf -lrte_ethdev -lrte_pmd_ixgbe \
            -lrte_pmd_e1000 

.PHONY: all clean install
all:dpdk_driver

dpdk_driver:$(DRV_SRC)
	$(CC) $(CX_FLAGS) $(SO_FLAGS)  $(INC_FLAGS)  -o $(DRV_EXPORT_LIB)/lib$@.so $^  $(DPDK_LIB)

clean:
	$(RM) $(DRV_EXPORT_LIB)/*
	
install:
	$(INSTALL) $(BASE_INC_DIR)/* $(DRV_INSTALL_INC)
	$(INSTALL) $(DRV_EXPORT_LIB)/* $(DRV_INSTALL_LIB)
	


