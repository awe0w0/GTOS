#ifndef GTOS_A3_DASHBOARD_H
#define GTOS_A3_DASHBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { GtosDashboardStatusBytes = 512 };

void gtos_dashboard_start(uint16_t* pixels);
void gtos_dashboard_tick(uint32_t milliseconds, int command);
int gtos_dashboard_probe(char* buffer, size_t size);
void gtos_dashboard_publish_status(const char* json, bool online);
void gtos_dashboard_stop_status(void);

#endif
