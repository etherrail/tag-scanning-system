#ifndef SCAN_HEADER
#define SCAN_HEADER

#include <stdbool.h>

#define MAX_SCAN_LENGTH 10

extern char scanBuffer[MAX_SCAN_LENGTH];
extern int scanIndex;

extern char scan[MAX_SCAN_LENGTH];
extern bool scanReady;

void scannerBegin();

#endif
