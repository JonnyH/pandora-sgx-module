obj-m	:= pvrsrvkm.o

FILES := \
services4/srvkm/common/queue.c \
services4/srvkm/common/hash.c \
services4/srvkm/common/perproc.c \
services4/srvkm/common/mem.c \
services4/srvkm/common/power.c \
services4/srvkm/common/deviceclass.c \
services4/srvkm/common/metrics.c \
services4/srvkm/common/resman.c \
services4/srvkm/common/buffer_manager.c \
services4/srvkm/common/pvrsrv.c \
services4/srvkm/common/handle.c \
services4/srvkm/common/lists.c \
services4/srvkm/common/ra.c \
services4/srvkm/common/devicemem.c \
services4/srvkm/env/linux/pvr_debug.c \
services4/srvkm/env/linux/mm.c \
services4/srvkm/env/linux/mutex.c \
services4/srvkm/env/linux/mmap.c \
services4/srvkm/env/linux/module.c \
services4/srvkm/env/linux/proc.c \
services4/srvkm/env/linux/event.c \
services4/srvkm/env/linux/osfunc.c \
services4/srvkm/env/linux/pvr_bridge_k.c \
services4/srvkm/env/linux/pdump.c \
services4/srvkm/env/linux/mutils.c \
services4/srvkm/env/linux/osperproc.c \
services4/srvkm/devices/sgx/sgxtransfer.c \
services4/srvkm/devices/sgx/sgxinit.c \
services4/srvkm/devices/sgx/sgxutils.c \
services4/srvkm/devices/sgx/pb.c \
services4/srvkm/devices/sgx/sgxkick.c \
services4/srvkm/devices/sgx/mmu.c \
services4/srvkm/devices/sgx/sgxreset.c \
services4/srvkm/devices/sgx/sgxpower.c \
services4/srvkm/bridged/bridged_pvr_bridge.c \
services4/srvkm/bridged/bridged_support.c \
services4/srvkm/bridged/sgx/bridged_sgx_bridge.c \
services4/system/$(TI_PLATFORM)/sysutils_linux.c \
services4/system/$(TI_PLATFORM)/sysconfig.c \

EXTRA_CFLAGS += -I$(src)/include4
EXTRA_CFLAGS += -I$(src)/services4/include
EXTRA_CFLAGS += -I$(src)/services4/srvkm/include
EXTRA_CFLAGS += -I$(src)/services4/srvkm/hwdefs
EXTRA_CFLAGS += -I$(src)/services4/srvkm/bridged
EXTRA_CFLAGS += -I$(src)/services4/srvkm/devices/sgx
EXTRA_CFLAGS += -I$(src)/services4/srvkm/env/linux
EXTRA_CFLAGS += -I$(src)/services4/system/include
EXTRA_CFLAGS += -I$(src)/services4/system/$(TI_PLATFORM)
EXTRA_CFLAGS += -I$(src)/services4/srvkm/bridged/sgx

EXTRA_CFLAGS += $(ALL_CFLAGS)

pvrsrvkm-y	:= $(FILES:.c=.o)
ifeq ($(TI_PLATFORM),ti8168)
obj-y := services4/3rdparty/dc_ti8168_linux/
else
obj-y := services4/3rdparty/dc_omap3_linux/
endif
obj-y += services4/3rdparty/bufferclass_ti/

ifeq ($(VERSION), 3)
export EXTRA_CFLAGS += -DAUTOCONF_INCLUDED
endif
