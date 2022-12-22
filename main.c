/* Name: main.c
 * Project: bootloaderJOY
 * Author Francis Gradel, B.Eng. (Retronic Design)
 * Modified Date: 2020-12-22
 * Based on project bootloaderHID from Christian Starkjohann
 * Tabsize: 4
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "usbcalls.h"

#define IDENT_VENDOR_NUM        0x16c0
#define IDENT_VENDOR_STRING     "obdev.at"
#define IDENT_PRODUCT_NUM       0x05df
#define IDENT_PRODUCT_STRING    "HIDBoot"

#define IDENT_VENDOR_NUM_JOY       0x0810
#define IDENT_PRODUCT_NUM_JOY      0xe501
#define IDENT_VENDOR_STRING_JOY    "retronicdesign.com"

#define IDENT_VENDOR_NUM_MOUSE       0x16c0
#define IDENT_PRODUCT_NUM_MOUSE      0x27da
#define IDENT_VENDOR_STRING_MOUSE    "retronicdesign.com"

/* ------------------------------------------------------------------------- */

static char dataBuffer[65536 + 256];    /* buffer for file data */
static int  startAddress, endAddress;
static char leaveBootLoader = 0;

/* ------------------------------------------------------------------------- */

static int  parseUntilColon(FILE *fp)
{
int c;

    do{
        c = getc(fp);
    }while(c != ':' && c != EOF);
    return c;
}

static int  parseHex(FILE *fp, int numDigits)
{
int     i;
char    temp[9];

    for(i = 0; i < numDigits; i++)
        temp[i] = getc(fp);
    temp[i] = 0;
    return strtol(temp, NULL, 16);
}

/* ------------------------------------------------------------------------- */

static int  parseIntelHex(char *hexfile, char buffer[65536 + 256], int *startAddr, int *endAddr)
{
int     address, base, d, segment, i, lineLen, sum;
FILE    *input;

    input = fopen(hexfile, "r");
    if(input == NULL){
        fprintf(stderr, "error opening %s: %s\n", hexfile, strerror(errno));
        return 1;
    }
    while(parseUntilColon(input) == ':'){
        sum = 0;
        sum += lineLen = parseHex(input, 2);
        base = address = parseHex(input, 4);
        sum += address >> 8;
        sum += address;
        sum += segment = parseHex(input, 2);  /* segment value? */
        if(segment != 0)    /* ignore lines where this byte is not 0 */
            continue;
        for(i = 0; i < lineLen ; i++){
            d = parseHex(input, 2);
            buffer[address++] = d;
            sum += d;
        }
        sum += parseHex(input, 2);
        if((sum & 0xff) != 0){
            fprintf(stderr, "Warning: Checksum error between address 0x%x and 0x%x\n", base, address);
        }
        if(*startAddr > base)
            *startAddr = base;
        if(*endAddr < address)
            *endAddr = address;
    }
    fclose(input);
    return 0;
}

/* ------------------------------------------------------------------------- */

char    *usbErrorMessage(int errCode)
{
static char buffer[80];

    switch(errCode){
        case USB_ERROR_ACCESS:      return "Access to device denied";
        case USB_ERROR_NOTFOUND:    return "The specified device was not found";
        case USB_ERROR_BUSY:        return "The device is used by another application";
        case USB_ERROR_IO:          return "Communication error with device";
        default:
            sprintf(buffer, "Unknown USB error %d", errCode);
            return buffer;
    }
    return NULL;    /* not reached */
}

static int  getUsbInt(char *buffer, int numBytes)
{
int shift = 0, value = 0, i;

    for(i = 0; i < numBytes; i++){
        value |= ((int)*buffer & 0xff) << shift;
        shift += 8;
        buffer++;
    }
    return value;
}

static void setUsbInt(char *buffer, int value, int numBytes)
{
int i;

    for(i = 0; i < numBytes; i++){
        *buffer++ = value;
        value >>= 8;
    }
}

/* ------------------------------------------------------------------------- */

typedef struct deviceInfo{
    char    reportId;
    char    pageSize[2];
    char    flashSize[4];
}deviceInfo_t;

typedef struct deviceData{
    char    reportId;
    char    address[3];
    char    data[128];
}deviceData_t;

static int uploadData(char *dataBuffer, int startAddr, int endAddr, int vid, int pid)
{
usbDevice_t *dev = NULL;
int         err = 0, len, mask, pageSize, deviceSize;
union{
    char            bytes[1];
    deviceInfo_t    info;
    deviceData_t    data;
}           buffer;

	if(vid==0 || pid==0) {
		vid=IDENT_VENDOR_NUM;
		pid=IDENT_PRODUCT_NUM;
	}

    if((err = usbOpenDevice(&dev, vid, IDENT_VENDOR_STRING, pid, IDENT_PRODUCT_STRING, 1)) != 0){
        fprintf(stderr, "Error opening HIDBoot device: %s\n", usbErrorMessage(err));
        goto errorOccurred;
    }
    len = sizeof(buffer);
    if(endAddr > startAddr){    // we need to upload data
        if((err = usbGetReport(dev, USB_HID_REPORT_TYPE_FEATURE, 1, buffer.bytes, &len)) != 0){
            fprintf(stderr, "Error reading page size: %s\n", usbErrorMessage(err));
            goto errorOccurred;
        }
        if(len < sizeof(buffer.info)){
            fprintf(stderr, "Not enough bytes in device info report (%d instead of %d)\n", len, (int)sizeof(buffer.info));
            err = -1;
            goto errorOccurred;
        }
        pageSize = getUsbInt(buffer.info.pageSize, 2);
        deviceSize = getUsbInt(buffer.info.flashSize, 4);
        printf("Page size   = %d (0x%x)\n", pageSize, pageSize);
        printf("Device size = %d (0x%x); %d bytes remaining\n", deviceSize, deviceSize, deviceSize - 2048);
        if(endAddr > deviceSize - 2048){
            fprintf(stderr, "Data (%d bytes) exceeds remaining flash size!\n", endAddr);
            err = -1;
            goto errorOccurred;
        }
        if(pageSize < 128){
            mask = 127;
        }else{
            mask = pageSize - 1;
        }
        startAddr &= ~mask;                  /* round down */
        endAddr = (endAddr + mask) & ~mask;  /* round up */
        printf("Uploading %d (0x%x) bytes starting at %d (0x%x)\n", endAddr - startAddr, endAddr - startAddr, startAddr, startAddr);
        while(startAddr < endAddr){
            buffer.data.reportId = 2;
            memcpy(buffer.data.data, dataBuffer + startAddr, 128);
            setUsbInt(buffer.data.address, startAddr, 3);
            printf("\r0x%05x ... 0x%05x", startAddr, startAddr + (int)sizeof(buffer.data.data));
            fflush(stdout);
            if((err = usbSetReport(dev, USB_HID_REPORT_TYPE_FEATURE, buffer.bytes, sizeof(buffer.data))) != 0){
                fprintf(stderr, "Error uploading data block: %s\n", usbErrorMessage(err));
                goto errorOccurred;
            }
            startAddr += sizeof(buffer.data.data);
        }
        printf("\n");
    }
    if(leaveBootLoader){
        /* and now leave boot loader: */
        buffer.info.reportId = 1;
        usbSetReport(dev, USB_HID_REPORT_TYPE_FEATURE, buffer.bytes, sizeof(buffer.info));
        /* Ignore errors here. If the device reboots before we poll the response,
         * this request fails.
         */
		fprintf(stderr, "Joystick device now in normal mode.\n");
    }
errorOccurred:
    if(dev != NULL)
        usbCloseDevice(dev);
    return err;
}

static int putJoyinBootloaderMode(int vid, int pid)
{
usbDevice_t *dev = NULL;
int         err = 0;

typedef struct HIDSetReport_t{
    char    reportId;
    char    data;
}HIDSetReport_t;

HIDSetReport_t buffer;

	if(vid==0 || pid==0) {
		vid=IDENT_VENDOR_NUM_JOY;
		pid=IDENT_PRODUCT_NUM_JOY;
	}
	
    if((err = usbOpenDevice(&dev, vid, IDENT_VENDOR_STRING_JOY, pid, NULL, 1)) != 0){
		if((err = usbOpenDevice(&dev, IDENT_VENDOR_NUM_MOUSE, IDENT_VENDOR_STRING_MOUSE, IDENT_PRODUCT_NUM_MOUSE, NULL, 1)) != 0){
			fprintf(stderr, "Error opening joystick device: %s\n", usbErrorMessage(err));
			goto errorOccurred;
		}
		else {
			buffer.reportId = 0;
			buffer.data = 0x5A;
			usbSetReport(dev, USB_HID_REPORT_TYPE_FEATURE, (char *)&buffer, sizeof(buffer));
			/* Ignore errors here. If the device reboots before we poll the response,
			* this request fails.
			*/
			fprintf(stderr, "Mouse device now in bootloader mode.\n");
		}
    }
	else {
		buffer.reportId = 0;
		buffer.data = 0x5A;
        usbSetReport(dev, USB_HID_REPORT_TYPE_FEATURE, (char *)&buffer, sizeof(buffer));
		/* Ignore errors here. If the device reboots before we poll the response,
         * this request fails.
         */
		fprintf(stderr, "Joystick device now in bootloader mode.\n");
	}

errorOccurred:
    if(dev != NULL)
        usbCloseDevice(dev);
    return err;
}

/* ------------------------------------------------------------------------- */

static void printUsage(char *pname)
{
	fprintf(stderr, "usage: %s [-b] [VID(in Hex) PID(in Hex)]\n", pname);
	fprintf(stderr, "[-b] switch target to bootloader mode\n");
    fprintf(stderr, "usage: %s [-r] [<intel-hexfile>] [VID(in Hex) PID(in Hex)]\n", pname);
	fprintf(stderr, "[-r] reset target to normal mode\n");
	fprintf(stderr, "usage: %s [-l]\n", pname);
	fprintf(stderr, "[-l] list compatible devices on USB bus\n");
}

int main(int argc, char **argv)
{
char    *file = NULL;
int		vid, pid;
int 	numdevices;
	
	vid=pid=0;

    if(argc < 2){
        printUsage(argv[0]);
        return 1;
    }
    if(strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0){
        printUsage(argv[0]);
        return 1;
    }
	if(strcmp(argv[1], "-l") == 0){
        fprintf(stderr, "Compatible devices on USB bus:\n");
		numdevices = usbListDevice(IDENT_VENDOR_NUM, IDENT_PRODUCT_NUM);
		numdevices += usbListDevice(IDENT_VENDOR_NUM_JOY, IDENT_PRODUCT_NUM_JOY);
		numdevices += usbListDevice(IDENT_VENDOR_NUM_MOUSE, IDENT_PRODUCT_NUM_MOUSE);
		printf("\nDevice discovered = %d\n",numdevices);
        return 1;
    }
    if(strcmp(argv[1], "-b") == 0){
			if(argc == 4) {
				sscanf(argv[2],"%x",&vid);
				sscanf(argv[3],"%x",&pid);
			}
			return putJoyinBootloaderMode(vid,pid);
        }
    else if(strcmp(argv[1], "-r") == 0){
        leaveBootLoader = 1;
        if(argc == 3){
            file = argv[2];
        }
		else if(argc == 5) {
			file = argv[2];
			sscanf(argv[3],"%x",&vid);
			sscanf(argv[4],"%x",&pid);
		}
    }else{
        file = argv[1];
		if(argc == 4) {
			sscanf(argv[2],"%x",&vid);
			sscanf(argv[3],"%x",&pid);
		}
    }
    startAddress = sizeof(dataBuffer);
    endAddress = 0;
    if(file != NULL){   // an upload file was given, load the data
        memset(dataBuffer, -1, sizeof(dataBuffer));
        if(parseIntelHex(file, dataBuffer, &startAddress, &endAddress))
            return 1;
        if(startAddress >= endAddress){
            fprintf(stderr, "No data in input file, exiting.\n");
            return 0;
        }
    }
    // if no file was given, endAddress is less than startAddress and no data is uploaded
    if(uploadData(dataBuffer, startAddress, endAddress, vid, pid))
        return 1;
    return 0;
}

/* ------------------------------------------------------------------------- */


