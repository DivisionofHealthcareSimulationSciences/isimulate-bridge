#ifndef SERVICE_DISCOVERY_H
#define SERVICE_DISCOVERY_H

// avahi service discovery browser
// see https://www.avahi.org/doxygen/html/client-browse-services_8c-example.html

#include <stdint.h>
#include <stdbool.h>

extern char monitor_address[20];
extern uint16_t monitor_port;
extern bool monitor_service_new;

int service_discovery();

#endif