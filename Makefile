# Name: Makefile
# Project: bootloaderJOY
# Author Francis Gradel, B.Eng. (Retronic Design)
# Modified Date: 2020-12-22
# Based on project bootloaderHID from Christian Starkjohann
# Tabsize: 4
# License: Proprietary, free under certain conditions. See Documentation.

# Please read the definitions below and edit them as appropriate for your
# system:

# Use the following 3 lines on Unix and Mac OS X:
#USBFLAGS=   `libusb-config --cflags`
#USBLIBS=    `libusb-config --libs`
#EXE_SUFFIX=

# Use the following 3 lines on Windows and comment out the 3 above:
USBFLAGS=   -ID:\libusb-win32-bin-1.2.7.1\include
USBLIBS=    -lhid -lsetupapi -LD:\libusb-win32-bin-1.2.7.1\lib D:\libusb-win32-bin-1.2.7.1\lib\gcc\libusb.a
EXE_SUFFIX= .exe

CC=				gcc
CXX=			g++
CFLAGS=			-O2 -Wall $(USBFLAGS)
LIBS=			$(USBLIBS)
ARCH_COMPILE=	
ARCH_LINK=		

OBJ=		main.o usbcalls.o
PROGRAM=	bootloadJOY$(EXE_SUFFIX)

all: $(PROGRAM)

$(PROGRAM): $(OBJ)
	$(CC) $(ARCH_LINK) $(CFLAGS) -o $(PROGRAM) $(OBJ) $(LIBS)


strip: $(PROGRAM)
	strip $(PROGRAM)

clean:
	rm -f $(OBJ) $(PROGRAM)

.c.o:
	$(CC) $(ARCH_COMPILE) $(CFLAGS) -c $*.c -o $*.o
