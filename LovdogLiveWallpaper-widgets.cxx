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

void populate_widgets_from_layout(const std::string& layout_line, 
                                 const WallpaperConfig& cfg, 
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
    std::ifstream layout_file(config.widgets_file);
    if (!layout_file.is_open()) return;

    std::string line;
    const int start_x = config.rn_width * config.widget_x_prop;

    while (std::getline(layout_file, line)) {
        trim_string(line);
        if (line.empty() || line[0] == '#') continue;

        // 1. Obtener la rejilla de widgets de la línea actual
        Widgets current_row;
        populate_widgets_from_layout(line, config, current_row);
        
        int current_x = start_x;
        int max_row_h = 0;

        int max_pixel_width = 0;
        int line_spacing = 10;

        // 2. Iterar sobre cada celda (widget) de la fila
        for (const auto& widget_lines : current_row) {
            if (widget_lines.empty()) continue;

            // Calcular ancho visual máximo de este widget específico
            for (const auto& l : widget_lines) {
                int baseLine = 0;
                // Medimos el tamaño que ocupará el texto en píxeles
                cv::Size textSize = ft2->getTextSize(l, config.widget_font_px, -1, &baseLine);
                
                if (textSize.width > max_pixel_width) max_pixel_width = textSize.width;
            }

            // Dimensiones de la caja HUD para este widget
            int padding_h = 20; // Espacio extra a los lados
            int padding_v = 10; // Espacio extra arriba/abajo
            
            int bg_w = max_pixel_width + padding_h;
            int bg_h = (widget_lines.size() * (config.widget_font_px + line_spacing)) + padding_v;

            if (bg_h > max_row_h) max_row_h = bg_h;

            // Definir ROI para la caja individual
            cv::Rect roi(current_x, y_cursor - config.widget_font_px, bg_w, bg_h);
            roi &= cv::Rect(0, 0, frame.cols, frame.rows);

            if (roi.width > 5 && roi.height > 5) {
                // Dibujar fondo semitransparente
                cv::Mat overlay = frame(roi).clone();
                overlay.setTo(cv::Scalar(0, 0, 0));
                cv::addWeighted(overlay, 0.45, frame(roi), 0.55, 0, frame(roi));
                
                // Borde HUD (Cian/Amarillo)
                cv::rectangle(frame, roi, cv::Scalar(255, 255, 0), 1);

                // Color adaptativo local
                cv::Scalar font_color = get_adaptive_color(cv::mean(frame(roi)), true);
                font_color[3] = 255;

                // Renderizar el texto dentro de la caja
                int text_y = y_cursor;
                for (const auto& t_line : widget_lines) {
                    // Añadimos +10 en X para que no pegue al borde izquierdo de la caja
                    ft2->putText(frame, t_line, cv::Point(current_x + 10, text_y), 
                                 config.widget_font_px, font_color, -1, cv::LINE_AA, true);
                    text_y += (config.widget_font_px + line_spacing);
                }
            }

            // Desplazamiento horizontal: ancho de la caja + box_sw (como margen)
            current_x += bg_w + (config.widget_box_sw * 10);
        }

        // Salto de línea: alto de la fila + box_sh
        y_cursor += max_row_h + (config.widget_box_sh * 10);
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

    int line_h = config.widget_font_px + 10;
    int widget_x = config.rn_width * config.widget_x_prop;
    int bg_w = max_v_w * (config.widget_font_px * 0.65) + 20;
    int bg_h = lines.size() * line_h + 10;

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

