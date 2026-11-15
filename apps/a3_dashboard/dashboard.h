#ifndef GTOS_A3_DASHBOARD_H
#define GTOS_A3_DASHBOARD_H

#include <stdint.h>

void gtos_dashboard_start(uint16_t* pixels);
void gtos_dashboard_tick(uint32_t milliseconds, int command);

#endif
