#include <termios.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// mostly taken from stty.c

/* Which member(s) of `struct termios' a mode uses.  */
enum mode_type
  {
    control, input, output, local, combination, none
  };

/* Flags for `struct mode_info'. */
#define SANE_SET 1		/* Set in `sane' mode. */
#define SANE_UNSET 2		/* Unset in `sane' mode. */
#define REV 4			/* Can be turned off by prepending `-'. */
#define OMIT 8			/* Don't display value. */

/* Each mode.  */
struct mode_info
  {
    const char *name;		/* Name given on command line.  */
    enum mode_type type;	/* Which structure element to change. */
    char flags;				/* Setting and display options.  */
    unsigned long bits;		/* Bits to set for this mode.  */
    unsigned long mask;		/* Other bits to turn off for this mode.  */
  };


static struct mode_info const mode_info[] =
{
  {"parenb", control, REV, PARENB, 0},
  {"parodd", control, REV, PARODD, 0},
  {"cs5", control, 0, CS5, CSIZE},
  {"cs6", control, 0, CS6, CSIZE},
  {"cs7", control, 0, CS7, CSIZE},
  {"cs8", control, 0, CS8, CSIZE},
  {"hupcl", control, REV, HUPCL, 0},
  {"hup", control, REV | OMIT, HUPCL, 0},
  {"cstopb", control, REV, CSTOPB, 0},
  {"cread", control, SANE_SET | REV, CREAD, 0},
  {"clocal", control, REV, CLOCAL, 0},
#ifdef CRTSCTS
  {"crtscts", control, REV, CRTSCTS, 0},
#endif

  {"ignbrk", input, SANE_UNSET | REV, IGNBRK, 0},
  {"brkint", input, SANE_SET | REV, BRKINT, 0},
  {"ignpar", input, REV, IGNPAR, 0},
  {"parmrk", input, REV, PARMRK, 0},
  {"inpck", input, REV, INPCK, 0},
  {"istrip", input, REV, ISTRIP, 0},
  {"inlcr", input, SANE_UNSET | REV, INLCR, 0},
  {"igncr", input, SANE_UNSET | REV, IGNCR, 0},
  {"icrnl", input, SANE_SET | REV, ICRNL, 0},
  {"ixon", input, REV, IXON, 0},
  {"ixoff", input, SANE_UNSET | REV, IXOFF, 0},
  {"tandem", input, REV | OMIT, IXOFF, 0},
#ifdef IUCLC
  {"iuclc", input, SANE_UNSET | REV, IUCLC, 0},
#endif
#ifdef IXANY
  {"ixany", input, SANE_UNSET | REV, IXANY, 0},
#endif
#ifdef IMAXBEL
  {"imaxbel", input, SANE_SET | REV, IMAXBEL, 0},
#endif
#ifdef IUTF8
  {"iutf8", input, SANE_UNSET | REV, IUTF8, 0},
#endif

  {"opost", output, SANE_SET | REV, OPOST, 0},
#ifdef OLCUC
  {"olcuc", output, SANE_UNSET | REV, OLCUC, 0},
#endif
#ifdef OCRNL
  {"ocrnl", output, SANE_UNSET | REV, OCRNL, 0},
#endif
#ifdef ONLCR
  {"onlcr", output, SANE_SET | REV, ONLCR, 0},
#endif
#ifdef ONOCR
  {"onocr", output, SANE_UNSET | REV, ONOCR, 0},
#endif
#ifdef ONLRET
  {"onlret", output, SANE_UNSET | REV, ONLRET, 0},
#endif
#ifdef OFILL
  {"ofill", output, SANE_UNSET | REV, OFILL, 0},
#endif
#ifdef OFDEL
  {"ofdel", output, SANE_UNSET | REV, OFDEL, 0},
#endif
#ifdef NLDLY
  {"nl1", output, SANE_UNSET, NL1, NLDLY},
  {"nl0", output, SANE_SET, NL0, NLDLY},
#endif
#ifdef CRDLY
  {"cr3", output, SANE_UNSET, CR3, CRDLY},
  {"cr2", output, SANE_UNSET, CR2, CRDLY},
  {"cr1", output, SANE_UNSET, CR1, CRDLY},
  {"cr0", output, SANE_SET, CR0, CRDLY},
#endif
#ifdef TABDLY
# ifdef TAB3
  {"tab3", output, SANE_UNSET, TAB3, TABDLY},
# endif
# ifdef TAB2
  {"tab2", output, SANE_UNSET, TAB2, TABDLY},
# endif
# ifdef TAB1
  {"tab1", output, SANE_UNSET, TAB1, TABDLY},
# endif
# ifdef TAB0
  {"tab0", output, SANE_SET, TAB0, TABDLY},
# endif
#else
# ifdef OXTABS
  {"tab3", output, SANE_UNSET, OXTABS, 0},
# endif
#endif
#ifdef BSDLY
  {"bs1", output, SANE_UNSET, BS1, BSDLY},
  {"bs0", output, SANE_SET, BS0, BSDLY},
#endif
#ifdef VTDLY
  {"vt1", output, SANE_UNSET, VT1, VTDLY},
  {"vt0", output, SANE_SET, VT0, VTDLY},
#endif
#ifdef FFDLY
  {"ff1", output, SANE_UNSET, FF1, FFDLY},
  {"ff0", output, SANE_SET, FF0, FFDLY},
#endif
  {NULL, none, 0, 0, 0}
};

struct speed_map
{
	const char *string;			/* ASCII representation. */
	speed_t speed;				/* Internal form. */
	int value;					/* Numeric value. */
};

static struct speed_map const speeds[] =
{
  {"0", B0, 0},
  {"50", B50, 50},
  {"75", B75, 75},
  {"110", B110, 110},
  {"134", B134, 134},
  {"134.5", B134, 134},
  {"150", B150, 150},
  {"200", B200, 200},
  {"300", B300, 300},
  {"600", B600, 600},
  {"1200", B1200, 1200},
  {"1800", B1800, 1800},
  {"2400", B2400, 2400},
  {"4800", B4800, 4800},
  {"9600", B9600, 9600},
  {"19200", B19200, 19200},
  {"38400", B38400, 38400},
  {"exta", B19200, 19200},
  {"extb", B38400, 38400},
#ifdef B57600
  {"57600", B57600, 57600},
#endif
#ifdef B115200
  {"115200", B115200, 115200},
#endif
#ifdef B230400
  {"230400", B230400, 230400},
#endif
#ifdef B460800
  {"460800", B460800, 460800},
#endif
#ifdef B500000
  {"500000", B500000, 500000},
#endif
#ifdef B576000
  {"576000", B576000, 576000},
#endif
#ifdef B921600
  {"921600", B921600, 921600},
#endif
#ifdef B1000000
  {"1000000", B1000000, 1000000},
#endif
#ifdef B1152000
  {"1152000", B1152000, 1152000},
#endif
#ifdef B1500000
  {"1500000", B1500000, 1500000},
#endif
#ifdef B2000000
  {"2000000", B2000000, 2000000},
#endif
#ifdef B2500000
  {"2500000", B2500000, 2500000},
#endif
#ifdef B3000000
  {"3000000", B3000000, 3000000},
#endif
#ifdef B3500000
  {"3500000", B3500000, 3500000},
#endif
#ifdef B4000000
  {"4000000", B4000000, 4000000},
#endif
  {NULL, 0, 0}
};

int baud_to_value (speed_t speed) {
  int i;

  for (i = 0; speeds[i].string != NULL; ++i)
    if (speed == speeds[i].speed)
      return speeds[i].value;
  return 0;
}


static tcflag_t *mode_type_flag (enum mode_type type, struct termios *mode)
{
  switch (type)
    {
    case control:
      return &mode->c_cflag;

    case input:
      return &mode->c_iflag;

    case output:
      return &mode->c_oflag;

    case local:
      return &mode->c_lflag;

    case combination:
      return NULL;

    default:
      abort ();
    }
}




int decode_termios (struct termios *tp, char * dest, int destSize) {
	char *p = dest;
	int remaining = destSize;
	int len,i;
	int iBaud,oBaud;
	enum mode_type prev_type = control;
	tcflag_t *bitsp;
	unsigned long mask;

	iBaud = baud_to_value(cfgetispeed(tp));
	oBaud = baud_to_value(cfgetospeed(tp));

	if (iBaud==oBaud) {
		len = snprintf(dest,remaining,"baud: %d",oBaud);
	} else {
		len = snprintf(dest,remaining,"obaud: %d ibaud: %d",oBaud,iBaud);
	}
	p+=len; remaining-=len;

	for (i = 0; mode_info[i].name != NULL; ++i) {
		if (mode_info[i].flags & OMIT) continue;
		if (mode_info[i].type != prev_type) {
			len = snprintf(p,remaining," |");
			p+=len; remaining-=len;
			prev_type = mode_info[i].type;
        }

		bitsp = mode_type_flag (mode_info[i].type, tp);
		mask = mode_info[i].mask ? mode_info[i].mask : mode_info[i].bits;
		len = 0;
		if ((*bitsp & mask) == mode_info[i].bits)
			len = snprintf(p,remaining," %s", mode_info[i].name);
		else if (mode_info[i].flags & REV)
			len = snprintf(p,remaining," -%s", mode_info[i].name);
		p+=len; remaining-=len;
    }
    return 0;
}
