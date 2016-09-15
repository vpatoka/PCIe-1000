obj-m := symmbc7x.o

EXTRA_CFLAGS := -I$(src)/../include

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

default:
        $(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
        $(MAKE) -C $(KDIR) M=$(PWD) clean

install:
        @./install-sh
