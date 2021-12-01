
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include "util.h"
#include "uart.h"
#include "pylontechapi.h"
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
		printf("'\n");
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
	bufAddHex1(buf,pa->ver);		// VER
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
	VPRINTF(2,"packetPrepareData: chk: 0x%04x, length field: 0x%04x\n",chk,length);
	strupper(buf);								// make it upper case, dont know if this is needed
	return buf;
}


// this is the maximum possible length plus a 0 byte
#define RECEIVE_BUFSIZE 24+4096+1
// min len incl. length field
#define PKT_LEN_MIN (1+(6*2))


packetDataT * packetReceive (PYL_HandleT* pyl) {
	char buf[RECEIVE_BUFSIZE];
	char *p = &buf[0];
	int res,length,chk,chkExpected,len,infoLen;
	packetDataT *pa;

	memset(buf,0,RECEIVE_BUFSIZE);

	// wait for the start char
	//uart_setCharsAndTimeout (1,5*10);
	//uart_setCharsAndTimeout (1,1);

	// wait for SOI
	res = uart_read_bytes(pyl->serFd, p,1,250);	// timeout 1/4 seconds
	if (res == 1) {
		if (buf[0] != 0x7e) {
			printf("packetReceive: received invalid SOI char (0x0%x), expected 0x7e\n",buf[0]);
			uart_flush(pyl->serFd);
			return NULL;
		}
	} else {
		VPRINTF(1,"packetReceive: did not receive SOI, got %d bytes\n",res);
		uart_flush(pyl->serFd);
		return NULL;
	}
	p++;

	// get the fix part of the packet (including the length field
	res = uart_read_bytes(pyl->serFd, p,PKT_LEN_MIN-1,100);
	if (res != PKT_LEN_MIN-1) {
		printf("packetReceive: got only %d bytes but expected %d bytes\n",res,PKT_LEN_MIN-1);
		uart_flush(pyl->serFd);
		return NULL;
	}
	// we have the packet and the last 4 bytes are the length in hex ascii (16 bit),
	// set p to the start of the length field
	p = p + res - 4;
	length = hex2int(p,2);
	chkExpected = calc_lchecksum (length);			// calculate the expected length checksum
	chk = (((length & 0x0f000) >> 12) & 0x0f);		// extract the length checksum from the received length field
	VPRINTF(2,"packetReceive: length field is '%s', lchk is: 0x0%x, lchkExpected: 0x0%x\n",p,chk,chkExpected);
	if (chk != chkExpected) {
		printf("packetReceive: invalid checksum in length field, got 0x0%x, expected 0x0%x\n",chk,chkExpected);
		uart_flush(pyl->serFd);
		return NULL;
	}
	// Calculate remaining chars to receive
	infoLen = (length & 0x0fff);					// remove checksum from length field
	len = infoLen + 4 + 1;							// info + checksum (16 bit) + EOI(0x0d)
	p = strend(buf);
	res = uart_read_bytes(pyl->serFd,p,len,100);	// receive remaining @ end of string
	if (res != len) {
		printf("packetReceive: got only %d bytes for the second part of a packet but expected %d bytes\n",res,len);
		uart_flush(pyl->serFd);
		return NULL;
	}
	// calculate the expected checksum
	p = buf; p++;									// without SOI
	chkExpected = calc_checksum(p,strlen(p)-1-4);	// minus EOI minus CHKSUM
	// lets check the checksum we got
	p = strend(buf)-1-4;							// extract the
	chk = hex2int(p,2);								// received checksum
	VPRINTF(2,"packetReceive: chk is: 0x0%x, chkExpected: 0x0%x, packet: '",chk,chkExpected);
	if (verbose >= 2) {
			dumpBuffer(buf,strlen(buf)); printf("' '");
			dumpBuffer(p,strlen(p)); printf("'\n");
	}
	if (chk != chkExpected) {
		printf("packetReceive: invalid checksum, got 0x0%x, expected 0x0%x\n",chk,chkExpected);
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
	VPRINTF(2,"packetReceive: received packet, rtn: 0x%02x %s\n",pa->cid2,cid2ResponseTxt(pa->cid2));
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
		printf("Error sending data, res: %d, len: %d\n",res,len);
		free(packetStr);
		return PYL_ERR;
	}
	if (verbose > 2) {
		printf ("sent packet -> ");
		dumpPacket(pa);
	}
	free(pa);									// not needed any longer
	return PYL_OK;
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


packetDataT * sendCommandAndReceive (
				PYL_HandleT* pyl,
				int CID2,
				char * dataHexAscii,
				int expectedInfoLength) {

	int res;
	int infoLen = 0;
	packetDataT * pa = packetAllocate(pyl, LI_BAT_DATA, CID2);


	if (!pa) return NULL;
	pa->info = dataHexAscii;

	res = packetSend (pyl,pa);
	if (res != 0) {
		printf("%s: packet send failed, res: %d\n",commandName(CID2),res);
		packetFree(pa);
		return NULL;
	}
	pa = packetReceive (pyl);
	if (! pa) {
		packetFree(pa);
		return NULL;
	}

	if (pa->cid2 != 0) {		// 0 means ok otherwise error code
		printf("%s: received error code 0x%02x (%s)\n",commandName(CID2),pa->cid2,cid2ResponseTxt (pa->cid2));
		packetFree(pa);
		return NULL;
	}

	if (expectedInfoLength) {
		if (pa->info) infoLen = strlen(pa->info);
		if (expectedInfoLength > infoLen) {
			printf("%s: expected to receive %d bytes of data but got only %d bytes\n",commandName(CID2),expectedInfoLength,infoLen);
			packetFree(pa);
			return NULL;
		}
		if (expectedInfoLength != infoLen)
			printf("%s: Info: expected to receive %d bytes of data, got %d bytes\n",commandName(CID2),expectedInfoLength,infoLen);
	}
	return pa;
}


int pyl_getProtocolVersion (PYL_HandleT* pyl) {
	packetDataT * pa = sendCommandAndReceive (pyl,CID2_GetCommunicationProtocolVersion, NULL, 0);
	if (!pa) return PYL_ERR;

	VPRINTF(1,"getProtocolVersion: got response, version is 0x%02x\n",pa->ver);
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
	//printf("Cell count: %d\n",pd->cellsCount);

	// get cell voltages
	for (i=0;i<pd->cellsCount;i++) {
		if (i < CELLS_MAX) pd->cellVoltage[i] = paInfoGetInt(pa,2);
		//printf("%d: %dmV ",i,pd->cellVoltage[i]);
	}
	//printf("\n");

	pd->tempCount = paInfoGetInt(pa,1);
	//printf("Temp count: %d\n",pd->tempCount);
	// get temperature values
	for (i=0;i<pd->tempCount;i++) {
		if (i < TEMPS_MAX) pd->temp[i] = (paInfoGetInt(pa,2)-2731)/10;
		//printf("%d: %dmV ",i,pd->temp[i]);
	}
	//printf("\n");

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
	VPRINTF(1,"getSystemParameter: got response, data len: %d\n",len);
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
	VPRINTF(1,"getManufacturerInformation: got response, data len: %d\n",len);
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
	VPRINTF(1,"getSerialNumber: got response, data len: %d\n",len);
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
		if (pyl->serFd) {
			uart_close(pyl->serFd);
			pyl->serFd = 0;
		}
		free(pyl);
	}
}


// sets the group and scans for devices in group, returns number of devices found
int pyl_setGroup (PYL_HandleT* pyl, int groupNum) {
	int i,res;

	if (pyl->serFd <=0) return -1;

	pyl->group = groupNum;
	pyl->numDevicesFound = 0;
	for (i = 0; i < DEVICES_MAX; i++) {
		pyl_setAdr(pyl,i+1);
		res = pyl_getProtocolVersion (pyl);
		if (res != PYL_OK) return pyl->numDevicesFound;
		pyl->numDevicesFound++;
	}
	return pyl->numDevicesFound;
}

// open the serial port and query the number of devices, first device is at group num +2, first group is 0
// returns -1 on failure opening the serial port or the number of devices found
int pyl_connect(PYL_HandleT* pyl, int groupNum, char *portname) {
	if (!pyl) return -1;
	if (pyl->serFd) {
		uart_close(pyl->serFd);
		pyl->serFd = 0;
	}
	pyl->serFd = initSerial (portname,115200);
	//printf("res:%d\n",res);


	return pyl_setGroup (pyl, groupNum);
}


// set device address in group, first device is 1
void pyl_setAdr(PYL_HandleT* pyl, int Adr) {
	pyl->adr = Adr;
}


int pyl_numDevices(PYL_HandleT* pyl) { return pyl->numDevicesFound; };
