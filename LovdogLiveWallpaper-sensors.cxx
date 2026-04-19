#include <iostream>
#include <fstream>
#include <sensors/sensors.h>
#include <string>

// Estructura de retorno para el motor principal
struct SysStats {
    float cpu_temp = 0.0f;
    int   battery  = -1;
};

// Obtener capacidad de batería (BAT1 detectado en tu sistema)
int get_battery_level() {
    std::ifstream file("/sys/class/power_supply/BAT1/capacity");
    int capacity = -1;
    if (!(file >> capacity)) return -1;
    return capacity;
}

// Función principal de recolección
SysStats get_system_stats() {
    SysStats stats;
    stats.battery = get_battery_level();

    if (sensors_init(NULL) != 0) return stats;

    const sensors_chip_name *cn;
    int c = 0;
    
    while ((cn = sensors_get_detected_chips(NULL, &c))) {
        // Filtramos por el prefijo 'coretemp' que vimos en tu salida
        if (std::string(cn->prefix) == "coretemp") {
            const sensors_feature *feat;
            int f = 0;
            while ((feat = sensors_get_features(cn, &f))) {
                // SENSORS_FEATURE_TEMP es el tipo 0x00
                if (feat->type == SENSORS_FEATURE_TEMP) {
                    double val;
                    if (sensors_get_value(cn, feat->number, &val) == 0) {
                        stats.cpu_temp = (float)val;
                        // Usamos el primer valor de temperatura (Package ID 0) y salimos
                        break; 
                    }
                }
            }
        }
    }

    sensors_cleanup();
    return stats;
}

// --- BLOQUE DE PRUEBA ---
// Solo se compila si definimos BUILDING_STANDALONE
#ifdef BUILDING_STANDALONE
int main() {
    SysStats s = get_system_stats();
    
    // Usando los íconos que te gustaron
    std::cout << "\n[ LLW System Monitor ]" << std::endl;
    std::cout << "  CPU Temp: " << s.cpu_temp << "°C" << std::endl;
    std::cout << "  Battery : " << s.battery << "%" << std::endl;
    
    if (s.cpu_temp > 80.0) std::cout << "⚠ ¡Cuidado! Temperatura elevada." << std::endl;
    if (s.battery < 15)   std::cout << "⚠ Batería baja, busca el cargador." << std::endl;
    
    return 0;
}
#endif
