#ifndef PYLONTECH_H_INCLUDED
#define PYLONTECH_H_INCLUDED

#define PYL_DEFPORTNAME "/dev/ttyUSB1"
#define PYL_DEFBAUDRATE 115200

#define LI_BAT_DATA 0x46

#define CELLS_MAX 16
#define TEMPS_MAX 16
// modules per group
//  manual: 16 (how can this be possible if the first module adr is 2 and we have only 4 bit ?
//  RS485-protocol-pylon-low-voltage-V3.3-20180821:
//     Maximum 8/12 (please refer to product specification) batteries in one group
//  lets use the lower 4 bit minus 2 as the first device is always @ adr 2
#define DEVICES_MAX 14


#define PYL_CELL_VOLTAGE_DIVIDER 1000
#define PYL_MODULE_VOLTAGE_DIVIDER 1000
#define PYL_MODULE_CURRENT_DIVIDER 10
#define PYL_MODULE_CAPACITY_DIVIDER 1000

// from the datasheet of my US3000C
// Nominal capacity (Wh): 3552
// Usable capacity (Wh) : 3374,4
// That results in 95% usable
#define PYL_USABLE_PERCENT 95
#define PLY_CALC_USABLE(capacity) (capacity*PYL_USABLE_PERCENT/100)

#define PYL_OK 0
#define PYL_ERR -1


typedef struct {
	int adr;					// current device address, first device is 1 here
	int group;					// max 12 devices in one group, upper 4 bit of adr are group id, lower 4 are adr+1
	int serFd;					// fileno for serial port i/o
	int protocolVersion;
	int numDevicesFound;
} PYL_HandleT;

typedef struct {
	char deviceName[11];
	int softwareVersion[2];
	char manufacturerName[21];
} PYL_ManufacturerInformationT;

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

typedef struct {
	char sn [18];
} PYL_SerialNumberT;

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

typedef struct {
	int commandValue;
	int chargeVoltageLimit;
	int dischargeVoltageLimit;
	int chargeCurrentLimit;
	int dischargeCurrentLimit;
	int chargeDischargeStatus;
} PYL_ChargeDischargeInfoT;

// allocate and initialize api handle
PYL_HandleT* pyl_initHandle();

// free api handle and close serial port if open
void pyl_freeHandle(PYL_HandleT* pyl);

// sets the group and scans for devices in group, returns number of devices found
int pyl_setGroup (PYL_HandleT* pyl, int groupNum);

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


#endif // PYLONTECH_H_INCLUDED

