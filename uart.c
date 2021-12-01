#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include "uart.h"
#include <sys/select.h>
#include "log.h"

struct termios oldtio,newtio;

//#define TCP

#ifdef TCP
// for TCP use
// socat -d -d -d TCP-LISTEN:4242,reuseaddr,fork FILE:/dev/ttyUSB1,b115200,rawer,cs8,cread=1,clocal=1

#define HOSTNAME "vgx.armin.d"
//#define HOSTNAME "localhost"
#define PORT "4242"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

#endif // TCP


extern int verbose;



int uart_waiti(int serFd, int timeout_miliseconds) {
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
	if (res <= 0) return res;
	return UART_OK;
}

void uart_flush(int serFd) {
#ifdef TCP
#if 0
	int res;
	char c;
	do {
		res = uart_waiti(100);
		if (res == UART_OK) read(serFd,&c,1);
	} while (res == UART_OK);
#endif
#else
	tcflush(serFd, TCIOFLUSH);	/* discard all buffered data */
#endif // TCP
}


#ifdef TCP

int socketConnect (const char *addrOrName, const char *port) {
        int sockfd,err;
        struct addrinfo hints;
    struct addrinfo *res;

    memset(&hints, 0, sizeof(hints));
    //hints.ai_family=AF_UNSPEC;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

        err = getaddrinfo(addrOrName, port, &hints, &res);

    if (err != 0 || res == NULL) {
        printf("socketConnect: getaddrinfo for '%s'failed err=%d res=%p", addrOrName, err, res);
        return -1;
    }

    /* Code to print the resolved IP.
       Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    //addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    //LOG("DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

    sockfd = socket(res->ai_family, res->ai_socktype, 0);
    if (sockfd < 0) {
       printf("socketConnect: Failed to allocate socket.");
       freeaddrinfo(res);
       return -1;
    }

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) != 0) {
       printf("socketConnect: socket connect failed errno=%d", errno);
       close(sockfd);
       freeaddrinfo(res);
       return -1;
    }

    freeaddrinfo(res);
    return sockfd;
}

#endif // TCP


void uart_close(int serFd) {
	if (serFd != 0) {
		close(serFd);
    }
}

int initSerial (char * uart_port, int baud) {
	int serFd;
#ifdef TCP
	serFd = socketConnect (HOSTNAME,PORT);
	return serFd;
#else
	int baudFlag;

	VPRINTF(2,"Opening %s | %d baud ",uart_port,baud); fflush(stdout);
	serFd = open(uart_port, O_RDWR | O_NOCTTY );
	VPRINTF(2, "Res:%d\n",serFd);
	if (serFd <0) { return serFd; }

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

	uart_flush(serFd);	/* discard all buffered data */
	tcsetattr(serFd,TCSANOW,&newtio);
	return serFd;
#endif
}


// set the number of expected chars and the timeout between chars (in 1/10 second)
void uart_setCharsAndTimeout (int serFd, int expectedChars, int timeout) {
#ifndef TCP
	if (serFd) {
		newtio.c_cc[VTIME]    = timeout;		/* inter-character timer 1/10 second */
		newtio.c_cc[VMIN]     = expectedChars;	/* min size of return packet */
		tcsetattr(serFd,TCSANOW,&newtio);
	}
#endif
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



int uart_write_bytes(int serFd, char* src, size_t size) {
	int res;
	if (verbose > 1) {
		VPRINTF(1,"uart_write_bytes: Request to write %d bytes ",(int)size);
		if (verbose > 1) dumpBuffer(src,size);
		printf("\n");
	}
	res = write (serFd, src, size);
	VPRINTF(1,"uart_write_bytes: res: %d\n",res);
#ifndef TCP
	//tcdrain(serFd);		// make sure all data has been sent
#endif // TCP
	return res;
}

int uart_read_bytes(int serFd, char* buf, int maxLen, int timeoutms) {
	int bytesReceived = 0;
	int bytesRemaining = maxLen;
	int res;

         //  printf("receive maxLen=%d\n",maxLen);
	while (bytesReceived<maxLen) {       /* loop for input */
		if (timeoutms > 0) {
			res = uart_waiti(serFd,timeoutms);
			if (res != UART_OK) {
				VPRINTF(1,"uart_read_bytes: timeout waiting for data, received %d, requested %d\n",bytesReceived,maxLen);
				return -1;
			}
		}
		res = read(serFd,buf,bytesRemaining);
		if (res <= 0) return 0;
		buf+=res; bytesRemaining-=res; bytesReceived+=res;
		VPRINTF(1,"uart_read_bytes: received %d bytes, remaining %d\n",res,bytesRemaining);

	}
	return maxLen;
}


int uart_read(int serFd, char* buf, int maxLen) {
	VPRINTF(2,"uart_read: %d bytes\n",maxLen);
	int res;
	res = read(serFd,buf,maxLen);
	VPRINTF(1,"uart_read: received %d bytes (%s)\n",res,buf);
	return res;
}



