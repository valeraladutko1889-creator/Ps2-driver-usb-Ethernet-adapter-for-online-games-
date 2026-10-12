IOP_BIN = smap.irx
IOP_OBJS = smap_usb_monolith.o
IOP_LIBS = -lusbd -lthbase -thevent -lsysmem -lsysclib
IOP_SRC_EXPORTS = exports.tab

include $(PS2SDK)/Defs.make
include $(PS2SDK)/rules.iop

IOP_CFLAGS += -O2 -Wall

clean:
	rm -f $(IOP_BIN) $(IOP_OBJS) *.smap *.lst
