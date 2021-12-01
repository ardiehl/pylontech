#ifndef LOG_H_INCLUDED
#define LOG_H_INCLUDED

#if defined(ESP_PLATFORM) || defined(ESP_EMU)
#define ESP
#endif

#ifndef ESP
#include <syslog.h>
#endif

#include <stdio.h>

extern int log_verbosity;
extern int log_syslog;

void log_setVerboseLevel (int verboseLevel);
void log_incVerboseLevel();
void log_setSyslogTarget (char * progName);
void log_close();
void log_fprintf(FILE *stream, char *format, ...);

#define VPRINTF(LEVEL , ...) if (log_verbosity >= LEVEL) log_fprintf(stdout, __VA_ARGS__)
#define PRINTF(...) if (log_verbosity >= 0) log_fprintf(stdout, __VA_ARGS__)
#define EPRINTF(...) if (log_verbosity >= 0) log_fprintf(stderr, __VA_ARGS__)

#endif // LOG_H_INCLUDED
