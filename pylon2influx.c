/*
 * pylon2influx.h
 *
 * Copyright 2021 Armin Diehl <ad@ardiehl.de>
 *
 * log data from pylontech batteries to influxdb
 *
 * Dec 2, 2021 Armin: initial version
 *
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <stdlib.h>
#include "util.h"
#include <signal.h>
#include <stdint.h>
#include "uart.h"
#include "pylontechapi.h"
#include "influxdb-post/influxdb-post.h"

#define VER "0.00 Armin Diehl <ad@ardiehl.de> Dec 2,2021"
char * ME = "pylon2influx";



PYL_HandleT* pyl;		// pylontech api handle
influx_client_t *iClient;

#define queryIntervalSeconds 15
#ifdef TESTMODE
#define sendIntervalSeconds 12
#else
#define sendIntervalSeconds 60*5
#endif

// buffer max 2 days
#define NUM_RECS_TO_BUFFER_ON_FAILURE 2*24*60 / sendIntervalSeconds

// maximal length of a http line send
#define INFLUX_BUFLEN 2048

char * influxBuf;
size_t influxBufUsed;
int influxBufLen;

void usage(void) {
        PRINTF("Usage: pylontech [OPTION]...\n" \
        "  -h, --help         display help and exit\n" \
        "  -d, --device       specify device (%s)\n" \
        "  -b, --baud         specify serial baudrate (%d)\n" \
        "  -g, --group        Pylontech group address (0-15)\n" \
		"  -s, --server       influxdb server name or ip\n" \
		"  -p, --port         influxdb port (8086)\n" \
        "  -n, --db           database name\n" \
        "  -u, --user         influxdb user name\n" \
        "  -u, --password     influxdb password\n" \
        "  -c, --cache        #entries for influxdb cache (%d)\n" \
        "  -v, --verbose[=x]  increase verbose level\n" \
        "  -y, --syslog       log to syslog insead of stderr\n" \
        "  -Y, --syslogtest   send a testtext to syslog and exit\n" \
        "  -e, --version      show version\n\n" \
        "The cache will be used in case the influxdb server is down. In\n" \
        "that case data will be send when the server is reachable again.\n"
        ,PYL_DEFPORTNAME,PYL_DEFBAUDRATE,NUM_RECS_TO_BUFFER_ON_FAILURE);
        exit (1);
}




int parseArgs (int argc, char **argv) {
	char * portname = NULL;
	int baudrate = PYL_DEFBAUDRATE;
	int res = 0;
	int c;
	int option_index = 0;
	int group = 0;
	char * dbName = NULL;
	char * serverName = NULL;
	char * userName = NULL;
	char * password = NULL;
	int syslog = 0;
	int port = 8086;
	int numQueueEntries = NUM_RECS_TO_BUFFER_ON_FAILURE;

    static struct option long_options[] =
        {
                {"help",        no_argument,       0, 'h'},
                {"device",      required_argument, 0, 'd'},
                {"group",       required_argument, 0, 'g'},
                {"verbose",     optional_argument, 0, 'v'},
                {"baud",        required_argument, 0, 'b'},
                {"server",      required_argument, 0, 's'},
                {"db",          required_argument, 0, 'n'},
                {"user",        required_argument, 0, 'u'},
                {"password",    required_argument, 0, 'p'},
                {"port",        required_argument, 0, 'o'},
                {"syslog",      no_argument      , 0, 'y'},
                {"syslogtest",  no_argument      , 0, 'Y'},

                {0, 0, 0, 0}
        };

    while ((c = getopt_long (argc, argv, "hd:v::b:gs:n:u:p:o:yY",long_options, &option_index)) != -1) {
        switch ((char)c) {
			case 'v':
				if (optarg) {
					res = strtol (optarg,NULL,10);
					if (errno) {
						EPRINTF("verbosity is not a number\n"); usage();
					}
					log_setVerboseLevel(res);
				} else log_incVerboseLevel();
				break;
			case 's': serverName = strdup(optarg); break;
			case 'n': dbName = strdup(optarg); break;
			case 'u': userName = strdup(optarg); break;
			case 'p': password = strdup(optarg); break;
            case 'h': usage(); break;
            case 'd': portname = strdup(optarg); break;
			case 'g':
				group = strtol (optarg,NULL,10);
				if ((errno) || (group < 0) || (group > 15)) {
					EPRINTF("Invalid group, not between 0 and 15\n"); usage();
				}
				break;
			case 'o':
				port = strtol (optarg,NULL,10);
				if ((errno) || (port < 0) || (port > 0xffff)) {
					EPRINTF("Invalid port number\n"); usage();
				}
				break;
            case 'b':
				baudrate = strtol (optarg,NULL,10);
				if ((errno) || (baudrate < 1200)) {
					EPRINTF("Invalid baudrate specified\n"); usage();
				}
				break;
			case 'y': syslog = 1; break;
			case 'Y': 	{
							VPRINTF(0,"%s : sending testtext via syslog\n\n",ME);
							log_setSyslogTarget(ME);
							VPRINTF(0,"testtext via syslog by %s",ME);
							exit(0);
						}
			case '?': usage(); break;
		}
	}

	if (portname == NULL) portname = strdup(PYL_DEFPORTNAME);

	// init pylontech api
	pyl = pyl_initHandle();
	res =  pyl_connect(pyl, group, portname);
	if (res < 0) { EPRINTF("error opening serial port %s\n",portname); return 1; }
	if (res < 1) { EPRINTF("no pylontech devices found on %s\n",portname); return 2; }

	// set device for commands below
	//pyl_setAdr(pyl,adr);
	if (syslog) log_setSyslogTarget(ME);

	PRINTF("%d devices found in group %d\n\n",pyl_numDevices(pyl),group);

	if (dbName == NULL) { EPRINTF("database name not specified\n"); usage(); }
	if (serverName == NULL) { EPRINTF("influx server name not specified\n"); usage(); }

	iClient = influxdb_post_init (serverName, port, dbName, userName, password, numQueueEntries);

    return 0;
}

uint64_t timestamp;


#define NAMELEN 20
#define SETNAME(c,d) snprintf(name,NAMELEN,c,d)
int appendAnalogData(int adr, PYL_AnalogDataT * ad) {
	char name[NAMELEN+1];
	SETNAME("%d",adr);
	int i,designVoltage;
	float remaining_kWh;

	if (ad->voltage <= 15) designVoltage = 12; /* FIXME */
	else if (ad->voltage <= 30) designVoltage = 24;
	else designVoltage = 48;

	int capacityNotUsableAH = ad->capacity - PLY_CALC_USABLE(ad->capacity);		// AH not usable

	remaining_kWh = (float)(ad->capacity-capacityNotUsableAH) * designVoltage / PYL_MODULE_CAPACITY_DIVIDER / 1000;

	influxBufUsed = influxdb_format_line(&influxBuf, &influxBufLen, influxBufUsed,
		INFLUX_MEAS("Battery"),
		INFLUX_TAG("Module", name),
		INFLUX_F_FLT("i", (float)ad->current/PYL_MODULE_CURRENT_DIVIDER, 1),
		INFLUX_F_FLT("u", (float)ad->voltage/PYL_MODULE_VOLTAGE_DIVIDER, 2),
		INFLUX_F_INT("AH", ad->remainingCapacity),
		INFLUX_F_FLT("kWH", remaining_kWh, 2),
		INFLUX_END);
	for (i=0;i<ad->cellsCount;i++) {
		snprintf(name,NAMELEN,"u%d",i);		// cell voltage
		influxBufUsed = influxdb_format_line(&influxBuf, &influxBufLen, influxBufUsed,	INFLUX_F_FLT(name, (float)ad->cellVoltage[i] / PYL_MODULE_VOLTAGE_DIVIDER, 3), INFLUX_END);
	}
	for (i=0;i<ad->tempCount;i++) {
		if (i==0) SETNAME("%s","temp_bms"); else SETNAME("temp_%d",i);
		influxBufUsed = influxdb_format_line(&influxBuf, &influxBufLen, influxBufUsed,	INFLUX_F_INT(name, ad->temp[i]), INFLUX_END);
	}

	influxBufUsed = influxdb_format_line(&influxBuf, &influxBufLen, influxBufUsed,
		INFLUX_F_INT("CycleCount", ad->cycleCount),
		INFLUX_TS(timestamp), INFLUX_END);

	//printf("%s\n\nlen: %d used: %zu\n",influxBuf,influxBufLen,influxBufUsed);

	return 0;
}

void dump (char *p, int len) {
	while (len) {
		printf("%02x ",*p); p++; len--;
	}
	printf("\n");
}

void showAnalogData(PYL_AnalogDataT * pd) {

	int i;

		printf ("%3d cells, Voltage V:",pd->cellsCount);
		for (i=0;i<pd->cellsCount;i++)
			printf("%7.3f",(float)pd->cellVoltage[i]/1000);
		printf("\n%d temperature values: ",pd->tempCount);
		for (i=0;i<pd->tempCount;i++)
			printf(" %d",pd->temp[i]);
		printf("\ncurrent: %7.3f A voltage: %7.3f V"
				,(float)pd->current/PYL_MODULE_CURRENT_DIVIDER,
				(float)pd->voltage/PYL_MODULE_VOLTAGE_DIVIDER);
		printf(" capacity: %d",pd->capacity);
		printf(" remaining: %d",pd->remainingCapacity);
		printf(" cycle: %d\n",pd->cycleCount);
}



// new Value, last value
int analogDataChanged (PYL_AnalogDataT *a1, PYL_AnalogDataT *a2) {
/* // we can not use memcmp, there will be jitter in the 3rd digit of the cell voltage resulting
   // in a large amount of useless data
	int rc = memcmp(a1,a2,sizeof(PYL_AnalogDataT));

	if (rc == 0) printf("Identical\n"); else {
		printf("a1: "); showAnalogData(a1);
		printf("a2: "); showAnalogData(a2);
	}
	return rc;
*/

	int i;

	if (abs(a1->voltage - a2->voltage) > 2) {
		//printf("\nU %d %d\n",a1->voltage,a2->voltage);
		return 1;  // 0,03V diff
	}
	if (abs(a1->current - a2->current) > 2) {
		//printf("\nI %d %d\n",a1->current,a2->current);
		return 1;
	}
	// write if new current is zero and old was not
	if ((a1->current == 0) && (a2->current != 0)) {
		//printf("\ncurrent 0\n");
		return 1;
	}

	for (i=0;i<a1->cellsCount;i++) {
		if (abs (a1->cellVoltage[i] - a2->cellVoltage[i]) > 4) {
			//printf("\ncell u\n");
			return 1;  // 0,005V delta
		}
	}
	for (i=0;i<a1->tempCount;i++) {
		if (abs(a1->temp[i] - a2->temp[i]) > 0) {
			//printf("\ntemp\n");
			return 1;
		}
	}
	return 0;

}

int errs_http;
int errs_pylon;
int http_sendCount;

void mainloop() {
	PYL_AnalogDataT ad[PYL_MAX_DEVICES_IN_GROUP];
	PYL_AnalogDataT adSent[PYL_MAX_DEVICES_IN_GROUP];
	int rc, deviceNo, entriesAdded;
	int numDvices = pyl_numDevices(pyl);
	memset(&ad,0,sizeof(ad)); memset(&adSent,0,sizeof(adSent));

	LOG(0,"mainloop started");

	while(1) {
		timestamp = influxdb_getTimestamp();
		entriesAdded = 0;
		for (deviceNo=0;deviceNo<numDvices;deviceNo++) {
			pyl_setAdr(pyl,deviceNo+1);							// set target device
			rc = pyl_getAnalogData(pyl,&ad[deviceNo]);			// and get analog values
			if (rc == PYL_OK) {
				ad[deviceNo].infoflag = 0;
				ad[deviceNo].commandValue = 0;					// ignore these ones
				if (analogDataChanged (&ad[deviceNo],&adSent[deviceNo])) {
					appendAnalogData(deviceNo+1,&ad[deviceNo]);
					if (influxBuf == NULL) {
						LOG(0,"appendAnalogData failed for device %d, entriesAdded: %d\n",deviceNo,entriesAdded);
						break;

					} else entriesAdded++;
				}
			} else {
				LOG(0,"getAnalogData for Module %d returned %d\n",deviceNo+1,rc);
				errs_pylon++;
				memset(&ad[deviceNo],0,sizeof(PYL_AnalogDataT));  // write one empty record
				if (analogDataChanged (&ad[deviceNo],&adSent[deviceNo])) {
					appendAnalogData(deviceNo+1,&ad[deviceNo]);
					if (influxBuf == NULL) {
						LOG(0,"appendAnalogData failed for device %d, entriesAdded: %d\n",deviceNo,entriesAdded);
						break;

					} else entriesAdded++;
				}
			}
		}
		if (entriesAdded) {
			//printf("would send:\n%s\n",influxBuf);
			//free(influxBuf); influxBuf = NULL;  // influx post would free
			rc = influxdb_post_http_line(iClient, influxBuf);
			influxBuf = NULL;
			if (rc != 0) {
				LOG(0,"influxdb_post_http_line returned %d\n",rc);
				errs_http++;
			} else {
				memmove(&adSent,&ad,sizeof(ad));
				http_sendCount++;
			}
		}
		sleep(queryIntervalSeconds);
	}
}

void sighup_handler(int signum) {
	LOG(0,"Influxdb packets send: %d, http send errors: %d, pylontech query errors: %d",http_sendCount,errs_http,errs_pylon);
}

void exit_handler(void) {
    LOG(2,"exit_handler called\n");

	if(pyl)	pyl_freeHandle(pyl);
	sighup_handler(0);
	LOG(0,"terminated");
}


void sigterm_handler(int signum) {
	LOG(2,"sigterm_handler called\n");
	exit_handler();
}





int main (int argc, char **argv) {
	if (parseArgs(argc,argv) != 0) exit(1);

	atexit(exit_handler);
	signal(SIGTERM, sigterm_handler);
	signal(SIGHUP, sighup_handler);

	mainloop();
}
