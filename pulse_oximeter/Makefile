obj-m := pulse_oximeter.o


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	gcc app.c -o app

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
