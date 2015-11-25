all:
	make -C UDT4
	make -C udtfs

clean:
	make -C UDT4 clean
	make -C udtfs clean

install:
	make -C udtfs install

