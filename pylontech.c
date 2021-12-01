
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
#include "pylontechapi.h"
//#define debug





#ifdef debug
int verbose = 2;
#else
int verbose = 0;
#endif

#define SCAN_MAX_DEVICES 32
#define READ_INTERVAL_SECS 2



PYL_HandleT* pyl;		// api handle


void exit_handler(void) {
    //LOG(0,"terminated");

	VPRINTF(2,"exit_handler called\n");

	if(pyl)	uart_close(pyl->serFd);
    kb_normal();
}


void sigterm_handler(int signum) {
	VPRINTF(2,"sigterm_handler called\n");
	exit_handler();
}



void showAnalogData(PYL_HandleT* pyl) {
	PYL_AnalogDataT pd;
	int i;

	int res = pyl_getAnalogData(pyl,&pd);
	if (res == PYL_OK) {
		printf ("%3d cells, Voltage V:",pd.cellsCount);
		for (i=0;i<pd.cellsCount;i++)
			printf("%7.3f",(float)pd.cellVoltage[i]/1000);
		printf("\n%d temperature values: ",pd.tempCount);
		for (i=0;i<pd.tempCount;i++)
			printf(" %d",pd.temp[i]);
		printf("\n" \
			   "             current: %7.3f A\n" \
			   "             voltage: %7.3f V\n"
				,(float)pd.current/PYL_MODULE_CURRENT_DIVIDER,
				(float)pd.voltage/PYL_MODULE_VOLTAGE_DIVIDER);
		printf("       pack capacity: %d\n",pd.capacity);
		printf("  remaining capacity: %d\n",pd.remainingCapacity);
		printf("         cycle count: %d\n",pd.cycleCount);

	} else printf("failed to read analog values from device %d, rc:%d\n",pyl->adr,res);

}



void showSystemParameter(PYL_HandleT* pyl) {
	int res;

	PYL_SystemParameterT sp;
	res = pyl_getSystemParameter(pyl,&sp);
	if (res == PYL_OK) {
		printf("          cellHighVoltageLimit: %7.3f V\n",((float)sp.cellHighVoltageLimit)/PYL_MODULE_VOLTAGE_DIVIDER);
		printf("           cellLowVoltageLimit: %7.3f V\n",((float)sp.cellLowVoltageLimit)/PYL_MODULE_VOLTAGE_DIVIDER);
		printf("         cellUnderVoltageLimit: %7.3f V\n",((float)sp.cellUnderVoltageLimit)/PYL_MODULE_VOLTAGE_DIVIDER);
		printf("    chargeHighTemperatureLimit: %7d\n",sp.chargeHighTemperatureLimit);
		printf("     chargeLowTemperatureLimit: %7d\n",sp.chargeLowTemperatureLimit);
		printf("            chargeCurrentLimit: %7.3f A\n",((float)sp.chargeCurrentLimit)/PYL_MODULE_VOLTAGE_DIVIDER);
		printf("        moduleHighVoltageLimit: %7.3f V\n",((float)sp.moduleHighVoltageLimit)/PYL_MODULE_VOLTAGE_DIVIDER);
		printf("         moduleLowVoltageLimit: %7.3f V\n",((float)sp.moduleLowVoltageLimit)/PYL_MODULE_VOLTAGE_DIVIDER);
		printf("       moduleUnderVoltageLimit: %7.3f V\n",((float)sp.moduleUnderVoltageLimit)/PYL_MODULE_VOLTAGE_DIVIDER);
		printf(" dischargeHighTemperatureLimit: %7d\n",sp.dischargeHighTemperatureLimit);
		printf("  dischargeLowTemperatureLimit: %7d\n",sp.dischargeLowTemperatureLimit);
		printf("         dischargeCurrentLimit: %7.3f A\n",((float)sp.dischargeCurrentLimit)/PYL_MODULE_VOLTAGE_DIVIDER);
	} else printf("failed to read system parameter from device %d, rc:%d\n",pyl->adr,res);
}





void showManufacturerInformation(PYL_HandleT* pyl) {
	int res;

	PYL_ManufacturerInformationT mi;
	res = pyl_getManufacturerInformation(pyl,&mi);
	if (res == PYL_OK) {
		printf(	"    Device name: %s\n" \
				"Softwareversion: %d.%d\n" \
				"   Manufacturer: %s\n",
				mi.deviceName,mi.softwareVersion[0],mi.softwareVersion[1],mi.manufacturerName);

	}  else printf("failed to read manufacturer information from device %d, rc:%d\n",pyl->adr,res);
}



char * statusStr (int status) {
	switch (status) {
		case 0:	return "OK";
		case 1: return "!BelowLimit";
		case 2: return "!AboveLimit";
	}
	return "!OTHER";
}

void showAlarmInfo(PYL_HandleT* pyl) {
	int res;
	PYL_AlarmInfoT ai;
	int i;

	res = pyl_getAlarmInfo(pyl,&ai);
	if (res == PYL_OK) {
		printf ("   %3d cells, status:",ai.cellsCount);
		for (i=0;i<ai.cellsCount;i++)
			printf(" %s",statusStr(ai.cellVoltageStatus[i]));
		printf("\n%d temperature values:",ai.tempCount);
		for (i=0;i<ai.tempCount;i++)
			printf(" %s",statusStr(ai.tempStatus[i]));
		printf("\n      charge current: %s\n ",statusStr(ai.chargeCurrentStat));
		printf("  discharge current: %s\n ",statusStr(ai.dischargeCurrentStat));
		printf("         status 1-5:");
		for (i=0;i<5;i++)
			printf(" %02x",ai.status[i]);
		printf("\n");

	}  else printf("failed to read alarm information from device %d, rc:%d\n",pyl->adr,res);
}



void showChargeDischargeInfo(PYL_HandleT* pyl) {
	int res;
	PYL_ChargeDischargeInfoT cd;

	res = pyl_getChargeDischargeInfo(pyl, &cd);
	if (res == PYL_OK) {
		printf("   charge voltage limit: %7.3f V\n",(float)cd.chargeVoltageLimit/1000);
		printf("discharge voltage limit: %7.3f V\n",(float)cd.dischargeVoltageLimit/1000);
		printf("   charge current limit: %7.1f A\n",(float)cd.chargeCurrentLimit/10);
		printf("discharge current limit: %7.1f A\n",(float)cd.dischargeCurrentLimit/1000);  // differs from documentation
		printf("charge discharge status: 0x%02x\n",cd.chargeDischargeStatus);
		printf("       charging enabled: %d\n",(cd.chargeDischargeStatus & 0x80) >> 7);
		printf("    discharging enabled: %d\n",(cd.chargeDischargeStatus & 0x40) >> 6);
		printf("   charge immediately 1: %d\n",(cd.chargeDischargeStatus & 0x20) >> 5);
		printf("   charge immediately 2: %d\n",(cd.chargeDischargeStatus & 0x10) >> 4);
		printf("         charge request: %d\n",(cd.chargeDischargeStatus & 0x08) >> 3);

	}  else printf("failed to read charge discharge information from device %d, rc:%d\n",pyl->adr,res);
}

void showSerialNumber(PYL_HandleT* pyl, int verb) {
	int res;

	PYL_SerialNumberT mi;
	res = pyl_getSerialNumber(pyl,&mi);
	if (res == PYL_OK) {
		printf(	"Serial Number: %s\n", mi.sn);
	}  else if (verb) printf("failed to read serial number device %d, rc:%d\n",pyl->adr,res);
}



// returns number of devices found, sets adr to first device
int scan (PYL_HandleT* pyl, int scanVerbose) {
	int numFound=0;
	int res;
	PYL_ManufacturerInformationT mi;
	PYL_SerialNumberT sn;

	for (int i=1;i<SCAN_MAX_DEVICES;i++) {
		if (scanVerbose) { printf("\rChecking address %d ",i); fflush(stdout); }
		pyl_setAdr(pyl,i);
		res = pyl_getProtocolVersion (pyl);
		if (res == PYL_OK) {
			if (scanVerbose) printf("\rfound device at address %d",i);
			numFound++;
			res = pyl_getManufacturerInformation (pyl,&mi);
			if (res == PYL_OK) printf(" %s V%d.%d",mi.deviceName,mi.softwareVersion[0],mi.softwareVersion[1]);
			res = pyl_getSerialNumber(pyl,&sn);
			if (res == PYL_OK) printf(" S/N %s",sn.sn);
			printf("\n");
		}
	}

	if (numFound) printf("\rDetected %d devices           \n",numFound); else printf("no devices found\n");
	return numFound;
}


void showSummary() {
	int i,j,res;
	PYL_ManufacturerInformationT mi;
	PYL_AnalogDataT pd;
	PYL_SerialNumberT sn;
	float percentCharge;
	int min,max;
	int totalCapacity = 0;
	int remainingCapacity = 0;
	float kwh;
	int designVoltage = 0;
	int capacityNotUsable;
	int totalCurrent = 0;

			//      123456789012345671234567890 12345678901234567    123456 1234567 1234567 123456 1234567 123456 123456
	printf(		"                                                    ---------pack--------- -cell voltage- -temperature-\n" \
				"   manufacturer        model     serial             charge voltage current   min      max    min    max\n" \
				"-------------------------------------------------------------------------------------------------------\n");
	for (i=1;i<=pyl_numDevices(pyl);i++) {
		pyl_setAdr(pyl,i);
		pyl_getManufacturerInformation(pyl,&mi);
		pyl_getSerialNumber(pyl,&sn);

		printf("%2d %17s%s %18s ",i,mi.manufacturerName,mi.deviceName,sn.sn);
		fflush(stdout);
		res = pyl_getAnalogData(pyl,&pd);
		if (res == PYL_OK) {
			totalCapacity += pd.capacity;
			remainingCapacity += pd.remainingCapacity;
			totalCurrent += pd.current;
			percentCharge = ((float)pd.remainingCapacity / (float)pd.capacity)*100;
			printf("%7.2f%%%8.2f%8.2f", percentCharge, (float)pd.voltage/PYL_MODULE_VOLTAGE_DIVIDER, (float)pd.current / PYL_MODULE_CURRENT_DIVIDER);
			max=0; min=0xffff;
			for (j=0;j<pd.cellsCount;j++) {
				if (pd.cellVoltage[j] > max) max = pd.cellVoltage[j];
				if (pd.cellVoltage[j] < min) min = pd.cellVoltage[j];
			}
			printf("%6.3f%9.3f",(float)min/PYL_MODULE_VOLTAGE_DIVIDER,(float)max/PYL_MODULE_VOLTAGE_DIVIDER);
			max=0; min=0xffff;
			for (j=0;j<pd.tempCount;j++) {
				if (pd.temp[j] > max) max = pd.temp[j];
				if (pd.temp[j] < min) min = pd.temp[j];
			}
			printf("%7d%7d\n",min,max);

			if (pd.voltage <= 15) designVoltage = 12; /* FIXME */
			else if (pd.voltage <= 30) designVoltage = 24;
			else designVoltage = 48;
		}
	}
	capacityNotUsable = totalCapacity - PLY_CALC_USABLE(totalCapacity);		// wh not usable
	kwh = (float)(totalCapacity-capacityNotUsable) * designVoltage / PYL_MODULE_CAPACITY_DIVIDER / 1000;
	printf("\ntotal capacity %d AH (~ %.2f kWh, ~%.2f KWh per module)\n",(totalCapacity-capacityNotUsable)/PYL_MODULE_CAPACITY_DIVIDER, kwh, kwh / pyl_numDevices(pyl));
	kwh = (float)(remainingCapacity-capacityNotUsable) * designVoltage / PYL_MODULE_CAPACITY_DIVIDER / 1000;
	printf("\remaining capacity %d AH (~ %.2f kWh, ~%.2f KWh per module)\n",(remainingCapacity-capacityNotUsable)/PYL_MODULE_CAPACITY_DIVIDER, kwh, kwh / pyl_numDevices(pyl));
	printf("total current %.2f A (~ %.2f kW)\n",(float)totalCurrent / PYL_MODULE_CURRENT_DIVIDER, ((float)totalCurrent * designVoltage) / PYL_MODULE_CURRENT_DIVIDER/1000);

}


void usage(void) {
        printf("Usage: pylontech [OPTION]...\n" \
        "  -h, --help         display help and exit\n" \
        "  -d, --device       specify device, default: %s\n" \
        "  -b, --baud         specify serial baudrate, default: %d\n" \
        "  -a, --adr          Pylontech device address (1-12)\n" \
        "  -g, --group        Pylontech group address (0-15)\n" \
        "  -s, --scan         Scan for devices at address 1 to 255\n" \
        "  -y, --systemparam  Show system parameter\n" \
        "  -l, --alarm        Show alarm information\n" \
        "  -S, --serial       Show system serial number\n" \
        "  -P, --packdata     Show analog data\n" \
        "  -c, --charge       Show charge / discharge info\n" \
        "  -m, --manufact     Show manufacturer information\n" \
        "  -v, --verbose[=x]  increase verbose level\n"
        "  -y, --syslog       log to syslog insead of stderr\n"
        "  -e, --version      show version\n"
        ,PYL_DEFPORTNAME,PYL_DEFBAUDRATE);
        exit (1);
}




int main (int argc, char **argv) {
	char * portname = NULL;
	int baudrate = PYL_DEFBAUDRATE;
	int res = 0;
	int c;
	int option_index = 0;
	int adr = 1;
	int group = 0;
	char command = 0;


	//test();
	//exit (0);

    static struct option long_options[] =
        {
                {"help",        no_argument,       0, 'h'},
                {"device",      required_argument, 0, 'd'},
                {"group",       required_argument, 0, 'g'},
                {"scan",        no_argument,       0, 's'},
                {"systemparam", no_argument,       0, 'y'},
                {"manufact",    no_argument,       0, 'm'},
                {"serial",      no_argument,       0, 'S'},
                {"alarm",       no_argument,       0, 'l'},
                {"charge",      no_argument,       0, 'c'},
                {"packdata",    no_argument      , 0, 'P'},
                {"verbose",     optional_argument, 0, 'v'},
                {"baud",        required_argument, 0, 'b'},
                {"adr",			required_argument, 0, 'a'},

                {0, 0, 0, 0}
        };

    while ((c = getopt_long (argc, argv, "hd:v::b:a:ymSPslcg",long_options, &option_index)) != -1) {
        switch ((char)c) {
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
				if ((errno) || (adr < 1) || (adr > DEVICES_MAX)) {
					fprintf(stderr,"Invalid adr specified\n"); usage();
				}
				break;
			case 'g':
				group = strtol (optarg,NULL,10);
				if ((errno) || (group < 0) || (group > 15)) {
					fprintf(stderr,"Invalid group, not between 0 and 15\n"); usage();
				}
				break;
            case 'b':
				baudrate = strtol (optarg,NULL,10);
				if ((errno) || (baudrate < 1200)) {
					fprintf(stderr,"Invalid baudrate specified\n"); usage();
				}
				break;
			case '?': usage(); break;
			case 's':
			case 'y':
			case 'm':
			case 'S':
			case 'P':
			case 'l':
			case 'c': command = (char)c; break;
		}
	}

	if (portname == NULL) portname = strdup(PYL_DEFPORTNAME);


	atexit(exit_handler);
	signal(SIGTERM, sigterm_handler);

	// init pylontech api
	pyl = pyl_initHandle();
	res =  pyl_connect(pyl, group, portname);
	if (res < 0) { fprintf(stderr,"error opening serial port %s\n",portname); exit(1); }
	if (res < 1) { fprintf(stderr,"no pylontech devices found\n"); exit(1); }

	// set device for commands below
	pyl_setAdr(pyl,adr);

	printf("%d devices found in group %d\n\n",pyl_numDevices(pyl),group);

	switch (command) {
		case 's':		// scan
			scan(pyl,1);
			break;
		case 'y':
			showSystemParameter(pyl);
			break;
		case 'm':
			showManufacturerInformation(pyl);
			break;
		case 'S':
			showSerialNumber(pyl,1);
			break;
		case 'P':
			showAnalogData(pyl);
			break;
		case 'l':
			showAlarmInfo(pyl);
			break;
		case 'c':
			showChargeDischargeInfo(pyl);
			break;
		default:
			showSummary();
	}

    return res;
}
