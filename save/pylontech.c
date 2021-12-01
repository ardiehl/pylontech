
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "util.h"
#include <signal.h>
#include <stdint.h>
#include "uart.h"
#include "pylontech.h"
//#define debug



#define DEFPORTNAME "/dev/ttyUSB0"
#define DEFBAUDRATE 115200

#ifdef debug
int verbose = 2;
#else
int verbose = 0;
#endif

#define SCAN_MAX_DEVICES 32
#define READ_INTERVAL_SECS 2



const char * cid2RespTxt1[] = {"ok","VER error","CHKSUM error","LCHKSUM error","CID2 invalidation","Command format error","Invalid data"};
const char * cid2RespTxt2[] = {"ADR error","Communication error"};
const char * emptyString = "";
char command;

int protocolVersion = 0x20;
int adr = 2;						// fist device in my setup (5x3000) is 2, different address for each battery,support a maximum of 254 batteries





const char * cid2ResponseTxt (int cid2) {
	if ((cid2 < 1) || (cid2 > 0x91)) return (emptyString);
	if (cid2 <= 6) return (cid2RespTxt1[cid2-1]);
	if ((cid2 == 0x91) || (cid2 == 0x92)) return cid2RespTxt2[cid2-0x91];
	return emptyString;

}


void printBin(int bits, int data) {
	int mask = 1 << (bits-1);

	while (bits) {
		if (data & mask) printf("1"); else printf("0");
		mask = mask >> 1;
		bits--;
	}
}

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


void bufAdd1 (char * dest, int data) {
	char st[3];

	sprintf (st,"%02x",data);
	strupper(st);
	strcat(dest,st);
}

void bufAdd2 (char * dest, int data) {
	char st[5];

	sprintf (st,"%04x",data);
	strupper(st);
	strcat(dest,st);
}


typedef struct {
	int  ver;			// length = 1 (00..ff)
	int  adr;
	int  cid1;
	int  cid2;
	char *info;			// can be NULL, otherwise hex ascii data to be sent
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


void packetClear (packetDataT * pa) {
	memset(pa,0,sizeof(packetDataT));
	pa->ver = protocolVersion;
	pa->adr = adr;
}


packetDataT * packetAllocate(int cid1, int cid2) {
	packetDataT * pa = malloc(sizeof(packetDataT));
	if(pa==NULL) return pa;
	packetClear (pa);
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
	bufAdd1(buf,pa->ver);		// VER
	bufAdd1(buf,pa->adr);
	bufAdd1(buf,pa->cid1);
	bufAdd1(buf,pa->cid2);
	length = calc_lengthFiled(infoLen);
	bufAdd2(buf,length);	// Length field, 4 Bit checksum + 12 bit info length
	if (infoLen > 0) {
		strcat(buf,pa->info);
	}
	temp = buf; temp ++;						// we need to calculate the checksum without SOI
	chk = calc_checksum(temp,strlen(temp));
	bufAdd2(buf,chk);
	strcat(buf,"\r");							// EOI 0x0d
	VPRINTF(2,"packetPrepareData: chk: 0x%04x, length field: 0x%04x\n",chk,length);
	strupper(buf);
	return buf;
}



void usage(void) {
        printf("Usage: pylontech [OPTION]...\n" \
        "  -h, --help         display help and exit\n" \
        "  -d, --device       specify device, default: %s\n" \
        "  -b, --baud         specify serial baudrate, default: %d\n" \
        "  -a, --adr          Pylontech device address\n" \
        "  -s, --scan         Scan for devices at address 1 to 255\n" \
        "  -y, --SystemParam  Show system parameter\n" \
        "  -m, --manufact     Show manufacturer information\n" \
        "  -s, --server       influxdb server name or ip\n" \
        "  -n, --db           database name\n" \
        "  -v, --verbose[=x]  increase verbose level\n"
        "  -y, --syslog       log to syslog insead of stderr\n"
        "  -e, --version      show version\n"
        ,DEFPORTNAME,DEFBAUDRATE);
        exit (1);
}



void exit_handler(void)
{
    //LOG(0,"terminated");

	VPRINTF(2,"exit_handler called\n");

	uart_close();
    kb_normal();
}


void sigterm_handler(int signum) {
	VPRINTF(2,"sigterm_handler called\n");
	exit_handler();
}

// this is the maximum possible length plus a 0 byte
#define RECEIVE_BUFSIZE 24+4096+1
// min len incl. length field
#define PKT_LEN_MIN (1+(6*2))

int hex2int(char *hex, int numBytes) {
    int val = 0;
    uint8_t nibble;
    int numNibbles = numBytes*2;

    while (numNibbles) {
		numNibbles--;
        // get current character then increment
        nibble = *hex++;
        // transform hex character to the 4bit equivalent number, using the ascii table indexes
        if (nibble >= '0' && nibble <= '9') nibble -= '0';
        else if (nibble >= 'a' && nibble <='f') nibble = nibble - 'a' + 10;
        else if (nibble >= 'A' && nibble <='F') nibble = nibble - 'A' + 10;
        // shift 4 to make space for new digit, and add the 4 bits of the new digit
        val = (val << 4) | (nibble & 0xF);
    }
    return val;
}

void getStringFromHex(char *src, char *dst, int numChars) {

	while (numChars) {
		*dst = (char)hex2int(src,1); src+=2; dst++; numChars--;
	}
}

packetDataT * packetReceive () {
	char buf[RECEIVE_BUFSIZE];
	char *p = &buf[0];
	int res,length,chk,chkExpected,len,infoLen;
	packetDataT *pa;

	memset(buf,0,RECEIVE_BUFSIZE);

	// wait for the start char
	//uart_setCharsAndTimeout (1,5*10);
	//uart_setCharsAndTimeout (1,1);

	res = uart_read_bytes(p,1,250);	// timeout 1/4 seconds
	if (res == 1) {
		if (buf[0] != 0x7e) {
			printf("packetReceive: received invalid start char (0x0%x), expected 0x7e\n",buf[0]);
			return NULL;
		}
	} else {
		VPRINTF(1,"packetReceive: did not receive SOI, got %d bytes\n",res);
		return NULL;
	}
	p++;

	res = uart_read_bytes(p,PKT_LEN_MIN-1,100);	// timeout 100ms between byte(s)
	if (res != PKT_LEN_MIN-1) {
		printf("packetReceive: got only %d bytes but expected %d bytes\n",res,PKT_LEN_MIN-1);
		return NULL;
	}
	// we have the packet and the last 4 bytes are the length in hex ascii (16 bit), set ptmp to the start of length
	p = p + res - 4;
	length = hex2int(p,2);
	chkExpected = calc_lchecksum (length);
	chk = (((length & 0x0f000) >> 12) & 0x0f);
	VPRINTF(2,"packetReceive: length field is '%s', lchk is: 0x0%x, lchkExpected: 0x0%x\n",p,chk,chkExpected);
	if (chk != chkExpected) {
		printf("packetReceive: invalid checksum in length field, got 0x0%x, expected 0x0%x\n",chk,chkExpected);
		return NULL;
	}
	// Calculate remaining chars to receive
	infoLen = (length & 0x0fff);
	len = infoLen + 4 + 1;							// info + checksum (16 bit) + EOI(0x0d)
	p = strend(buf);
	res = uart_read_bytes(p,len,100);							// read remaining @ end of string
	if (res != len) {
		printf("packetReceive: got only %d bytes for the second part of a packet but expected %d bytes\n",res,len);
		return NULL;
	}
	// calculate the expected checksum
	p = buf; p++;									// without SOI
	chkExpected = calc_checksum(p,strlen(p)-1-4);	// minus EOI minus CHKSUM
	// lets check the checksum we got
	p = strend(buf)-1-4;
	chk = hex2int(p,2);
	VPRINTF(2,"packetReceive: chk is: 0x0%x, chkExpected: 0x0%x, packet: '",chk,chkExpected);
	if (verbose >= 2) {
			dumpBuffer(buf,strlen(buf)); printf("' '");
			dumpBuffer(p,strlen(p)); printf("'\n");
	}
	if (chk != chkExpected) {
		printf("packetReceive: invalid checksum, got 0x0%x, expected 0x0%x\n",chk,chkExpected);
		return NULL;
	}

	// we got it and the checksums are ok, convert from hex ascii
	p = buf;
	p++;			// SOI
	pa = packetAllocate(0,0);
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
	return pa;
}

// converts pa to hex ascii and sends it, free pa if packed has been sent
int packetSend (packetDataT * pa) {
	char * packetStr;
	int len,res;

	packetStr = packetPrepareData (pa);			// convert to hex ascii including SOI, EOI,
												// length field with length checksum and packet checksum
	if (packetStr == NULL) return ERR;			// no memory, not in these days ;-)
	len = strlen(packetStr);
	res = uart_write_bytes(packetStr, len);
	if (res != len) {							// something went wrong
		printf("Error sending data, res: %d, len: %d\n",res,len);
		free(packetStr);
		return ERR;
	}
	if (verbose > 2) {
		printf ("sent packet -> ");
		dumpPacket(pa);
	}
	free(pa);									// not needed any longer
	return OK;
}


char *commandName(int command) {
	switch (command) {
		case CID2_GetAnalogValue: return "GetAnalogValue";
		case CID2_GetAlarmData: return "GetAlarmData";
		case CID2_GetSystemParameter: return "GetSystemParameter";
		case CID2_GetCommunicationProtocolVersion: return "GetCommunicationProtocolVersion";
		case CID2_GetManufacturerInformation: return "GetManufacturerInformation";
		case CID2_GetChargeDischargeManagementInformation: return "GetChargeDischargeManagementInformation";
		case CID2_GetSerialNumberOfEquipment: return "GetSerialNumberOfEquipment";
		case CID2_SetChargeDischargeManagementInformation: return "SetChargeDischargeManagementInformation";
		case CID2_TurnOff: return "TurnOff";
	}
	return "";
}


packetDataT * sendCommandAndReceive (
				int CID2,
				char * dataHexAscii,
				int expectedInfoLength) {

	int res;
	int infoLen = 0;
	packetDataT * pa = packetAllocate(LI_BAT_DATA, CID2);


	if (!pa) return NULL;
	pa->info = dataHexAscii;

	res = packetSend (pa);
	if (res != 0) {
		printf("%s: packet send failed, res: %d\n",commandName(CID2),res);
		packetFree(pa);
		return NULL;
	}
	pa = packetReceive ();
	if (! pa) {
		packetFree(pa);
		return NULL;
	}

	if (expectedInfoLength) {
		if (pa->info) infoLen = strlen(pa->info);
		if (expectedInfoLength != infoLen) {
			printf("%s: expected to receive %d bytes of data but got only %d bytes\n",commandName(CID2),expectedInfoLength,infoLen);
			packetFree(pa);
			return NULL;
		}
	}
	return pa;
}


int getProtocolVersion () {
	int res;
	packetDataT * pa;

	//printf("calling packet allocated");
	pa = packetAllocate(LI_BAT_DATA, CID2_GetCommunicationProtocolVersion);
	if(pa==NULL) return 1;						// not these days ;-)
	//printf("packet allocated");
	res = packetSend (pa);
	if (res != 0) {
		printf("getProtocolVersion: packet send failed\n");
		packetFree(pa);
		return ERR;
	}
	pa = packetReceive ();
	if (! pa) {
		packetFree(pa);
		return ERR;
	}
	VPRINTF(1,"getProtocolVersion: got response, version is 0x%02x\n",pa->ver);
	protocolVersion = pa->ver;
	packetFree(pa);
	return OK;
}


int getAnalogValue () {
	int res;
	packetDataT * pa;

	//printf("calling packet allocated");
	pa = packetAllocate(LI_BAT_DATA, CID2_GetAnalogValue);
	pa->info = strdup("FF");					// FF=get data for all packs
	if(pa==NULL) return 1;						// not these days ;-)
	//printf("packet allocated");
	res = packetSend (pa);
	if (res != 0) {
		printf("getAnalogValue: packet send failed\n");
		packetFree(pa);
		return ERR;
	}
	pa = packetReceive ();
	if (! pa) {
		packetFree(pa);
		return ERR;
	}
	printf("we go a return packet\n");
	packetFree(pa);
	return OK;
}


void test() {
	packetDataT * pa;
	char * packetStr;

	pa = packetAllocate(0x46, 0x42);
	pa->ver = 0x20;
	pa->adr = 2;
	pa->info = strdup("02");
	packetStr = packetPrepareData (pa);

	dumpBuffer(packetStr,strlen(packetStr)); printf("\n");
	free(packetStr);
	free(pa);

}


// returns number of devices found, sets adr to first device
int scan (int scanVerbose) {
	int numFound=0;
	int res,firstAdr;

	firstAdr = 0;
	for (int i=1;i<SCAN_MAX_DEVICES;i++) {
		if (scanVerbose) { printf("\rChecking address %d ",i); fflush(stdout); }
		adr = i;
		res = getProtocolVersion ();
		if (res == OK) {
			if (scanVerbose) printf("\rfound device at address %d\n",i);
			numFound++;
			if(!firstAdr) adr=i;
		}
	}
	adr = firstAdr;
	if (numFound) printf("\nDetected %d devices\n",numFound); else printf("no devices found\n");
	return numFound;
}


int getSystemParameter (PYL_SystemParameterT *sp) {
	int res,len;
	packetDataT * pa;
	char * p;

	//printf("calling packet allocated");
	pa = packetAllocate(LI_BAT_DATA, CID2_GetSystemParameter);
	if(pa==NULL) return 1;						// not these days ;-)
	res = packetSend (pa);
	if (res != 0) {
		printf("getSystemParameter: packet send failed\n");
		packetFree(pa);
		return ERR;
	}
	pa = packetReceive ();
	if (! pa) {
		packetFree(pa);
		return ERR;
	}
	len = 0;
	if (pa->info) len = strlen(pa->info);
	VPRINTF(1,"getSystemParameter: got response, data len: %d\n",len);
	if (len) {
		p = pa->info;		// info contains the response data in hex ascii, each field is 2 bytes = 4 hex nibbles
		sp->UnitCellVoltage = hex2int(p,2); p+=4;
		sp->UnitCellLowVoltageThreshold = hex2int(p,2); p+=4;
		sp->UnitCellUnderVoltageThreshold = hex2int(p,2); p+=4;
		sp->ChargeUupperLimitTemperature = hex2int(p,2); p+=4;
		sp->ChargeLowerLimitTemperature = hex2int(p,2); p+=4;
		sp->ChargeLowerLimitCurrent = hex2int(p,2); p+=4;
		sp->UpperLimitOfTotalVoltage = hex2int(p,2); p+=4;
		sp->LowerLimitOfTotalVoltage = hex2int(p,2); p+=4;
		sp->UnderVoltageOfTotalVoltage = hex2int(p,2); p+=4;
		sp->DischargeUpperLimitTemperature = hex2int(p,2); p+=4;
		sp->DischargeLowerLimitTemperature = hex2int(p,2); p+=4;
		sp->DischargeLowerLimitCurrent = hex2int(p,2); p+=4;
	}
	packetFree(pa);
	return OK;
}


void showSystemParameter() {
	int res;

	PYL_SystemParameterT sp;
	res = getSystemParameter(&sp);
	if (res == OK) {
		printf("               UnitCellVoltage: %d\n",sp.UnitCellVoltage);
		printf("   UnitCellLowVoltageThreshold: %d\n",sp.UnitCellLowVoltageThreshold);
		printf(" UnitCellUnderVoltageThreshold: %d\n",sp.UnitCellUnderVoltageThreshold);
		printf("  ChargeUupperLimitTemperature: %d\n",sp.ChargeUupperLimitTemperature);
		printf("   ChargeLowerLimitTemperature: %d\n",sp.ChargeLowerLimitTemperature);
		printf("       ChargeLowerLimitCurrent: %d\n",sp.ChargeLowerLimitCurrent);
		printf("      UpperLimitOfTotalVoltage: %d\n",sp.UpperLimitOfTotalVoltage);
		printf("      LowerLimitOfTotalVoltage: %d\n",sp.LowerLimitOfTotalVoltage);
		printf("    UnderVoltageOfTotalVoltage: %d\n",sp.UnderVoltageOfTotalVoltage);
		printf("DischargeUpperLimitTemperature: %d\n",sp.DischargeUpperLimitTemperature);
		printf("DischargeLowerLimitTemperature: %d\n",sp.DischargeLowerLimitTemperature);
		printf("    DischargeLowerLimitCurrent: %d\n",sp.DischargeLowerLimitCurrent);
	} else printf("failed to read system parameter from device %d, rc:%d\n",adr,res);
}





int getManufacturerInformation (PYL_ManufacturerInformationT *mi) {
	int res,len;
	packetDataT * pa;
	char * p;

	pa = packetAllocate(LI_BAT_DATA, CID2_GetManufacturerInformation);
	if(pa==NULL) return 1;						// not these days ;-)
	res = packetSend (pa);
	if (res != 0) {
		printf("getManufacturerInformation: packet send failed\n");
		packetFree(pa);
		return ERR;
	}
	pa = packetReceive ();
	if (! pa) {
		packetFree(pa);
		return ERR;
	}
	len = 0;
	if (pa->info) len = strlen(pa->info);
	VPRINTF(1,"getManufacturerInformation: got response, data len: %d\n",len);
	if (len) {
		p = pa->info;		// info contains the response data in hex ascii, each field is 2 bytes = 4 hex nibbles
		memset(mi,0,sizeof(*mi));
		getStringFromHex(p, mi->DeviceName, 10); p+=20;
		mi->SoftwareVersion[0] = hex2int(p,1); p+=2;
		mi->SoftwareVersion[1] = hex2int(p,1); p+=2;
		getStringFromHex(p, mi->ManufactureName, 20);
	}
	packetFree(pa);
	return OK;
}



void showManufacturerInformation() {
	int res;

	PYL_ManufacturerInformationT mi;
	res = getManufacturerInformation(&mi);
	if (res == OK) {
		printf(	"    Device name: %s\n" \
				"Softwareversion: %d.%d\n" \
				"   Manufacturer: %s\n",
				mi.DeviceName,mi.SoftwareVersion[0],mi.SoftwareVersion[1],mi.ManufactureName);

	}  else printf("failed to read manufacturer information from device %d, rc:%d\n",adr,res);
}


int main (int argc, char **argv) {
	char * portname = NULL;
	int baudrate = DEFBAUDRATE;
	int res = 0;
	char c;
	int option_index = 0;

	//test();
	//exit (0);

    static struct option long_options[] =
        {
                {"help",        no_argument,       0, 'h'},
                {"device",      required_argument, 0, 'd'},
                {"scan",        no_argument,       0, 's'},
                {"systemparam", no_argument,       0, 'y'},
                {"manufact",    no_argument,       0, 'm'},
                {"verbose",     optional_argument, 0, 'v'},
                {"baud",        required_argument, 0, 'b'},
                {"adr",			required_argument, 0, 'a'},

                {0, 0, 0, 0}
        };

    while ((c = getopt_long (argc, argv, "hd:v::b:a:ym",long_options, &option_index)) != -1) {
        switch (c) {
			case 'v':
				if (optarg) {
					res = strtol (optarg,NULL,10);
					if (errno) {
						fprintf(stderr,"verbosity is not a number\n"); usage();
					}
					verbose = res;
				} else verbose++;
				break;
            case 'h': usage(); break;
            case 'd': portname = strdup(optarg); break;
            case 'a':
				adr = strtol (optarg,NULL,10);
				if ((errno) || (adr < 1) || (adr > 254)) {
					fprintf(stderr,"Invalid adr specified\n"); usage();
				}
				break;
            case 'b':
				baudrate = strtol (optarg,NULL,10);
				if ((errno) || (baudrate < 1200)) {
					fprintf(stderr,"Invalid baudrate specified\n"); usage();
				}
				break;
			case '?': usage(); break;
			case 's': command = 's'; break;
			case 'y': command = 'y'; break;
			case 'm': command = 'm'; break;
		}
	}

	if (portname == NULL) portname = strdup(DEFPORTNAME);

	printf("verbose: %d\n",verbose);

	//atexit(exit_handler);
	//signal(SIGTERM, sigterm_handler);

	res = initSerial (portname, baudrate); // 115200 or 9600 depending on dip switch
	if (res != 0) {
		fprintf(stderr,"failed to initialize serial port (%s) at %d baud\n",portname,baudrate);
		exit(res);
	}

	VPRINTF(1,"Serial port opened\n");

	switch (command) {
		case 's':		// scan
			scan(1);
			exit (0);
			break;
		case 'y':
			showSystemParameter();
			break;
		case 'm':
			showManufacturerInformation();
			break;
	}

	VPRINTF(1,"res of getProtocolVersion: %d\n",res);
	if (res != 0) {
		fprintf(stderr,"failed to get protocol version\n");
		exit(1);
	}

	printf("terminated\n");


    return res;
}
