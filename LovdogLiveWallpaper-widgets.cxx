#include <cstdio>
#include <memory>
#include <string>
#include <array>
#include <opencv2/freetype.hpp>
#include <vector>
#include <fstream>

#include <fontconfig/fontconfig.h>
#include "LovdogLiveWallpaper.hxx"
#include "opencv2/imgproc.hpp"

struct CachedWidget {
    widget_text content;
    int ttl; // Time To Live (15 iteraciones)
};

static std::map<std::string, CachedWidget> widget_cache;
std::vector<WidgetElement> active_widgets_list;

// Idea para LovdogLiveWallpaper-widgets.cxx
/*
std::string get_command_output(const char* cmd) {
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "Error";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

std::string fetch_khal_agenda() {
    // El comando que ya te funcionó
    std::string command = "khal list today 7days --format '{title}'";
    std::array<char, 128> buffer;
    std::string result;
    
    // Abrimos el pipe de lectura
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) return "Error al abrir khal";

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}
*/

struct PcloseDeleter {
    void operator()(FILE* f) const {
        if (f) pclose(f);
    }
};

std::string fetch_command_output(const std::string& cmd) {
    if (cmd.empty()) return "";

    std::array<char, 128> buffer;
    std::string result;

    // Ahora usamos el functor en lugar del puntero a función directo
    std::unique_ptr<FILE, PcloseDeleter> pipe(popen(cmd.c_str(), "r"));

    if (!pipe) return "Error: popen failed";

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    return result;
}

cv::Scalar get_adaptive_color(cv::Scalar avg, bool is_text) {
    cv::Scalar result;
    for(int i = 0; i < 3; i++) {
        if (is_text) {
            // Rango 128 - 255: Mapea el promedio para que siempre sea brillante
            // Si el fondo es oscuro, tiende a 255. Si es claro, se queda en 128.
            result[i] = 128 + (avg[i] / 2); 
        } else {
            // Rango 0 - 100: Mapea el promedio para que siempre sea oscuro
            // El borde será una versión muy oscurecida del fondo.
            result[i] = (avg[i] / 2.5); 
        }
    }
    return result;
}

std::string resolve_font_name(const std::string& font_name) {
    FcConfig* config = FcInitLoadConfigAndFonts();
    // Crear un patrón con el nombre que dio el usuario
    FcPattern* pat = FcNameParse((const FcChar8*)font_name.c_str());
    
    FcConfigSubstitute(config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    
    FcResult result;
    // Encontrar la fuente que mejor coincida
    FcPattern* font = FcFontMatch(config, pat, &result);
    
    std::string path = "";
    if (font) {
        FcChar8* file = NULL;
        if (FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch) {
            path = (char*)file; // Aquí tienes la ruta al .ttf
        }
        FcPatternDestroy(font);
    }
    
    FcPatternDestroy(pat);
    FcConfigDestroy(config);
    return path;
}

size_t visual_width(const std::string& s) {
    size_t width = 0;
    for (size_t i = 0; i < s.size(); i++)
        // Ignorar bytes de continuación en UTF-8 (0x80 a 0xBF)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            width++;
    return width;
}

void trim_string(std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        s.clear();
        return;
    }
    size_t last = s.find_last_not_of(" \t\r\n");
    s = s.substr(first, (last - first + 1));
}

void load_widget_file(const std::string& path, widget_text& text_out) {
    text_out.clear();
    std::ifstream ifs(path);
    if (!ifs.is_open()) return;

    std::string line;
    while (std::getline(ifs, line)) {
        text_out.push_back(line);
    }
}

void populate_widgets_from_layout(
    const std::string& layout_line, 
    const WallpaperConfig& cfg, 
    Widgets& widgets_out,
    char &h_gaps
){
    widgets_out.clear();
    h_gaps = 0; // Reset máscara

    // Detectar gaps en los extremos (Bitmasking)
    if (layout_line.size() >= 2) {
        if (layout_line.substr(0, 2) == "::") h_gaps |= 0b10;
        if (layout_line.substr(layout_line.size() - 2) == "::") h_gaps |= 0b01;
    }
    
    size_t start = 0;
    size_t end = layout_line.find("::");

    while (true) {
        std::string token = layout_line.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
        trim_string(token);

        // Solo agregamos si hay contenido real. Los :: vacíos ya los marcó h_gaps.
        if (!token.empty()) {
            std::string full_path = (token[0] == '/') ? token : cfg.prefix + token;
            widget_text current_content;
            load_widget_file(full_path, current_content);
            widgets_out.push_back(std::move(current_content)); 
        }

        if (end == std::string::npos) break;
        start = end + 2;
        end = layout_line.find("::", start);
    }
}

void _populate_widgets_from_layout(
        const std::string& layout_line, 
        const WallpaperConfig& cfg, 
        Widgets& widgets_out
){
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

void assemble_widgets_row(const Widgets& widgets, const WallpaperConfig& cfg, widget_text& row_out) {
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


static cv::Ptr<cv::freetype::FreeType2> ft2;

void render_widget_from_file(cv::Mat& frame, const WallpaperConfig& config, int& y_cursor) {
    std::vector<std::string> layout_lines;
    
    // Rápido: Leer archivo y cerrar
    {
        std::ifstream file(config.widgets_file);
        if (!file.is_open()) return;
        std::string l;
        while (std::getline(file, l)) {
            trim_string(l);
            if (!l.empty() && l[0] != '#') layout_lines.push_back(l);
        }
    } // El archivo se cierra aquí automáticamente

    int row_idx = 0;
    for (const auto& line : layout_lines) {
        Widgets row_widgets;
        char h_gaps = 0;
        populate_widgets_from_layout(line, config, row_widgets, h_gaps);

        // --- Procesamiento de la fila hacia WidgetElements ---
        std::vector<WidgetElement> elements;
        int total_row_w = 0;
        int max_row_h = 0;
        int line_h = config.widget_font_px + 0;

        for (size_t i = 0; i < row_widgets.size(); ++i) {
            WidgetElement el;
            el.widget = std::move(row_widgets[i]);
            el.position = h_gaps;
            el.ttl = 15;
            el.background_opacity = 0.45f;
            el.border_opacity = 1.0f;
            el.border_color = cv::Scalar(255, 255, 0);

            // Medir dimensiones
            int max_w = 0;
            for (const auto& txt : el.widget) {
                int bl = 0;
                cv::Size sz = ft2->getTextSize(txt, config.widget_font_px, -1, &bl);
                if (sz.width > max_w) max_w = sz.width;
            }
            el.box_width = max_w + 20;
            el.box_height = (el.widget.size() * line_h) + 10;
            
            if (el.box_height > max_row_h) max_row_h = el.box_height;
            total_row_w += el.box_width;
            elements.push_back(std::move(el));
        }

        // --- Cálculo de Posiciones (Usando tu lógica de Switch) ---
        int gap = (config.widget_box_sw == -1) ? 20 : (config.widget_box_sw * 10);
        int start_x = config.rn_width * config.widget_x_prop;
        int right_limit = config.rn_width - start_x;
        int current_x = start_x;

        if (!elements.empty()) {
            int active = elements.size();
            switch (h_gaps) {
                case 0b11: // Centrado
                    current_x = (config.rn_width - (total_row_w + (active - 1) * gap)) / 2;
                    break;
                case 0b10: // Derecha
                    current_x = right_limit - (total_row_w + (active - 1) * gap);
                    break;
                case 0b00: // Repartido
                    if (config.widget_box_sw == -1 && active > 1)
                        gap = (right_limit - start_x - total_row_w) / (active - 1);
                    break;
            }

            // --- Renderizado de los elementos calculados ---
            for (auto& el : elements) {
                el.box_x = current_x;
                el.box_y = y_cursor - config.widget_font_px;

                cv::Rect roi(el.box_x, el.box_y, el.box_width, el.box_height);
                roi &= cv::Rect(0, 0, frame.cols, frame.rows);

                if (roi.width > 0 && roi.height > 0) {
                    cv::Mat sub = frame(roi);
                    cv::Mat overlay(sub.size(), sub.type(), cv::Scalar(0,0,0));
                    cv::addWeighted(overlay, el.background_opacity, sub, 1.0f - el.background_opacity, 0, sub);
                    cv::rectangle(frame, roi, el.border_color, 1);

                    int ty = y_cursor;
                    for (const auto& txt : el.widget) {
                        ft2->putText(frame, txt, cv::Point(el.box_x + 10, ty), 
                                     config.widget_font_px, cv::Scalar(255,255,255), -1, cv::LINE_AA, true);
                        ty += line_h;
                    }
                }
                current_x += el.box_width + gap;
            }
        }
        y_cursor += max_row_h + (config.widget_box_sh * 10);
        row_idx++;
    }
}

void __render_widget_from_file(cv::Mat& frame, const WallpaperConfig& config, int& y_cursor) {
    std::ifstream layout_file(config.widgets_file);
    if (!layout_file.is_open()) return;

    std::string line;
    int row_idx = 0;
    const int start_x = config.rn_width * config.widget_x_prop;

    while (std::getline(layout_file, line)) {
        trim_string(line);
        if (line.empty() || line[0] == '#') continue;

        Widgets current_row;
        _populate_widgets_from_layout(line, config, current_row);

        // --- 1. PRE-CÁLCULO DE ANCHOS PARA ESPACIADO ---
        std::vector<int> col_widths;
        int total_widgets_w = 0;
        std::vector<widget_text> row_data; // Cache temporal para no re-procesar

        for (size_t i = 0; i < current_row.size(); ++i) {
            std::string key = "row_" + std::to_string(row_idx) + "_col_" + std::to_string(i);
            widget_text display_text;

            // Lógica de persistencia (mismo bloque que ya tenías)
            if (current_row[i].empty()) {
                if (widget_cache.count(key) && widget_cache[key].ttl > 0) {
                    display_text = widget_cache[key].content;
                    widget_cache[key].ttl--;
                } else {
                    widget_cache.erase(key);
                    row_data.push_back({}); // Placeholder vacío
                    col_widths.push_back(0);
                    continue;
                }
            } else {
                display_text = current_row[i];
                widget_cache[key] = {display_text, 15};
            }

            // Medir ancho de este widget específico
            int max_w = 0;
            for (const auto& l : display_text) {
                int bl = 0;
                cv::Size sz = ft2->getTextSize(l, config.widget_font_px, -1, &bl);
                if (sz.width > max_w) max_w = sz.width;
            }
            int bg_w = max_w + 20;
            col_widths.push_back(bg_w);
            total_widgets_w += bg_w;
            row_data.push_back(display_text);
        }


        // --- 2. CÁLCULO DE ALINEACIÓN SEGÚN PLACEHOLDERS ---
        // 2.1. Construir la máscara de bits
        // 0b10 (2) -> Gap al inicio
        // 0b01 (1) -> Gap al final
        // 0b11 (3) -> Gap en ambos (Centrado)
        // 0b00 (0) -> Sin gaps (Repartido/Izquierda)
        unsigned char gap_found = 0b00;
        if (current_row.front().empty()) gap_found |= 0b10;
        if (current_row.back().empty())  gap_found |= 0b01;

        int gap = (config.widget_box_sw == -1) ? 20 : (config.widget_box_sw * 10);
        int current_x = start_x;
        int right_limit = config.rn_width - start_x;
        int available_w = right_limit - start_x;
        int active_widgets = 0;

        for(const auto& w : row_data) if(!w.empty()) active_widgets++;

        if (active_widgets > 0) {
            switch (gap_found) {
                case 0b11: // :: elem :: -> CENTRADO
                    current_x = (config.rn_width - (total_widgets_w + (active_widgets - 1) * gap)) / 2;
                    break;

                case 0b10: // :: elem -> DERECHA
                    current_x = right_limit - (total_widgets_w + (active_widgets - 1) * gap);
                    break;

                case 0b01: // elem :: -> IZQUIERDA
                    current_x = start_x;
                    break;

                case 0b00: // elem1 elem2 -> REPARTIDO o IZQUIERDA
                default:
                    current_x = start_x;
                    if (config.widget_box_sw == -1 && active_widgets > 1) {
                        gap = (available_w - total_widgets_w) / (active_widgets - 1);
                    }
                    break;
            }
        }

        // --- 3. DIBUJO REAL ---
        int max_row_h = 0;
        int line_h = config.widget_font_px + 0;

        for (size_t i = 0; i < row_data.size(); ++i) {
            if (row_data[i].empty()) continue;

            int bg_w = col_widths[i];
            int bg_h = (row_data[i].size() * line_h) + 10;
            if (bg_h > max_row_h) max_row_h = bg_h;

            cv::Rect roi(current_x, y_cursor - config.widget_font_px, bg_w, bg_h);
            roi &= cv::Rect(0, 0, frame.cols, frame.rows);

            if (roi.width > 0 && roi.height > 0) {
                cv::Mat subRegion = frame(roi);
                cv::Mat overlay(subRegion.size(), subRegion.type(), cv::Scalar(0, 0, 0));
                cv::addWeighted(overlay, 0.45, subRegion, 0.55, 0, subRegion);
                cv::rectangle(frame, roi, cv::Scalar(255, 255, 0), 1);

                int ty = y_cursor;
                for (const auto& l : row_data[i]) {
                    ft2->putText(frame, l, cv::Point(current_x + 10, ty), 
                                 config.widget_font_px, cv::Scalar(255, 255, 255), -1, cv::LINE_AA, true);
                    ty += line_h;
                }
            }
            current_x += bg_w + gap; // Usamos el gap calculado
        }
        y_cursor += max_row_h + (config.widget_box_sh * 10);
        row_idx++;
    }
}

void _render_widget_from_file(cv::Mat& frame, const WallpaperConfig& config, int& y_cursor) {
    std::ifstream layout_file(config.widgets_file);
    if (!layout_file.is_open()) return;

    std::string line;
    int row_idx = 0;
    const int start_x = config.rn_width * config.widget_x_prop;

    while (std::getline(layout_file, line)) {
        trim_string(line);
        if (line.empty() || line[0] == '#') continue;

        Widgets current_row;
        _populate_widgets_from_layout(line, config, current_row);

        int current_x = start_x;
        int max_row_h = 0;

        for (size_t i = 0; i < current_row.size(); ++i) {
            std::string key = "row_" + std::to_string(row_idx) + "_col_" + std::to_string(i);
            widget_text display_text;

            // --- Lógica de Persistencia ---
            if (current_row[i].empty()) {
                if (widget_cache.count(key) && widget_cache[key].ttl > 0) {
                    display_text = widget_cache[key].content;
                    widget_cache[key].ttl--;
                } else {
                    widget_cache.erase(key);
                    continue; // Desaparece definitivamente
                }
            } else {
                display_text = current_row[i];
                widget_cache[key] = {display_text, 15}; // Reset TTL
            }

            // --- Medición y Dibujo ---
            // (Usa ft2->getTextSize para asegurar que el Rect sea del tamaño correcto)
            int max_w = 0;
            int line_h = config.widget_font_px + 0;
            for (const auto& l : display_text) {
                int bl = 0;
                cv::Size sz = ft2->getTextSize(l, config.widget_font_px, -1, &bl);
                if (sz.width > max_w) max_w = sz.width;
            }

            int bg_w = max_w + 20;
            int bg_h = (display_text.size() * line_h) + 10;
            if (bg_h > max_row_h) max_row_h = bg_h;

            cv::Rect roi(current_x, y_cursor - config.widget_font_px, bg_w, bg_h);
            roi &= cv::Rect(0, 0, frame.cols, frame.rows);

            if (roi.width > 0 && roi.height > 0) {
                cv::Mat subRegion = frame(roi); // Esto es una referencia, no copia
                cv::Mat overlay(subRegion.size(), subRegion.type(), cv::Scalar(0, 0, 0));
                cv::addWeighted(overlay, 0.45, subRegion, 0.55, 0, subRegion);
                cv::rectangle(frame, roi, cv::Scalar(255, 255, 0), 1);

                int ty = y_cursor;
                for (const auto& l : display_text) {
                    ft2->putText(frame, l, cv::Point(current_x + 10, ty), 
                                 config.widget_font_px, cv::Scalar(255, 255, 255), -1, cv::LINE_AA, true);
                    ty += line_h;
                }
            }
            current_x += bg_w + (config.widget_box_sw * 10);
        }
        y_cursor += max_row_h + (config.widget_box_sh * 10);
        row_idx++;
    }
}


void render_widget_from_cmd(cv::Mat& frame, const WallpaperConfig& config, const std::string& text, int& y_cursor) {
    if (text.empty()) return;

    std::stringstream ss(text);
    std::vector<std::string> lines;
    std::string line;
    size_t max_v_w = 0;

    while (std::getline(ss, line)) {
        lines.push_back(line);
        size_t v = visual_width(line);
        if (v > max_v_w) max_v_w = v;
    }

    int line_h = config.widget_font_px + 5;
    int widget_x = config.rn_width * config.widget_x_prop;
    int bg_w = max_v_w * (config.widget_font_px * 0.65) + 20;
    int bg_h = lines.size() * line_h + 0;

    cv::Rect roi(widget_x - 10, y_cursor - config.widget_font_px, bg_w, bg_h);
    roi &= cv::Rect(0, 0, frame.cols, frame.rows);

    if (roi.width > 5 && roi.height > 5) {
        cv::Mat overlay = frame(roi).clone();
        overlay.setTo(cv::Scalar(0, 0, 0));
        cv::addWeighted(overlay, 0.45, frame(roi), 0.55, 0, frame(roi));
        cv::rectangle(frame, roi, cv::Scalar(255, 255, 0), 1);

        cv::Scalar font_color = get_adaptive_color(cv::mean(frame(roi)), true);
        for (const auto& l : lines) {
            ft2->putText(frame, l, cv::Point(widget_x, y_cursor), 
                         config.widget_font_px, font_color, -1, cv::LINE_AA, true);
            y_cursor += line_h;
        }
    }
}

void draw_system_widget(cv::Mat& frame, const WallpaperConfig& config, const std::string& text) {
    if (ft2.empty()) {
        ft2 = cv::freetype::createFreeType2();
        ft2->loadFontData(resolve_font_name(config.widget_font), 0);
    }

    int y_cursor = config.rn_height * config.widget_y_prop;

    if (config.widget_cmd == ".") {
        render_widget_from_file(frame, config, y_cursor);
    } else {
        render_widget_from_cmd(frame, config, text, y_cursor);
    }
}

