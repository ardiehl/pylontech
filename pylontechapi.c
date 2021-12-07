/*
 * pylontechapi.c
 *
 * Copyright 2021 Armin Diehl <ad@ardiehl.de>
 *
 * Api for accessing Pylontech Batteries via RS485
 * Tested with US3000C but should also work with other models
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 * Dec 1, 2021 Armin:
    first version
*/
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include "util.h"
#include "uart.h"
#include "pylontechapi.h"
#include "termios_helper.h"
//#define debug

#define CID2_GetAnalogValue 0x42
#define CID2_GetAlarmData 0x44
#define CID2_GetSystemParameter 0x47
#define CID2_GetCommunicationProtocolVersion 0x4f
#define CID2_GetManufacturerInformation 0x51
#define CID2_GetChargeDischargeManagementInformation 0x92
#define CID2_GetSerialNumberOfEquipment 0x93
#define CID2_SetChargeDischargeManagementInformation 0x94
#define CID2_TurnOff 0x95
#define CID2_GetFirmwareInfo 0x96

extern int verbose;

const char * emptyString = "";


const char * cid2ResponseTxt (int cid2) {
	switch (cid2) {
		case 0: return emptyString;
		case 1:	return "VER error";
		case 2: return "CHKSUM error";
		case 3: return "LCHKSUM error";
		case 4: return "CID2 invalid";
		case 5: return "Command format error";
		case 6: return "Invalid data";
		case 8: return "ADR error";
		case 9: return "Communication error";
	}
	return "unknown error";
}

const char *commandName(int command) {
	switch (command) {
		case CID2_GetAnalogValue							: return "GetAnalogValue";
		case CID2_GetAlarmData								: return "GetAlarmData";
		case CID2_GetSystemParameter						: return "GetSystemParameter";
		case CID2_GetCommunicationProtocolVersion			: return "GetCommunicationProtocolVersion";
		case CID2_GetManufacturerInformation				: return "GetManufacturerInformation";
		case CID2_GetChargeDischargeManagementInformation	: return "GetChargeDischargeManagementInformation";
		case CID2_GetSerialNumberOfEquipment				: return "GetSerialNumberOfEquipment";
		case CID2_SetChargeDischargeManagementInformation	: return "SetChargeDischargeManagementInformation";
		case CID2_TurnOff									: return "TurnOff";
		case CID2_GetFirmwareInfo							: return "GetFirmwareInfo";
	}
	return emptyString;
}



#if 0
void printBin(int bits, int data) {
	int mask = 1 << (bits-1);

	while (bits) {
		if (data & mask) printf("1"); else printf("0");
		mask = mask >> 1;
		bits--;
	}
}
#endif

// calculate 4 bit checksum for length field
int calc_lchecksum (int len) {
	if (len == 0) return 0;
	int chk = 0;

	len &= 0x0fff;				// we only have 12 bits
	// sum it up
	chk = ((len & 0xf00) >> 8) + ((len & 0x0f0) >> 4) + (len & 0x00f);
	chk %= 16;					// modulo 16
	chk = ~chk & 0x0f;			// invert and limit to 4 bits
	chk += 1;					// and one on top
	return chk;
}

// set length checksum in length field (bits 15..12)
int calc_lengthFiled (int length) {
	return length + ((calc_lchecksum(length) << 12) &0x0f000);
}

int calc_checksum (char *data, int len) {
	char *p = data;
	int chk = 0;

	while (len) {
		chk += (uint8_t)*p;
		p++; len--;
	}
	//printf("Sum: 0x0%x\n",chk);
	chk %= 65536;
	chk = ~chk & 0x0ffff;
	chk += 1;
	return chk;
}


typedef struct {
	int  ver;			// length = 1 (00..ff)
	int  adr;
	int  cid1;
	int  cid2;
	char *info;			// can be NULL, otherwise hex ascii data to be sent
	char *readPtr;		// current read pos in info
} packetDataT;


void dumpPacket (packetDataT * pa) {
	if (! pa) return;

	printf("packet: ver: 0x%02x adr: %d, cid1: 0x0%02x, cid2: 0x%02x",pa->ver,pa->adr,pa->cid1,pa->cid2);

	if (pa->info) {
		printf(" info: '");
		dumpBuffer(pa->info,strlen(pa->info));
		//printf("'\n");
	}
	printf("\n");
}


void packetClear (PYL_HandleT* pyl, packetDataT * pa) {
	memset(pa,0,sizeof(packetDataT));
	if(pyl) {
		pa->adr = (pyl->group << 4) + pyl->adr+1;
		pa->ver = pyl->protocolVersion;
	}
}


packetDataT * packetAllocate(PYL_HandleT* pyl, int cid1, int cid2) {
	packetDataT * pa = malloc(sizeof(packetDataT));
	if(pa==NULL) return pa;
	packetClear (pyl,pa);
	pa->cid1 = cid1;
	pa->cid2 = cid2;
	return pa;
}


void packetFree (packetDataT * pa) {
	if (pa) {
		if (pa->info) free (pa->info);
		free(pa);
	}
}


// generate the data to be transferred
char * packetPrepareData (packetDataT *pa) {
	int infoLen = 0;
	int len,length;
	char *buf, *temp;
	int chk;


	if (pa->info) infoLen = strlen(pa->info);
	len = (10*2) + (infoLen*2);

	buf = malloc(len+1);
	if (buf == NULL) return buf;
	strcpy(buf,"~");			// SOI
	if (pa->cid2 == CID2_GetCommunicationProtocolVersion) bufAddHex1(buf,0);
	else bufAddHex1(buf,pa->ver);		// VER
	bufAddHex1(buf,pa->adr);
	bufAddHex1(buf,pa->cid1);
	bufAddHex1(buf,pa->cid2);
	length = calc_lengthFiled(infoLen);
	bufAddHex2(buf,length);	// Length field, 4 Bit checksum + 12 bit info length
	if (infoLen > 0) {
		strcat(buf,pa->info);
	}
	temp = buf; temp ++;						// we need to calculate the checksum without SOI
	chk = calc_checksum(temp,strlen(temp));
	bufAddHex2(buf,chk);
	strcat(buf,"\r");							// EOI 0x0d
	VPRINTF(2,"packetPrepareData: CID2: %d (%s) chk: 0x%04x, length field: 0x%04x\n",pa->cid2,commandName(pa->cid2),chk,length);
	strupper(buf);								// make it upper case, dont know if this is needed
	return buf;
}


// this is the maximum possible length plus a 0 byte
#define RECEIVE_BUFSIZE 24+4096+1
// min len incl. length field
#define PKT_LEN_MIN (1+(6*2))

#ifdef USE_SELECT
#define UART_READ(PYL,...) uart_read_bytes(PYL->serFd, __VA_ARGS__)
#else
#define UART_READ(PYL,...) uart_read_bytes(PYL->serFd,&PYL->ti, __VA_ARGS__)
#endif // USE_SELECT

packetDataT * packetReceive (PYL_HandleT* pyl) {
	char buf[RECEIVE_BUFSIZE];
	char *p = &buf[0];
	int res2, res,length,chk,chkExpected,len,infoLen;
	packetDataT *pa;
	char EOI_buf[4];
	int maxInvalidSOI = 10;

	memset(buf,0,RECEIVE_BUFSIZE);

	// wait for SOI
	int SOI=0;
	while  (SOI == 0) {
		res = UART_READ(pyl,p,1,18);
		if (res == 1) {
			if (buf[0] != 0x7e) {
				if (pyl->initialized) LOG(0,"packetReceive: received invalid SOI char (0x0%x)[%c], expected 0x7e\n",buf[0],buf[0]>' '?buf[0]:' ');
				usleep(1000 * 50);
				uart_flush(pyl->serFd);
				maxInvalidSOI--;
				if(maxInvalidSOI==0) return NULL;
			} else {
				SOI++;
			}
		} else {
			uart_flush(pyl->serFd);
			if (pyl->initialized) {
				// send a few 0x0d in case some of the pylontech's are waiting for EOI
				memset(&EOI_buf,0x0d,sizeof(EOI_buf));
				res = uart_write_bytes(pyl->serFd,EOI_buf,sizeof(EOI_buf));	// uart_write_bytes waits until all chars are transmitted
				res2 = uart_waiti(pyl->serFd,50);
				LOG(0,"packetReceive: got no response, send %d 0x0d, checking for available data returned %d\n",res,res2);
			}

			return NULL;
		}
	}
	p++;

	// get the fix part of the packet (including the length field
	res = UART_READ(pyl, p,PKT_LEN_MIN-1,50);
	if (res != PKT_LEN_MIN-1) {
		LOG(0,"packetReceive: got only %d bytes but expected %d bytes\n",res,PKT_LEN_MIN-1);
		usleep(1000 * 50);
		uart_flush(pyl->serFd);
		return NULL;
	}
	// we have the packet and the last 4 bytes are the length in hex ascii (16 bit),
	// set p to the start of the length field
	p = p + res - 4;
	length = hex2int(p,2);
	chkExpected = calc_lchecksum (length);			// calculate the expected length checksum
	chk = (((length & 0x0f000) >> 12) & 0x0f);		// extract the length checksum from the received length field
	VPRINTF(3,"packetReceive: length field is '%s', lchk is: 0x0%x, lchkExpected: 0x0%x\n",p,chk,chkExpected);
	if (chk != chkExpected) {
		LOG(0,"packetReceive: invalid checksum in length field, got 0x0%x, expected 0x0%x\n",chk,chkExpected);
		usleep(1000 * 50);
		uart_flush(pyl->serFd);
		return NULL;
	}
	// Calculate remaining chars to receive
	infoLen = (length & 0x0fff);					// remove checksum from length field
	len = infoLen + 4 + 1;							// info + checksum (16 bit) + EOI(0x0d)
	p = strend(buf);
	res = UART_READ(pyl,p,len,50);					// receive remaining @ end of string
	if (res != len) {
		LOG(0,"packetReceive: got only %d bytes for the second part of a packet but expected %d bytes\n",res,len);
		usleep(1000 * 50);
		uart_flush(pyl->serFd);
		return NULL;
	}
	// calculate the expected checksum
	p = buf; p++;									// without SOI
	chkExpected = calc_checksum(p,strlen(p)-1-4);	// minus EOI minus CHKSUM
	// lets check the checksum we got
	p = strend(buf)-1-4;							// extract the
	chk = hex2int(p,2);								// received checksum
	VPRINTF(3,"packetReceive: chk is: 0x0%x, chkExpected: 0x0%x, packet: '",chk,chkExpected);
	if (verbose >= 2) {
			dumpBuffer(buf,strlen(buf)); printf("' '");
			dumpBuffer(p,strlen(p)); printf("'\n");
	}
	if (chk != chkExpected) {
		LOG(0,"packetReceive: invalid checksum, got 0x0%x, expected 0x0%x\n",chk,chkExpected);
		usleep(1000 * 50);
		uart_flush(pyl->serFd);
		return NULL;
	}

	// we got it and the checksums are ok, convert from hex ascii
	p = buf;
	p++;			// SOI
	pa = packetAllocate(NULL,0,0);
	pa->ver  = hex2int(p,1); p+=2;
	pa->adr  = hex2int(p,1); p+=2;
	pa->cid1 = hex2int(p,1); p+=2;
	pa->cid2 = hex2int(p,1); p+=2;

	// set the the info part
	if (infoLen >0 ) {
		p+=4;								// skip length, is now @ info
		pa->info = calloc(1,infoLen+1);
		memcpy(pa->info,p,infoLen);
	}
	VPRINTF(3,"packetReceive: received packet, rtn: 0x%02x %s\n",pa->cid2,cid2ResponseTxt(pa->cid2));
	if (verbose > 2) {
		printf ("received packet -> ");
		dumpPacket(pa);
	}
	pa->readPtr = pa->info;
	return pa;
}

int paInfoGetInt(packetDataT *pa, int byteCount) {
	int res = hex2int(pa->readPtr,byteCount);
	pa->readPtr+=(byteCount*2);
	return res;
}

void paInfoGetString(char * dest, packetDataT *pa, int len) {
	char c;
	while (len) {
		c = (char) paInfoGetInt(pa,1);
		*dest = c; dest++;
		len--;
	}
	*dest = '\0';
}

void paInfoSkipBytes(packetDataT *pa, int byteCount) {
	pa->readPtr+=(byteCount*2);
}

// converts pa to hex ascii and sends it, free pa if packed has been sent
int packetSend (PYL_HandleT* pyl, packetDataT * pa) {
	char * packetStr;
	int len,res;

	uart_flush(pyl->serFd);

	packetStr = packetPrepareData (pa);			// convert to hex ascii including SOI, EOI,
												// length field with length checksum and packet checksum
	if (packetStr == NULL) return PYL_ERR;			// no memory, not in these days ;-)
	len = strlen(packetStr);
	res = uart_write_bytes(pyl->serFd, packetStr, len);
	if (res != len) {							// something went wrong
		LOG(0,"Error sending data, res: %d, len: %d\n",res,len);
		free(packetStr);
		uart_flush(pyl->serFd);
		return PYL_ERR;
	}
	if (verbose > 2) {
		printf ("sent packet -> ");
		dumpPacket(pa);
	}
	free(pa);									// not needed any longer
	return PYL_OK;
}


#define TERMIOS_BUFLEN 512
packetDataT * sendCommandAndReceive2 (
				PYL_HandleT* pyl,
				int CID2,
				char * dataHexAscii,
				int expectedInfoLength) {

	int res;
	int infoLen = 0;
	packetDataT * pa = packetAllocate(pyl, LI_BAT_DATA, CID2);
	char *p;

	if (pyl == NULL) return NULL;
	if (!pa) return NULL;
	if (pyl->serFd <= 0) return NULL;
	pa->info = dataHexAscii;

	// on my Cerbos GX some program / service is accessing the serial port even if there is a lock file present
	// check if we have the correct serial settings
	struct termios ti;
	tcgetattr(pyl->serFd,&ti); 					// get current port settings
	if ( (ti.c_cflag!=pyl->ti.c_cflag) ||		// flags different than expected ?
		 (ti.c_iflag!=pyl->ti.c_iflag) ||
		 (ti.c_oflag!=pyl->ti.c_oflag) ) {
		p = malloc(TERMIOS_BUFLEN);
		if (p) {
			decode_termios (&ti, p, TERMIOS_BUFLEN);
			LOG(0,"Settings for %s altered externally\n",pyl->portname);
			LOG(0,"Current settings: %s\n",p);
			decode_termios (&pyl->ti, p, TERMIOS_BUFLEN);
			LOG(0,"Resetting to expected settings: %s\n",p);
		}
		free(p);
		tcsetattr(pyl->serFd,TCSANOW,&pyl->ti);
	}
	res = packetSend (pyl,pa);
	if (res != 0) {
		LOG(0,"%s: packet send failed, res: %d\n",commandName(CID2),res);
		packetFree(pa);
		return NULL;
	}
	pa = packetReceive (pyl);
	if (! pa) {
		packetFree(pa);
		return NULL;
	}

	if (pa->cid2 != 0) {		// 0 means ok otherwise error code
		LOG(0,"%s: received error code 0x%02x (%s)\n",commandName(CID2),pa->cid2,cid2ResponseTxt (pa->cid2));
		packetFree(pa);
		return NULL;
	}

	if (expectedInfoLength) {
		if (pa->info) infoLen = strlen(pa->info);
		if (expectedInfoLength > infoLen) {
			LOG(0,"%s: expected to receive %d bytes of data but got only %d bytes\n",commandName(CID2),expectedInfoLength,infoLen);
			packetFree(pa);
			return NULL;
		}
		if (expectedInfoLength != infoLen)
			LOG(0,"%s: Info: expected to receive %d bytes of data, got %d bytes\n",commandName(CID2),expectedInfoLength,infoLen);
	}
	return pa;
}

#define RETRY_COUNT 2
// in case of error, close port, wait, reopen and try again once
packetDataT * sendCommandAndReceive (
				PYL_HandleT* pyl,
				int CID2,
				char * dataHexAscii,
				int expectedInfoLength) {
	packetDataT * pd;
	int rc;
	int retry = RETRY_COUNT;

	while (retry) {
		pd = sendCommandAndReceive2 (pyl,CID2,dataHexAscii,expectedInfoLength);
		//if (pd==NULL) printf("sendCommandAndReceive2 failed, initialized: %d\n",pyl->initialized);
		if ((pd==NULL) && (pyl->initialized)) {
			LOG(0,"sendCommandAndReceive: Closing and reopening %s due to communication errors\n",pyl->portname);
			pyl_closeSerialPort(pyl);
			usleep(1000*500);
			rc = pyl_openSerialPort(pyl);
			if (rc != 0) {
				LOG(0,"sendCommandAndReceive: failed to reopen %s after failure\n",pyl->portname);
				return pd;
			}
		}
		if (pd) return pd;
		retry--;
	}
	return NULL;
}


int pyl_getProtocolVersion (PYL_HandleT* pyl) {
	packetDataT * pa = sendCommandAndReceive (pyl,CID2_GetCommunicationProtocolVersion, NULL, 0);
	if (!pa) return PYL_ERR;

	VPRINTF(2,"getProtocolVersion: got response, version is 0x%02x\n",pa->ver);
	pyl->protocolVersion = pa->ver;
	packetFree(pa);
	return PYL_OK;
}


int pyl_getAnalogData (PYL_HandleT* pyl, PYL_AnalogDataT *pd) {
	char info[10];
	int i;

	memset(pd,0,sizeof(*pd));
	sprintf(info,"%02x",pyl->adr+1);
	packetDataT * pa = sendCommandAndReceive (pyl,CID2_GetAnalogValue, info, 122);
	if (!pa) return PYL_ERR;

	pd->infoflag = paInfoGetInt(pa,1);
	pd->commandValue = paInfoGetInt(pa,1);

	pd->cellsCount = paInfoGetInt(pa,1);

	// get cell voltages
	for (i=0;i<pd->cellsCount;i++) {
		if (i < CELLS_MAX) pd->cellVoltage[i] = paInfoGetInt(pa,2);
	}

	pd->tempCount = paInfoGetInt(pa,1);
	// get temperature values
	for (i=0;i<pd->tempCount;i++) {
		if (i < TEMPS_MAX) pd->temp[i] = (paInfoGetInt(pa,2)-2731)/10;
	}

	pd->current = paInfoGetInt(pa,2);
	if (pd->current >= 32768) pd->current -= 65536;

	pd->voltage = paInfoGetInt(pa,2);
	pd->remainingCapacity = paInfoGetInt(pa,2);

	pd->userDefinedItemCount = paInfoGetInt(pa,1);

	pd->capacity = paInfoGetInt(pa,2);
	pd->cycleCount  = paInfoGetInt(pa,2);

	// larger batteries provide capacity encoded in 3 bytes, in that case userDefinedItemCount is 4 and
	// packCapacity as well as packRemainingCapacity are set to 0xffff
	if ((pd->capacity == 0xffff) && (pd->remainingCapacity == 0xffff)) {
		pd->remainingCapacity = paInfoGetInt(pa,3);
		pd->capacity = paInfoGetInt(pa,3);
	}

	packetFree(pa);
	return PYL_OK;
}



int pyl_getSystemParameter (PYL_HandleT* pyl, PYL_SystemParameterT *sp) {
	int len;

	packetDataT * pa = sendCommandAndReceive (pyl,CID2_GetSystemParameter, NULL, 2+(12*4));
	if (!pa) return PYL_ERR;

	len = 0;
	if (pa->info) len = strlen(pa->info);
	VPRINTF(2,"getSystemParameter: got response, data len: %d\n",len);
	if (len) {
		paInfoSkipBytes(pa,1);
		sp->cellHighVoltageLimit = paInfoGetInt(pa,2);
		sp->cellLowVoltageLimit = paInfoGetInt(pa,2);
		sp->cellUnderVoltageLimit = paInfoGetInt(pa,2);
		sp->chargeHighTemperatureLimit = (paInfoGetInt(pa,2)-2731)/10;
		sp->chargeLowTemperatureLimit = (paInfoGetInt(pa,2)-2731)/10;
		sp->chargeCurrentLimit = paInfoGetInt(pa,2);
		sp->moduleHighVoltageLimit = paInfoGetInt(pa,2);
		sp->moduleLowVoltageLimit = paInfoGetInt(pa,2);
		sp->moduleUnderVoltageLimit = paInfoGetInt(pa,2);
		sp->dischargeHighTemperatureLimit = (paInfoGetInt(pa,2)-2731)/10;
		sp->dischargeLowTemperatureLimit = (paInfoGetInt(pa,2)-2731)/10;
		sp->dischargeCurrentLimit = paInfoGetInt(pa,2);
	}
	packetFree(pa);
	return PYL_OK;
}


int pyl_getManufacturerInformation (PYL_HandleT* pyl, PYL_ManufacturerInformationT *mi) {
	int len;
	packetDataT * pa = sendCommandAndReceive (pyl,CID2_GetManufacturerInformation, NULL, (10+2+20)*2);
	memset(mi,0,sizeof(*mi));
	if (!pa) return PYL_ERR;

	len = 0;
	if (pa->info) len = strlen(pa->info);
	VPRINTF(2,"getManufacturerInformation: got response, data len: %d\n",len);
	if (len) {
		paInfoGetString(mi->deviceName,pa,10);
		mi->softwareVersion[0] = paInfoGetInt(pa,1);
		mi->softwareVersion[1] = paInfoGetInt(pa,1);
		paInfoGetString(mi->manufacturerName,pa,20);
	}
	packetFree(pa);
	return PYL_OK;
}



int pyl_getSerialNumber (PYL_HandleT* pyl, PYL_SerialNumberT *mi) {
	int len;
	char info[10];

	sprintf(info,"%02x",pyl->adr+1);

	packetDataT * pa = sendCommandAndReceive (pyl,CID2_GetSerialNumberOfEquipment, info, 17*2);
	if (!pa) return PYL_ERR;

	char * p;

	len = 0;
	if (pa->info) len = strlen(pa->info);
	VPRINTF(2,"getSerialNumber: got response, data len: %d\n",len);
	if (len) {
		p = pa->info;		// info contains the response data in hex ascii, each field is 2 bytes = 4 hex nibbles
		memset(mi,0,sizeof(*mi));
		p+=2;  // looks like addr
		getStringFromHex(p, mi->sn, 16);
	}
	packetFree(pa);
	return PYL_OK;
}


int pyl_getAlarmInfo (PYL_HandleT* pyl, PYL_AlarmInfoT *ai) {
	char info[10];
	int i;

	memset(ai,0,sizeof(*ai));
	sprintf(info,"%02x",pyl->adr+1);
	packetDataT * pa = sendCommandAndReceive (pyl, CID2_GetAlarmData, info, 66);
	if (!pa) return PYL_ERR;

	ai->dataFlag = paInfoGetInt(pa,1);
	ai->commandValue = paInfoGetInt(pa,1);

	ai->cellsCount = paInfoGetInt(pa,1);

	// get cell voltage status bytes
	for (i=0;i<ai->cellsCount;i++) {
		if (i < CELLS_MAX) ai->cellVoltageStatus[i] = paInfoGetInt(pa,1);
	}

	ai->tempCount = paInfoGetInt(pa,1);
	// get temperature value status bytes
	for (i=0;i<ai->tempCount;i++) {
		if (i < TEMPS_MAX) ai->tempStatus[i] = paInfoGetInt(pa,1);
	}
	ai->chargeCurrentStat = paInfoGetInt(pa,1);
	ai->moduleVoltageStat = paInfoGetInt(pa,1);
	ai->dischargeCurrentStat = paInfoGetInt(pa,1);
	for (i=0;i<5;i++) ai->status[i] = paInfoGetInt(pa,1);

	packetFree(pa);
	return PYL_OK;
}


int pyl_getChargeDischargeInfo (PYL_HandleT* pyl, PYL_ChargeDischargeInfoT *cd) {
	char info[10];

	memset(cd,0,sizeof(*cd));
	sprintf(info,"%02x",pyl->adr+1);
	packetDataT * pa = sendCommandAndReceive (pyl, CID2_GetChargeDischargeManagementInformation, info, 20);
	if (!pa) return PYL_ERR;

	cd->commandValue = paInfoGetInt(pa,1);
	cd->chargeVoltageLimit = paInfoGetInt(pa,2);
	cd->dischargeVoltageLimit = paInfoGetInt(pa,2);
	cd->chargeCurrentLimit = paInfoGetInt(pa,2);
	cd->dischargeCurrentLimit = paInfoGetInt(pa,2);
	cd->chargeDischargeStatus = paInfoGetInt(pa,1);

	packetFree(pa);
	return PYL_OK;
}


// allocate and initialize api handle
PYL_HandleT* pyl_initHandle() {
	PYL_HandleT* pyl;

	pyl = calloc(1,sizeof(*pyl));
	pyl->adr = 2;
	return pyl;
}

// free api handle and close serial port if open
void pyl_freeHandle(PYL_HandleT* pyl) {
	if(pyl) {
		pyl_closeSerialPort(pyl);
		if (pyl->portname) free(pyl->portname);
		free(pyl);
	}
}


// sets the group and scans for devices in group, returns number of devices found
int pyl_setGroup (PYL_HandleT* pyl, int groupNum) {
	int i,res;

	if (pyl->serFd <=0) return -1;

	pyl->group = groupNum;
	pyl->numDevicesFound = 0;
	for (i = 0; i < PYL_MAX_DEVICES_IN_GROUP; i++) {
		pyl_setAdr(pyl,i+1);
		res = pyl_getProtocolVersion (pyl);
		if (res != PYL_OK) break;
		pyl->numDevicesFound++;
	}
	if (pyl->numDevicesFound) pyl->initialized=1;
	//printf("pyl_setGroup: group: %d, numDevicesFound: %d, initialized: %d\n",groupNum,pyl->numDevicesFound,pyl->initialized);
	return pyl->numDevicesFound;
}


// closes the serial port, e.g. in case of errors. It will be reopened automatically on request
void pyl_closeSerialPort(PYL_HandleT* pyl) {
	if (!pyl) return;
	if (pyl->serFd) {
		uart_close(pyl->serFd,&pyl->ti_save);
		pyl->serFd = 0;
	}
}


int pyl_openSerialPort(PYL_HandleT* pyl) {
	if (pyl->serFd) {
		uart_close(pyl->serFd,&pyl->ti_save);
		pyl->serFd = 0;
	}

	pyl->serFd = initSerial (pyl->portname,115200,&pyl->ti_save,&pyl->ti);
	if (pyl->serFd <= 0) {
		pyl->serFd = 0;
		return PYL_ERR;
	}
	return PYL_OK;
}

// open the serial port and query the number of devices, first device is at group num +2, first group is 0
// returns -1 on failure opening the serial port or the number of devices found
int pyl_connect(PYL_HandleT* pyl, int groupNum, char *portname) {
	if (!pyl) return -1;
	pyl->portname = strdup(portname);

	int rc = pyl_openSerialPort(pyl);
	if (rc == PYL_OK) rc = pyl_setGroup (pyl, groupNum);
	return rc;
}


// set device address in group, first device is 1
void pyl_setAdr(PYL_HandleT* pyl, int Adr) {
	pyl->adr = Adr;
}


int pyl_numDevices(PYL_HandleT* pyl) { return pyl->numDevicesFound; };
