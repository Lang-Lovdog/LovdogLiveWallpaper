#ifndef LLW_SENSORS_H
#define LLW_SENSORS_H
#include <vector>
#include <string>

typedef std::vector<float> float_v;

// Estructura de retorno para el motor principal
struct SysStats {
    float   cpu_temp    = 0.0f;
    float   cpu_usage   = 0.0f;
    float   mem_temp    = 0.0f; // jc42
    float   ssd_temp    = 0.0f; // nvme
    int     battery     =-1  ;
    float   fps         = 0.0f;
    float_v cpu_history       ;
    float_v load_history      ;
};

// La función "get" principal
SysStats get_system_stats();
float get_cpu_usage();
std::string draw_sparkline(const std::vector<float>& history);

#endif
