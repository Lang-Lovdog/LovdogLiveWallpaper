#include <vector>
#include <string>
#include <fstream>
#include <iostream>

 typedef struct WidgetConfig {
    size_t        widget_box_w  = 0                  ;
    size_t        widget_box_h  = 0                  ;
    size_t        widget_box_sw = 1                  ;
    size_t        widget_box_sh = 1                  ;
    std::string   sep_fill      = " "                ;
    std::string   prefix        =
                 "/tmp/lovdog_live_wallpaper_widget_"; 
}WidgetConfig;


typedef std::vector<std::string> widget_text;


typedef std::vector<widget_text> Widgets;


// Auxiliar para limpiar espacios (opcional pero recomendada)
void trim_string(std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        s.clear();
        return;
    }
    size_t last = s.find_last_not_of(" \t\r\n");
    s = s.substr(first, (last - first + 1));
}

// 1. Obtener las líneas de un archivo individual
void load_widget_file(const std::string& path, widget_text& text_out) {
    text_out.clear();
    std::ifstream ifs(path);
    if (!ifs.is_open()) return;

    std::string line;
    while (std::getline(ifs, line)) {
        text_out.push_back(line);
    }
}

// 2. Parsear la línea del layout y cargar todos los contenidos en el contenedor Widgets
void populate_widgets_from_layout(const std::string& layout_line, 
                                 const WidgetConfig& cfg, 
                                 Widgets& widgets_out) {
    widgets_out.clear();
    
    size_t start = 0;
    size_t end = layout_line.find("::");

    while (true) {
        // Extraer el nombre del widget o ruta
        std::string token = layout_line.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
        trim_string(token);

        if (!token.empty()) {
            // Resolver ruta
            std::string full_path = (token[0] == '/') ? token : cfg.prefix + token;
            
            // Creamos un widget_text temporal, lo llenamos y lo movemos al contenedor
            widget_text current_content;
            load_widget_file(full_path, current_content);
            widgets_out.push_back(std::move(current_content)); 
        }

        if (end == std::string::npos) break;
        start = end + 2;
        end = layout_line.find("::", start);
    }
}

// 3. Calcular el ancho máximo de todas las líneas en todos los widgets
// Esto es vital para el padding horizontal
size_t visual_width(const std::string& s) {
    size_t width = 0;
    for (size_t i = 0; i < s.size(); i++)
        // Ignorar bytes de continuación en UTF-8 (0x80 a 0xBF)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            width++;
    return width;
}

void compute_max_width(const Widgets& widgets, size_t& max_w_out) {
    max_w_out = 0;
    for (const auto& w : widgets)
        for (const auto& line : w) {
            size_t w_vis = visual_width(line); // <--- Cambio aquí
            if (w_vis > max_w_out) max_w_out = w_vis;
        }
}

// 4. Calcular la altura máxima (número de líneas) entre todos los widgets
void compute_max_height(const Widgets& widgets, size_t& max_h_out) {
    max_h_out = 0;
    for (const auto& w : widgets)
        if (w.size() > max_h_out) max_h_out = w.size();
}

// 5. Ensamblar las columnas en una sola fila de texto (Fila Final)
// Auxiliar para truncar strings UTF-8 sin romper glifos
std::string utf8_safe_substr(const std::string& s, size_t max_v_w) {
    std::string result = "";
    size_t current_v_w = 0;
    for (size_t i = 0; i < s.size(); ) {
        size_t len = 1;
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0xf0) len = 4;
        else if (c >= 0xe0) len = 3;
        else if (c >= 0xc0) len = 2;

        if (current_v_w + 1 > max_v_w) break;
        result += s.substr(i, len);
        current_v_w++;
        i += len;
    }
    return result;
}

void assemble_widgets_row(const Widgets& widgets, const WidgetConfig& cfg, widget_text& row_out) {
    row_out.clear();
    
    size_t mh;
    compute_max_height(widgets, mh);

    // 1. Calculamos los anchos específicos para cada columna
    std::vector<size_t> col_widths(widgets.size(), 0);
    for (size_t c = 0; c < widgets.size(); ++c) {
        if (cfg.widget_box_w > 0) {
            col_widths[c] = cfg.widget_box_w; // Ancho fijo por config
        } else {
            // Ancho adaptativo: buscamos el máximo visual de ESTA columna
            for (const auto& line : widgets[c]) {
                size_t v_w = visual_width(line);
                if (v_w > col_widths[c]) col_widths[c] = v_w;
            }
            // Si el widget está vacío, le damos al menos 1 espacio para que no colapse
            if (col_widths[c] == 0) col_widths[c] = 1;
        }
    }

    // 2. Determinamos la altura de la caja (box_h)
    size_t target_h = (cfg.widget_box_h > 0) ? cfg.widget_box_h : mh;

    // 3. Ensamblado con anchos variables
    for (size_t r = 0; r < target_h; ++r) {
        std::string current_line = "";
        
        for (size_t c = 0; c < widgets.size(); ++c) {
            const widget_text& col = widgets[c];
            size_t target_w = col_widths[c]; // Usamos el ancho calculado para esta columna
            
            std::string cell = (r < col.size()) ? col[r] : "";
            size_t v_w = visual_width(cell);

            if (v_w > target_w) {
                current_line += utf8_safe_substr(cell, target_w);
            } else {
                current_line += cell;
                current_line.append(target_w - v_w, ' '); // El padding ahora es relativo a su propia columna
            }

            // Añadir el separador visual (sw) entre cajas
            if (c + 1 < widgets.size()) {
                current_line.append(cfg.widget_box_sw, cfg.sep_fill[0]);
            }
        }
        row_out.push_back(current_line);
    }
}

void __assemble_widgets_row(const Widgets& widgets, const WidgetConfig& cfg, widget_text& row_out) {
    row_out.clear();
    size_t mw, mh;
    compute_max_width(widgets, mw);
    compute_max_height(widgets, mh);

    // Prioridad: Si box_w > 0 usamos ese, si no, el máximo calculado
    size_t target_w = (cfg.widget_box_w > 0) ? cfg.widget_box_w : mw;
    // Si box_h > 0 limitamos la altura, si no, usamos la necesaria
    size_t target_h = (cfg.widget_box_h > 0) ? cfg.widget_box_h : mh;

    for (size_t r = 0; r < target_h; ++r) {
        std::string current_line = "";
        for (size_t c = 0; c < widgets.size(); ++c) {
            const widget_text& col = widgets[c];
            std::string cell = (r < col.size()) ? col[r] : "";
            size_t v_w = visual_width(cell);

            if (v_w > target_w) {
                current_line += utf8_safe_substr(cell, target_w);
            } else {
                current_line += cell;
                current_line.append(target_w - v_w, ' ');
            }

            if (c + 1 < widgets.size()) {
                current_line.append(cfg.widget_box_sw, cfg.sep_fill[0]);
            }
        }
        row_out.push_back(current_line);
    }
}

int main(int argc, char** argv) {
    WidgetConfig cfg;
    std::string layout_path;

    // Simple Argument Parser
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-w" && i + 1 < argc) cfg.widget_box_w = std::stoul(argv[++i]);
        else if (arg == "-H" && i + 1 < argc) cfg.widget_box_h = std::stoul(argv[++i]);
        else if (arg == "-sw" && i + 1 < argc) cfg.widget_box_sw = std::stoul(argv[++i]);
        else if (arg == "-sh" && i + 1 < argc) cfg.widget_box_sh = std::stoul(argv[++i]);
        else if (arg == "-p" && i + 1 < argc) cfg.prefix = argv[++i];
        else layout_path = arg; // El último argumento sin flag se toma como el layout
    }

    if (layout_path.empty()) {
        std::cerr << "Uso: " << argv[0] << " [-w ancho] [-H alto] [-sw esp_h] [-sh esp_v] <layout_file>" << std::endl;
        return 1;
    }

    std::ifstream layout_file(layout_path);
    if (!layout_file.is_open()) {
        std::cerr << "Error: No se pudo abrir " << layout_path << std::endl;
        return 1;
    }

    std::string line;
    while (std::getline(layout_file, line)) {
        if (line.empty() || line[0] == '#') continue; // Ignorar comentarios

        Widgets current_row;
        populate_widgets_from_layout(line, cfg, current_row);

        widget_text final_render;
        assemble_widgets_row(current_row, cfg, final_render);

        for (const auto& l : final_render) {
            std::cout << l << std::endl;
        }

        // Aplicamos el espaciado vertical entre filas del layout (box_sh)
        for (size_t i = 0; i < cfg.widget_box_sh; ++i) {
            std::cout << std::endl;
        }
    }

    return 0;
}

