#ifndef LLW_SENSORS_H
#define LLW_SENSORS_H

#include <string>

struct SysStats {
    float cpu_temp = 0.0f;
    float mem_temp = 0.0f; // jc42
    float ssd_temp = 0.0f; // nvme
    int   battery  = -1;
    float fps      = 0.0f;
};

// La función "get" principal
SysStats get_system_stats();

#endif
