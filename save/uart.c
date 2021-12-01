#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include "uart.h"
#include <sys/select.h>

struct termios oldtio,newtio;


#define FLUSH fsync(serFd)

int serFd;

extern int verbose;


int uart_waiti(int timeout_miliseconds) {
	fd_set set;
    struct timeval timeout;
    int res;

    FD_ZERO (&set);
	FD_SET (serFd, &set);
	timeout.tv_sec = 0;
	timeout.tv_usec = timeout_miliseconds*1000;
	VPRINTF(2,"uart_waiti: Calling select with tv_usec=%ld\n",timeout.tv_usec);
	res = select(serFd+1, &set, NULL, NULL, &timeout);
	VPRINTF(2,"uart_waiti: select returned %d\n",res);
	if (res <= 0) return ERR;
	return OK;
}


void uart_close() {
	if (serFd != 0) {
		close(serFd); serFd = 0;
    }
}

int initSerial (char * uart_port, int baud) {
	int baudFlag;

	if (serFd > 0) {
		tcdrain(serFd);				// make sure all data has been sent
		tcflush(serFd, TCIFLUSH);	// discard all buffered received data
		close(serFd);
	}
	serFd = open(uart_port, O_RDWR | O_NOCTTY );
	if (serFd <0) { return ERR; }

	tcgetattr(serFd,&oldtio); /* save current port settings */

	bzero(&newtio, sizeof(newtio));
	newtio.c_cflag = CS8 | CLOCAL | CREAD;  // CRTSCTS
	baudFlag = B115200;
	switch (baud) {
    	    case 1200: baudFlag = B1200; break;
    	    case 1800: baudFlag = B1800; break;
    	    case 2400: baudFlag = B2400; break;
    	    case 4800: baudFlag = B4800; break;
    	    case 9600: baudFlag = B9600; break;
    	    case 19200: baudFlag = B19200; break;
    	    case 38400: baudFlag = B38400; break;
    	    case 57600: baudFlag = B57600; break;
	}
	newtio.c_cflag = baudFlag | CS8 | CLOCAL | CREAD; // CRTSCTS
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;

	/* set input mode (non-canonical, no echo,...) */
	newtio.c_lflag = 0;

	newtio.c_cc[VTIME]    = 2;   /* inter-character timer 1/10 second */
	newtio.c_cc[VMIN]     = 0;

	tcflush(serFd, TCIOFLUSH);	/* discard all buffered data */
	tcsetattr(serFd,TCSANOW,&newtio);
	return OK;
}


// set the number of expected chars and the timeout between chars (in 1/10 second)
void uart_setCharsAndTimeout (int expectedChars, int timeout) {
	if (serFd) {
		newtio.c_cc[VTIME]    = timeout;		/* inter-character timer 1/10 second */
		newtio.c_cc[VMIN]     = expectedChars;	/* min size of return packet */
		tcsetattr(serFd,TCSANOW,&newtio);
	}
}


void dumpBuffer (char *buf, int size) {
	int i;
	char *p = buf;

	for (i=0;i<size;i++) {
		if ((*p<' ') || (*p > 0x7f)) {
			printf("<0x%02x>",*p);
		} else putchar(*p);
		p++;
	}
	//printf("\n");
}



int uart_write_bytes(char* src, size_t size) {
	int res;
	if (verbose > 1) {
		VPRINTF(1,"uart_write_bytes: Request to write %lu bytes ",size);
		if (verbose > 1) dumpBuffer(src,size);
		printf("\n");
	}
	res = write (serFd, src, size);
	VPRINTF(1,"uart_write_bytes: res: %d\n",res);
	tcdrain(serFd);		// make sure all data has been sent
	return res;
}

int uart_read_bytes(char* buf, int maxLen, int timeoutms) {
	int bytesReceived = 0;
	int bytesRemaining = maxLen;
	int res;

         //  printf("receive maxLen=%d\n",maxLen);
	while (bytesReceived<maxLen) {       /* loop for input */
		if (timeoutms > 0) {
			res = uart_waiti(timeoutms);
			if (res != OK) {
				VPRINTF(1,"uart_read_bytes: timeout waiting for data\n");
				return -1;
			}
		}
		res = read(serFd,buf,bytesRemaining);
		if (res <= 0) return 0;
		buf+=res; bytesRemaining-=res; bytesReceived+=res;
		VPRINTF(1,"uart_read_bytes: received %d bytes\n",res);

	}
	return maxLen;
}


int uart_read(char* buf, int maxLen) {
	VPRINTF(2,"uart_read: %d bytes\n",maxLen);
	int res;
	res = read(serFd,buf,maxLen);
	VPRINTF(1,"uart_read: received %d bytes (%s)\n",res,buf);
	return res;
}



