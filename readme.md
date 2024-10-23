# Pylontech
## API to query Pylontech Batteries as well as writing Battery data to InfluxDB (V1/V2) or other influxdb2 line protocol compatible software (e.g. telegraf or questdb)

This code provides:
- a library to read data from Pylontech Batteries (tested with UC3000)
- a CLI to query the batterie(s)
- a program to write monitoring data to InfluxDB (pylon2influx)

## Pylontech CLI

```
Usage: pylontech [OPTION]...
  -h, --help         display help and exit
  -d, --device       specify device, default: /dev/ttyUSB1
  -b, --baud         specify serial baudrate, default: 115200
  -a, --adr          Pylontech device address (1-12)
  -g, --group        Pylontech group address (0-15)
  -s, --scan         Scan for devices at address 1 to 255
  -y, --systemparam  Show system parameter
  -l, --alarm        Show alarm information
  -S, --serial       Show system serial number
  -P, --packdata     Show analog data
  -c, --charge       Show charge / discharge info
  -m, --manufact     Show manufacturer information
  -v, --verbose[=x]  increase verbose level
```

## pylon2influx
```
Usage: pylon2influx [OPTION]...
  -h, --help            display help and exit
  -d, --device          specify device (/dev/ttyUSB_pylontech)
  -b, --baud            specify serial baudrate (115200)
  -g, --group           Pylontech group address (0-15)
  -s, --server          influxdb server name or ip
  -p, --port            influxdb port (8086)
  -n, --db              database name
  -u, --user            influxdb user name
  -u, --password        influxdb password
  -B, --bucket          influxdb v2 bucket
  -O, --org             influxdb v2 org
  -T, --token           influxdb v2 auth api token
  -A, --influxapi       api string for influx, replaces db..token
  -I, --isslverifypeer  0: do not check ssl certificate, 1=check
  -c, --cache           #entries for influxdb cache (240)
  -v, --verbose[=x]     increase verbose level
  -y, --syslog          log to syslog insead of stderr
  -Y, --syslogtest      send a testtext to syslog and exit
  -e, --version         show version
  -q, --query           query interval in seconds (5)
  -t, --try             try to connect returns 0 on success

The cache will be used in case the influxdb server is down. In
that case data will be send when the server is reachable again.
```

In case you are not using influxdb, the api path can be specified via --influxapi=, e.g.
```
--influxapi=/api/write?token=mytoken&request_timeout=100
```
ssl will be used if https:// is the prefix of the specified hostname.

## Pylontech API
```
/ allocate and initialize api handle
PYL_HandleT* pyl_initHandle();

// free api handle and close serial port if open
void pyl_freeHandle(PYL_HandleT* pyl);

// sets the group and scans for devices in group, returns number of devices found
int pyl_setGroup (PYL_HandleT* pyl, int groupNum);

// closes the serial port, e.g. in case of errors. It will be reopened automatically on request
void pyl_closeSerialPort(PYL_HandleT* pyl);
int pyl_openSerialPort(PYL_HandleT* pyl);

// open the serial port and query the number of devices, first device is at group num +2, first group is 0
// returns -1 on failure opening the serial port or the number of devices found
int pyl_connect(PYL_HandleT* pyl, int groupNum, char *portname);

// set device address in group, first device is 1
void pyl_setAdr(PYL_HandleT* pyl, int Adr);

int pyl_numDevices(PYL_HandleT* pyl);

// get data from selected in device in selected group
int pyl_getProtocolVersion (PYL_HandleT* pyl);
int pyl_getAnalogData (PYL_HandleT* pyl, PYL_AnalogDataT *pd);
int pyl_getSystemParameter (PYL_HandleT* pyl, PYL_SystemParameterT *sp);
int pyl_getManufacturerInformation (PYL_HandleT* pyl, PYL_ManufacturerInformationT *mi);
int pyl_getSerialNumber (PYL_HandleT* pyl, PYL_SerialNumberT *mi);
int pyl_getAlarmInfo (PYL_HandleT* pyl, PYL_AlarmInfoT *ai);
int pyl_getChargeDischargeInfo (PYL_HandleT* pyl, PYL_ChargeDischargeInfoT *cd);
```

and the structs:
```
typedef struct {
	struct termios ti_save;		// this termios will be restored on exit
	struct termios ti;			// this is our new termios, store it here to check termios will be restored on exit
	char * portname;			// need to be free'ed
	int adr;					// current device address, first device is 1 here
	int group;					// max 12 devices in one group, upper 4 bit of adr are group id, lower 4 are adr+1
	int serFd;					// fileno for serial port i/o
	int protocolVersion;
	int numDevicesFound;
	int initialized;			// 1 after initialization (modules scanned)
} PYL_HandleT;
cd);
```

```
typedef struct {
	char deviceName[11];
	int softwareVersion[2];
	char manufacturerName[21];
} PYL_ManufacturerInformationT;
```
```
typedef struct {
	int cellHighVoltageLimit;		// all fields 2 bytes
	int cellLowVoltageLimit;
	int cellUnderVoltageLimit;
	int chargeHighTemperatureLimit;
	int chargeLowTemperatureLimit;
	int chargeCurrentLimit;
	int moduleHighVoltageLimit;
	int moduleLowVoltageLimit;
	int moduleUnderVoltageLimit;
	int dischargeHighTemperatureLimit;
	int dischargeLowTemperatureLimit;
	int dischargeCurrentLimit;
} PYL_SystemParameterT;
```
```
typedef struct {
	char sn [18];
} PYL_SerialNumberT;
```
```
typedef struct {
	int infoflag;
	int commandValue;
	int cellsCount;
	int cellVoltage[CELLS_MAX];
	int tempCount;
	int temp[TEMPS_MAX];		// already converted to degree celsius
	int current;
	int voltage;
	int remainingCapacity;
	int userDefinedItemCount;
	int capacity;
	int cycleCount;
} PYL_AnalogDataT;
```
```
typedef struct {
	int dataFlag;
	int commandValue;
	int cellsCount;
	int cellVoltageStatus[CELLS_MAX];	// 0=ok, 1=below low limit, 2=above high limit. F0=other Error
	int tempCount;
	int tempStatus[TEMPS_MAX];
	int chargeCurrentStat;
	int moduleVoltageStat;
	int dischargeCurrentStat;
	int status[5];
} PYL_AlarmInfoT;
```
```
typedef struct {
	int commandValue;
	int chargeVoltageLimit;
	int dischargeVoltageLimit;
	int chargeCurrentLimit;
	int dischargeCurrentLimit;
	int chargeDischargeStatus;
} PYL_ChargeDischargeInfoT;
```
