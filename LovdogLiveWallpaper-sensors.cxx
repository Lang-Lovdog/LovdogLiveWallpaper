#include <iostream>
#include <fstream>
#include <sensors/sensors.h>
#include <string>
// #include <algorithm> // Necesario para std::max_element si escalas dinámicamente
#include "LovdogLiveWallpaper-sensors.hxx"
#define GRAPH_HEIGHT 6
#define BRAILLE_EMPTY 0x00

const size_t MAX_HISTORY = 40;
static float_v global_cpu_history;
static float_v global_temp_history(MAX_HISTORY, 0.0f);
static float_v global_load_history(MAX_HISTORY, 0.0f);

#ifdef BUILDING_STANDALONE
    size_t utf8_length(const std::string& str) {
    size_t len = 0;
    for (size_t i = 0; i < str.length(); i++) {
        // En UTF-8, los bytes de continuación empiezan con los bits 10
        if ((str[i] & 0xC0) != 0x80) len++;
    }
    return len;
}
#endif

std::string draw_sparkline(const std::vector<float>& history) {
    if (history.empty()) return "";
    
    std::string chars[] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    std::string graph = "";
    
    for (float val : history) {
        // Escala basada en 100°C como máximo. 
        // Si prefieres escala dinámica, podrías usar el valor máximo del historial.
        int level = static_cast<int>((val / 100.0) * 7); 
        if (level > 7) level = 7;
        if (level < 0) level = 0;
        graph += chars[level];
    }
    return graph;
}

std::string get_braille(unsigned char pattern) {
    std::string s;
    char32_t code = 0x2800 + pattern;
    s += (char)(0xE0 | (code >> 12));
    s += (char)(0x80 | ((code >> 6) & 0x3F));
    s += (char)(0x80 | (code & 0x3F));
    return s;
}

std::string draw_braille_box(const std::vector<float>& history, std::string label, float max_val) {
    std::string rows[GRAPH_HEIGHT];
    float unit = max_val / (float)GRAPH_HEIGHT;

    for (size_t i = 0; i < MAX_HISTORY - 1; i += 2) {
        float v1 = history[i];
        float v2 = history[i + 1];

        for (int r = 0; r < GRAPH_HEIGHT; ++r) {
            unsigned char byte = BRAILLE_EMPTY;
            
            // Calculamos los límites de esta fila específica
            // r=0 es la fila superior, r=3 es la inferior
            float row_bottom = (float)(GRAPH_HEIGHT - 1 - r) * unit;
            
            // Sub-división interna de la fila (Braille tiene 4 puntos verticales, 
            // pero estamos usando 3 para los caracteres estándar de 6 puntos 0x3F)
            float sub_unit = unit / 3.0f;

            // Lógica para punto izquierdo
            if (v1 > row_bottom) {
                float rel = v1 - row_bottom;
                if (rel >= unit) byte |= 0x07; // Fila llena
                else if (rel >= sub_unit * 2) byte |= 0x03;
                else byte |= 0x01;
            }
            
            // Lógica para punto derecho
            if (v2 > row_bottom) {
                float rel = v2 - row_bottom;
                if (rel >= unit) byte |= 0x38; // Fila llena
                else if (rel >= sub_unit * 2) byte |= 0x18;
                else byte |= 0x08;
            }

            rows[r] += get_braille(byte);
        }
    }

    // Ensamblaje con bordes (usando tu corrección del loop para UTF-8)
    std::string box = "┌─ " + label + " ";
    while (box.length() < (MAX_HISTORY / 2) - 2) box += "─"; // Ajuste visual
    box += "┐\n";

    for (int i = 0; i < GRAPH_HEIGHT; ++i) {
        box += "│" + rows[i] + "│\n";
    }

    box += "└";
    for (size_t i = 0; i < MAX_HISTORY / 2; ++i) box += "─";
    box += "┘";
    
    return box;
}

std::string draw_braille_graph(const std::vector<float>& history) {
    if (history.empty()) return "";

    // Necesitamos un número par de muestras para el ancho del Braille (2 puntos por char)
    size_t width = history.size();
    std::string rows[4] = {"", "", "", ""};
    
    // Iteramos de 2 en 2 para construir el ancho de los caracteres Braille
    for (size_t i = 0; i < width - 1; i += 2) {
        float val_left = history[i];
        float val_right = history[i + 1];

        for (int row = 0; row < 4; ++row) {
            unsigned char byte = 0;
            // Definimos umbrales para cada una de las 4 filas (de abajo hacia arriba)
            // Fila 0 (fondo): 0-25, Fila 1: 26-50, Fila 2: 51-75, Fila 3 (techo): 76-100
            float threshold_low = (3 - row) * 25.0f;
            float threshold_high = threshold_low + 25.0f;

            // Lógica para el punto izquierdo del carácter Braille
            if (val_left > threshold_low) {
                // Si sobrepasa el umbral de la fila, activamos puntos según la altura interna
                if (val_left >= threshold_high) byte |= 0x07; // Columna izquierda completa (puntos 1,2,3)
                else byte |= 0x01; // Solo el punto base de esa fila
            }
            
            // Lógica para el punto derecho
            if (val_right > threshold_low) {
                if (val_right >= threshold_high) byte |= 0x38; // Columna derecha completa (puntos 4,5,6)
                else byte |= 0x08; 
            }

            rows[row] += get_braille(byte);
        }
    }

    // Unimos las filas con saltos de línea para el programa principal
    return rows[0] + "\n" + rows[1] + "\n" + rows[2] + "\n" + rows[3];
}

std::string draw_block_histogram_color(const std::vector<float>& history, std::string label, float max_val) {
    if (history.empty()) return "";

    std::string levels[] = {" ", "░", "▒", "▓", "█"};
    int max_l = 4;
    std::string rows[GRAPH_HEIGHT];
    float unit = max_val / (float)GRAPH_HEIGHT;

    for (int r = 0; r < GRAPH_HEIGHT; ++r) {
        float row_bottom = (float)(GRAPH_HEIGHT - 1 - r) * unit;
        
        // 1. Ponemos el color UNA VEZ al inicio de la fila
        if (r < 2) rows[r] += "\033[31m";      // Rojo (Arriba)
        else if (r < 4) rows[r] += "\033[33m"; // Amarillo (Medio)
        else rows[r] += "\033[32m";            // Verde (Abajo)

        for (size_t i = 0; i < history.size(); ++i) {
            float val = history[i];

            if (val >= row_bottom + unit) {
                rows[r] += levels[max_l];
            } else if (val > row_bottom) {
                float rel = (val - row_bottom) / unit;
                int idx = (int)(rel * max_l);
                rows[r] += levels[idx > 0 ? idx : 1];
            } else {
                // Para evitar que el color pinte los espacios vacíos del fondo, 
                // cerramos color, ponemos espacio, y reabrimos color.
                // Pero si tu fondo de widget es sólido, un simple " " basta.
                rows[r] += " "; 
            }
        }
        // 2. Cerramos el color al final de la fila
        rows[r] += "\033[0m";
    }

    // --- ENCAPSULACIÓN (Asegurando ancho fijo) ---
    size_t target_width = history.size(); 
    size_t visual_len = utf8_length(label);
    
    // Usamos el blanco brillante de tu tema para el marco
    std::string box = "\033[97m┌─ " + label + " "; 
    
    // Padding calculado sobre el ancho visual real
    int padding = (int)target_width - (int)visual_len - 3;
    if (padding < 0) padding = 0;
    
    for (int i = 0; i < padding; ++i) box += "─";
    box += "┐\n";

    for (int i = 0; i < GRAPH_HEIGHT; ++i) {
        box += "│" + rows[i] + "\033[97m│\n";
    }

    box += "└";
    for (size_t i = 0; i < target_width; ++i) box += "─";
    box += "┘\033[0m\n";

    return box;
}

std::string draw_block_histogram(const std::vector<float>& history, std::string label, float max_val) {
    if (history.empty()) return "";

    // Mapeo de densidad (IBM style)
    std::string levels[] = {" ", "░", "▒", "▓", "█"};
    int max_l = 4; // 4 niveles de densidad + vacío

    std::string rows[GRAPH_HEIGHT];
    float unit = max_val / (float)GRAPH_HEIGHT;

    // Aquí recorremos CADA punto del historial (no de 2 en 2)
    for (size_t i = 0; i < history.size(); ++i) {
        float val = history[i];

        for (int r = 0; r < GRAPH_HEIGHT; ++r) {
            float row_bottom = (float)(GRAPH_HEIGHT - 1 - r) * unit;
            
            if (val >= row_bottom + unit) {
                rows[r] += levels[max_l]; // Fila llena: Sólido
            } else if (val > row_bottom) {
                float rel = (val - row_bottom) / unit;
                int idx = (int)(rel * max_l);
                rows[r] += levels[idx > 0 ? idx : 1];
            } else {
                rows[r] += " "; // Vacío
            }
        }
    }

    // --- ENCAPSULACIÓN DEL MARCO ---
    size_t target_width = history.size(); // Ancho real de la historia
    size_t visual_len = utf8_length(label);
    
    std::string box = "┌─ " + label + " ";
    int padding = (int)target_width - (int)visual_len - 3;
    for (int i = 0; i < padding; ++i) box += "─";
    box += "┐\n";

    for (int i = 0; i < GRAPH_HEIGHT; ++i) {
        box += "│" + rows[i] + "│\n";
    }

    box += "└";
    for (size_t i = 0; i < target_width; ++i) box += "─";
    box += "┘\n";

    return box;
}

std::string draw_braille_histogram(const std::vector<float>& history, std::string label, float max_val) {
    if (history.empty()) return "";
    size_t visual_len = utf8_length(label);
    size_t max_visual_width = (MAX_HISTORY / 2) - 4;

    if (visual_len > max_visual_width) {
        // Si es muy largo, lo cortamos (aquí el substr es por bytes, 
        // pero para labels simples en Salamanca funcionará)
        label = label.substr(0, max_visual_width - 1) + "…";
        visual_len = utf8_length(label);
    }
    // Máscaras de bits para crecimiento vertical (Histograma)
    // Columna Izquierda (Puntos 1, 2, 3, 7)
#ifdef BRAILLE_8P
    unsigned char left_levels[] = {
        0x00, // vacio
        0x40, // solo base (Punto 7)
        0x44, // 2 puntos (7+3)
        0x46, // 3 puntos (7+3+2)
        0x47  // lleno (7+3+2+1)
    };

    // Columna Derecha (Puntos 4, 5, 6, 8)
    unsigned char right_levels[] = {
        0x00, // vacio
        0x80, // solo base (Punto 8)
        0xA0, // 2 puntos (8+6)
        0xB0, // 3 puntos (8+6+5)
        0xB8  // lleno (8+6+5+4)
    };
    int max_levels = 4;
#else
    unsigned char left_levels[] = {
        0x00, // Vacío
        0x04, // 1 punto (base: Punto 3)
        0x06, // 2 puntos (3+2)
        0x07  // 3 puntos (3+2+1)
    };

    unsigned char right_levels[] = {
        0x00, 
        0x20, // 1 punto (base: Punto 6)
        0x30, // 2 puntos (6+5)
        0x38  // 3 puntos (6+5+4)
    };
    int max_levels = 3;
#endif

    std::string rows[GRAPH_HEIGHT];
    float unit = max_val / (float)GRAPH_HEIGHT;

    for (size_t i = 0; i < MAX_HISTORY - 1; i += 2) {
        float v1 = history[i];     // Valor para la columna izquierda del caracter
        float v2 = history[i + 1]; // Valor para la columna derecha del caracter

        for (int r = 0; r < GRAPH_HEIGHT; ++r) {
        unsigned char byte = 0;
        float row_bottom = (float)(GRAPH_HEIGHT - 1 - r) * unit;

            if (v1 >= row_bottom + unit) {
                byte |= left_levels[max_levels]; 
            } else if (v1 > row_bottom) {
                float rel = (v1 - row_bottom) / unit;
                int lvl = (int)(rel * max_levels); 
                byte |= left_levels[lvl > 0 ? lvl : 1];
            }

            // --- Columna Derecha ---
            if (v2 >= row_bottom + unit) {
                byte |= right_levels[max_levels];
            } else if (v2 > row_bottom) {
                float rel = (v2 - row_bottom) / unit;
                int lvl = (int)(rel * max_levels);
                byte |= right_levels[lvl > 0 ? lvl : 1];
            }
            
            rows[r] += get_braille(byte);
        }
    }

    // --- ENCAPSULACIÓN DEL MARCO ---
    std::string box = "┌─ " + label + " ";
    
    // Calculamos el padding basado en el ancho visual, no en bytes
    int target_width = (MAX_HISTORY / 2);
    int current_visual_pos = visual_len + 3; // +3 por "┌─ " y el espacio tras el label
    
    for (int i = 0; i < (target_width - current_visual_pos); ++i) {
        box += "─";
    }
    box += "┐\n";

    // 3. Cuerpo de la gráfica
    for (int i = 0; i < GRAPH_HEIGHT; ++i) {
        box += "│" + rows[i] + "│\n";
    }

    // 4. Tapa Inferior
    box += "└";
    for (size_t i = 0; i < MAX_HISTORY / 2; ++i) box += "─";
    box += "┘\n";

    return box;
}

float get_cpu_usage() {
    static long double old_total, old_idle;
    long double a[10], total, idle;
    std::ifstream file("/proc/stat");
    std::string cpu;
    file >> cpu >> a[0] >> a[1] >> a[2] >> a[3] >> a[4] >> a[5] >> a[6] >> a[7] >> a[8] >> a[9];
    file.close();

    idle = a[3] + a[4];
    total = a[0]+a[1]+a[2]+a[3]+a[4]+a[5]+a[6]+a[7]+a[8]+a[9];
    
    float usage = 100.0f * (1.0f - (idle - old_idle) / (total - old_total));
    old_idle = idle; old_total = total;
    return usage;
}

int get_battery_level() {
    std::ifstream file("/sys/class/power_supply/BAT1/capacity");
    int capacity = -1;
    if (!(file >> capacity)) return -1;
    return capacity;
}

SysStats get_system_stats() {
    SysStats stats;
    static float_v global_temp_history(MAX_HISTORY, 0.0f);
    static float_v global_load_history(MAX_HISTORY, 0.0f);
    stats.battery = get_battery_level();

    if (sensors_init(NULL) == 0) {
        const sensors_chip_name *cn;
        int c = 0;
        bool found = false;
        
        while ((cn = sensors_get_detected_chips(NULL, &c)) && !found) {
            if (std::string(cn->prefix) == "coretemp") {
                const sensors_feature *feat;
                int f = 0;
                while ((feat = sensors_get_features(cn, &f))) {
                    if (feat->type == SENSORS_FEATURE_TEMP) {
                        double val;
                        if (sensors_get_value(cn, feat->number, &val) == 0) {
                            stats.cpu_temp = static_cast<float>(val);
                            found = true;
                            break; 
                        }
                    }
                }
            }
        }
        sensors_cleanup();
    }

    // --- Gestión del Historial (Fuera de los bucles) ---
    if (stats.cpu_temp > 0) {
        global_cpu_history.push_back(stats.cpu_temp);
        if (global_cpu_history.size() > MAX_HISTORY) {
            global_cpu_history.erase(global_cpu_history.begin());
        }
    }
    
    // Copiamos el historial a la estructura para que main pueda usarlo
    stats.cpu_history = global_cpu_history;
    stats.cpu_usage = get_cpu_usage();

    // Actualizar historial de Temp
    global_temp_history.erase(global_temp_history.begin());
    global_temp_history.push_back(stats.cpu_temp);
    
    // Actualizar historial de Load
    global_load_history.erase(global_load_history.begin());
    global_load_history.push_back(stats.cpu_usage);

    stats.cpu_history = global_temp_history;
    stats.load_history = global_load_history;

    return stats;
}

// --- BLOQUE DE PRUEBA ---
// Solo se compila si definimos BUILDING_STANDALONE
#ifdef BUILDING_STANDALONE
#include <unistd.h>
int main() {
    while(true) {
        SysStats s = get_system_stats();
        #ifndef BUILDING_WIDGET
            std::cout << "\033[2J\033[H"; 
        #endif
        
        std::cout << "  CPU Metrics" << std::endl;
        std::cout << draw_block_histogram_color(s.load_history, "Load " + std::to_string((int)s.cpu_usage) + "%", 100.0f) << std::endl;
        std::cout << draw_block_histogram_color(s.cpu_history, "Temp " + std::to_string((int)s.cpu_temp) + "°C", 100.0f) << std::endl;
        
        std::cout << "  Battery: " << s.battery << "%" << std::endl;
        std::flush(std::cout);
        usleep(500000); 
    }
}
#endif
