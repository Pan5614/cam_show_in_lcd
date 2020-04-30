CROSS_COMPILE= /opt/ingenic_compiler/mips-gcc472-glibc216/bin/mips-linux-gnu-
CXX=${CROSS_COMPILE}g++
CC=${CROSS_COMPILE}gcc


mycam:mycam.o
	$(CC) mycam.o -o mycam


mycam.o:mycam.c
	$(CC) -c mycam.c



clean:
	rm -rf *.o mycam
