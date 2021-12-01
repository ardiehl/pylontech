#include "log.h"
#include <syslog.h>
#include <stdarg.h>

int log_verbosity = 0;
int log_syslog = 0;

void log_setVerboseLevel (int verboseLevel) {
    log_verbosity = verboseLevel;
}

void log_incVerboseLevel() {
    log_verbosity++;
}

void log_setSyslogTarget (char * progName) {
#ifndef ESP
    if (log_syslog == 0) {
        openlog(progName, LOG_PERROR, LOG_DAEMON);
        log_syslog++;
    }
#endif
}

void log_fprintf(FILE *stream, char *format, ...)
{
	va_list args;
    va_start(args, format);
    if (log_syslog) syslog(LOG_DAEMON,format,args);
	else fprintf(stream,format,args);
    va_end(args);
}

void log_close() {
    if(log_syslog) {
        log_syslog=0;
        closelog();
    }
}
