#ifndef UART_H_INCLUDED
#define UART_H_INCLUDED

#include <termios.h>
#//define VPRINTF(vlevel,V...) if (verbose>=vlevel) printf(V)
#define UART_OK 0
#define UART_ERR -1

//#define USE_SELECT


int uart_waiti(int serFd, int timeout_miliseconds);


void dumpBuffer (char *buf, int size);

void uart_close(int serFd, struct termios * ti_save);

int initSerial (char * uart_port, int baud,
				struct termios * ti_save,		// current will be saved here if != NULL
				struct termios * ti);			// new will be saved here if != NULL

// set the number of expected chars and the timeout between chars (in 1/10 second), does not work for me
void uart_setCharsAndTimeout (int serFd, struct termios * ti, int expectedChars, int timeout);

int uart_write_bytes(int serFd, char* src, size_t size);

int uart_read_bytes(int serFd,
#ifndef USE_SELECT
					struct termios * ti,
#endif
					char* buf, int maxLen, int timeoutms);

int uart_read(int serFd, char* buf, int maxLen);

void uart_flush(int serFd);	/* discard all buffered data */

#endif // UART_H_INCLUDED
