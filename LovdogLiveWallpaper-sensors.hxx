#ifndef LLW_SENSORS_H
#define LLW_SENSORS_H
#include <vector>
#include <string>
#include <utility>

typedef std::vector<float> float_v;
typedef std::vector<std::pair<std::string, float>> floatstr_v;

// Estructura de retorno para el motor principal
struct SysStats {
    float       cpu_temp              = 0.0f        ;
    float       cpu_usage             = 0.0f        ;
    float       ram_total             = 0.0f        ;
    float       ram_usage             = 0.0f        ;
    float       mem_temp              = 0.0f        ; // jc42
    float       ssd_temp              = 0.0f        ; // nvme
    floatstr_v  storage_info                        ;
    size_t      storage_max_label_len = 0           ;
    int         battery               =-1           ;
    std::string battery_label         ="  Battery" ;
    float       fps                   = 0.0f        ;
    float_v     cpu_temp_history                    ;
    float_v     cpu_load_history                    ;
    float_v     ram_history                         ;
};

// La función "get" principal
SysStats get_system_stats();
float get_cpu_usage();
std::string draw_sparkline(const std::vector<float>& history);

#endif
