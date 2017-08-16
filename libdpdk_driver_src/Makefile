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
            -lrte_pmd_tap \
            -lrte_ip_frag \
            -lrte_bitratestats \
            -lrte_acl \
            -lrte_port \
            -lrte_pmd_sfc_efx \
            -lrte_mbuf \
            -lrte_pmd_kni \
            -lrte_pmd_bnxt \
            -lrte_vhost \
            -lrte_distributor \
            -lrte_mempool_stack \
            -lrte_pmd_af_packet \
            -lrte_metrics \
            -lrte_pipeline \
            -lrte_pmd_skeleton_event \
            -lrte_pmd_nfp \
            -lrte_net \
            -lrte_mempool \
            -lrte_pmd_ixgbe \
            -lrte_latencystats \
            -lrte_pmd_octeontx_ssovf \
            -lrte_meter \
            -lrte_pmd_cxgbe \
            -lrte_power \
            -lrte_kni \
            -lrte_pmd_fm10k \
            -lrte_cfgfile \
            -lrte_pmd_i40e \
            -lrte_pmd_null \
            -lrte_pmd_null_crypto \
            -lrte_pmd_lio \
            -lrte_sched \
            -lrte_pmd_qede \
            -lrte_table \
            -lrte_pmd_ena \
            -lrte_hash \
            -lrte_pmd_enic \
            -lrte_eventdev \
            -lrte_pmd_ring \
            -lrte_pdump \
            -lrte_pmd_virtio \
            -lrte_cryptodev \
            -lrte_reorder \
            -lrte_eal \
            -lrte_pmd_avp \
            -lrte_pmd_crypto_scheduler \
            -lrte_pmd_thunderx_nicvf \
            -lrte_pmd_vhost \
            -lrte_jobstats \
            -lrte_pmd_bond \
            -lrte_mempool_ring \
            -lrte_cmdline \
            -lrte_timer \
            -lrte_efd \
            -lrte_pmd_vmxnet3_uio \
            -lrte_ring \
            -lrte_pmd_ark \
            -lrte_ethdev \
            -lrte_lpm \
            -lrte_pmd_e1000 \
            -lrte_kvargs \
            -lrte_pmd_sw_event

.PHONY: all clean install
all:dpdk_driver

dpdk_driver:$(DRV_SRC)
	$(CC) $(CX_FLAGS) $(SO_FLAGS)  $(INC_FLAGS)  -o $(DRV_EXPORT_LIB)/lib$@.so $^  $(DPDK_LIB)

clean:
	$(RM) $(DRV_EXPORT_LIB)/*
	
install:
	$(INSTALL) $(BASE_INC_DIR)/* $(DRV_INSTALL_INC)
	$(INSTALL) $(DRV_EXPORT_LIB)/* $(DRV_INSTALL_LIB)
	


