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
    const int line_spacing = 10;

    while (std::getline(layout_file, line)) {
        trim_string(line);
        if (line.empty() || line[0] == '#') continue;

        // 1. Obtener la rejilla de widgets de la línea actual
        Widgets current_row;
        populate_widgets_from_layout(line, config, current_row);
        
        int current_x = start_x;
        int max_row_h = 0;

        // 2. Iterar sobre cada celda (widget) de la fila
        for (const auto& widget_lines : current_row) {
            if (widget_lines.empty()) continue;

            // Calcular ancho visual máximo de este widget específico
            size_t max_v_w = 0;
            for (const auto& l : widget_lines) {
                size_t v = visual_width(l);
                if (v > max_v_w) max_v_w = v;
            }

            // Dimensiones de la caja HUD para este widget
            int line_h = config.widget_font_px + line_spacing;
            int bg_w = max_v_w * (config.widget_font_px * 0.65) + 20; 
            int bg_h = widget_lines.size() * line_h + 5;

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
                    ft2->putText(frame, t_line, cv::Point(current_x + 5, text_y), 
                                 config.widget_font_px, font_color, -1, cv::LINE_AA, true);
                    text_y += line_h;
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


void _______draw_system_widget(cv::Mat& frame, const WallpaperConfig& config, const std::string& text) {
    if (ft2.empty()) {
        ft2 = cv::freetype::createFreeType2();
        ft2->loadFontData(resolve_font_name(config.widget_font), 0);
    }

    int y_cursor = config.rn_height * config.widget_y_prop;

    if (config.widget_cmd == ".") {
        // Modo Archivo de Layout
        render_widget_from_file(frame, config, y_cursor);
    } else {
        // Modo Comando Tradicional
        render_widget_from_cmd(frame, config, text, y_cursor);
    }
}

void ____draw_system_widget(cv::Mat& frame, const WallpaperConfig& config, const std::string& text) {
    // Si no hay comando (.), usamos el archivo de widgets. Si no hay nada, salimos.
    if (config.widget_cmd != "." && config.widget_cmd.empty()) return;

    std::vector<std::string> layout_lines;

    // 1. Lógica de selección de fuente de datos
    if (config.widget_cmd == ".") {
        std::ifstream ifs(config.widgets_file);
        if (!ifs.is_open()) return; // Si el archivo no existe, no dibujamos nada

        std::string line;
        while (std::getline(ifs, line)) {
            trim_string(line);
            if (!line.empty() && line[0] != '#') {
                layout_lines.push_back(line);
            }
        }
    } else {
        // Si hay un comando real, usamos su salida (text) como layout
        if (!text.empty()) layout_lines.push_back(text);
    }

    if (layout_lines.empty()) return;

    // Inicializar FreeType
    if (ft2.empty()) {
        ft2 = cv::freetype::createFreeType2();
        ft2->loadFontData(resolve_font_name(config.widget_font), 0);
    }

    int current_y = config.rn_height * config.widget_y_prop;
    const int widget_x = config.rn_width * config.widget_x_prop;
    const int line_spacing = 10;

    // 2. Renderizado por cada fila de widgets
    for (const auto& layout_row : layout_lines) {
        Widgets row_data;
        populate_widgets_from_layout(layout_row, config, row_data);
        
        if (row_data.empty()) continue;

        widget_text final_render; // <--- Definida correctamente aquí
        assemble_widgets_row(row_data, config, final_render);

        if (final_render.empty()) continue;

        // --- Geometría del fondo HUD ---
        size_t max_v_w = 0;
        for (const auto& l : final_render) {
            size_t v_w = visual_width(l);
            if (v_w > max_v_w) max_v_w = v_w;
        }

        int line_height = config.widget_font_px + line_spacing;
        int bg_w = max_v_w * (config.widget_font_px * 0.65) + 20; 
        int bg_h = final_render.size() * line_height + 5;

        cv::Rect roi(widget_x - 10, current_y - config.widget_font_px, bg_w, bg_h);
        roi &= cv::Rect(0, 0, frame.cols, frame.rows);

        if (roi.width > 5 && roi.height > 5) {
            // Fondo HUD
            cv::Mat overlay = frame(roi).clone();
            overlay.setTo(cv::Scalar(0, 0, 0));
            cv::addWeighted(overlay, 0.45, frame(roi), 0.55, 0, frame(roi));
            cv::rectangle(frame, roi, cv::Scalar(255, 255, 0), 1);

            cv::Scalar font_color = get_adaptive_color(cv::mean(frame(roi)), true);
            font_color[3] = 255;

            // Dibujar líneas
            int text_y = current_y;
            for (const auto& l : final_render) {
                ft2->putText(frame, l, cv::Point(widget_x, text_y), 
                             config.widget_font_px, font_color, -1, cv::LINE_AA, true);
                text_y += line_height;
            }

            // Desplazar cursor para la siguiente fila del layout
            current_y += (final_render.size() * line_height) + (config.widget_box_sh * line_spacing);
        }
    }
}

void __render_widget_from_cmd(cv::Mat& frame, const WallpaperConfig& config, const std::string& text, int& y_cursor) {
    if (text.empty()) return;

    std::stringstream ss(text);
    std::vector<std::string> lines;
    std::string line;
    size_t max_v_w = 0;

    while (std::getline(ss, line)) {
        lines.push_back(line);
        size_t v_w = visual_width(line);
        if (v_w > max_v_w) max_v_w = v_w;
    }

    int line_height = config.widget_font_px + 10;
    int widget_x = config.rn_width * config.widget_x_prop;
    int bg_w = max_v_w * (config.widget_font_px * 0.65) + 20; 
    int bg_h = lines.size() * line_height + 10;

    cv::Rect roi(widget_x - 10, y_cursor - config.widget_font_px, bg_w, bg_h);
    roi &= cv::Rect(0, 0, frame.cols, frame.rows);

    if (roi.width > 5 && roi.height > 5) {
        cv::Mat overlay = frame(roi).clone();
        overlay.setTo(cv::Scalar(0, 0, 0)); 
        cv::addWeighted(overlay, 0.45, frame(roi), 0.55, 0, frame(roi));
        cv::rectangle(frame, roi, cv::Scalar(255, 255, 0), 1);

        cv::Scalar font_color = get_adaptive_color(cv::mean(frame(roi)), true);
        font_color[3] = 255;

        for (const auto& l : lines) {
            ft2->putText(frame, l, cv::Point(widget_x, y_cursor), 
                         config.widget_font_px, font_color, -1, cv::LINE_AA, true);
            y_cursor += line_height;
        }
    }
}

void __render_widget_from_file(cv::Mat& frame, const WallpaperConfig& config, int& y_cursor) {
    std::ifstream layout_file(config.widgets_file);
    if (!layout_file.is_open()) return;

    int widget_x = config.rn_width * config.widget_x_prop;
    int line_spacing = 10;
    std::string line;

    while (std::getline(layout_file, line)) {
        trim_string(line);
        if (line.empty() || line[0] == '#') continue;

        Widgets current_row;
        populate_widgets_from_layout(line, config, current_row);
        
        widget_text final_render;
        assemble_widgets_row(current_row, config, final_render);

        if (final_render.empty()) continue;

        // Calcular dimensiones de la fila ensamblada
        size_t max_v_w = 0;
        for (const auto& l : final_render) {
            size_t v_w = visual_width(l);
            if (v_w > max_v_w) max_v_w = v_w;
        }

        int line_height = config.widget_font_px + line_spacing;
        int bg_w = max_v_w * (config.widget_font_px * 0.65) + 20; 
        int bg_h = final_render.size() * line_height + 5;

        cv::Rect roi(widget_x - 10, y_cursor - config.widget_font_px, bg_w, bg_h);
        roi &= cv::Rect(0, 0, frame.cols, frame.rows);

        if (roi.width > 5 && roi.height > 5) {
            cv::Mat overlay = frame(roi).clone();
            overlay.setTo(cv::Scalar(0, 0, 0));
            cv::addWeighted(overlay, 0.45, frame(roi), 0.55, 0, frame(roi));
            cv::rectangle(frame, roi, cv::Scalar(255, 255, 0), 1);

            cv::Scalar font_color = get_adaptive_color(cv::mean(frame(roi)), true);
            font_color[3] = 255;

            int row_y = y_cursor;
            for (const auto& l : final_render) {
                ft2->putText(frame, l, cv::Point(widget_x, row_y), 
                             config.widget_font_px, font_color, -1, cv::LINE_AA, true);
                row_y += line_height;
            }
            // Avanzar el cursor para la siguiente fila del archivo
            y_cursor += (final_render.size() * line_height) + (config.widget_box_sh * line_spacing);
        }
    }
}

void __draw_system_widget(cv::Mat& frame, const WallpaperConfig& config, const std::string& layout_line) {
    if (layout_line.empty() || layout_line[0] == '#') return;

    // 1. Cargar y Procesar Widgets (Lógica del Parser integrada)
    Widgets current_row;
    populate_widgets_from_layout(layout_line, config, current_row);
    
    if (current_row.empty()) return;

    // 2. Ensamblar la fila (Genera el vector de strings con padding visual)
    widget_text final_lines;
    assemble_widgets_row(current_row, config, final_lines);

    if (final_lines.empty()) return;

    // 3. Cálculos de Geometría para el Fondo (HUD Box)
    size_t max_v_w = 0;
    for (const auto& l : final_lines) {
        size_t v_w = visual_width(l);
        if (v_w > max_v_w) max_v_w = v_w;
    }

    int line_height = config.widget_font_px + 10;
    int x_start = config.rn_width * config.widget_x_prop;
    int y_start = config.rn_height * config.widget_y_prop;

    // Dimensiones de la caja
    int bg_w = max_v_w * (config.widget_font_px * 0.65) + 20; 
    int bg_h = final_lines.size() * line_height + 10;

    // Definir y validar ROI
    cv::Rect roi(x_start - 10, y_start - config.widget_font_px, bg_w, bg_h);
    roi &= cv::Rect(0, 0, frame.cols, frame.rows);

    // 4. Renderizado
    if (roi.width > 5 && roi.height > 5) {
        // Fondo semitransparente
        cv::Mat overlay = frame(roi).clone();
        overlay.setTo(cv::Scalar(0, 0, 0)); 
        cv::addWeighted(overlay, 0.45, frame(roi), 0.55, 0, frame(roi));
        
        // Borde HUD (Cyan/Cian para ese toque técnico)
        cv::rectangle(frame, roi, cv::Scalar(255, 255, 0), 1); 

        // Texto Adaptativo basado en el cuadro negro
        cv::Scalar font_color = get_adaptive_color(cv::mean(frame(roi)), true);
        font_color[3] = 255; 

        if (ft2.empty()) {
            ft2 = cv::freetype::createFreeType2();
            ft2->loadFontData(resolve_font_name(config.widget_font), 0);
        }

        int current_y = y_start;
        for (const auto& l : final_lines) {
            ft2->putText(frame, l, cv::Point(x_start, current_y), 
                         config.widget_font_px, font_color, -1, cv::LINE_AA, true);
            current_y += line_height;
        }
    }
}

void _draw_system_widget(cv::Mat& frame, const WallpaperConfig& config, const std::string& text) {
    if (text.empty()) return;

    // Asegurar que FreeType esté listo
    if (ft2.empty()) {
        ft2 = cv::freetype::createFreeType2();
        ft2->loadFontData(resolve_font_name(config.widget_font), 0);
    }

    std::stringstream ss(text);
    std::vector<std::string> lines;
    std::string line;
    size_t max_v_w = 0;

    while (std::getline(ss, line)) {
        lines.push_back(line);
        size_t v_w = visual_width(line);
        if (v_w > max_v_w) max_v_w = v_w;
    }

    int line_height = config.widget_font_px + 10;
    int widget_x = config.rn_width * config.widget_x_prop;
    int y_start = config.rn_height * config.widget_y_prop;

    // --- CÁLCULO DINÁMICO DEL BOX ---
    // El ancho es aprox: caracteres * (tamaño_fuente * factor_escala_monospaced)
    // El factor 0.65 suele ser el estándar para fuentes como JetBrains Mono o Courier
    int bg_w = max_v_w * (config.widget_font_px * 0.65) + 20; 
    int bg_h = lines.size() * line_height + 10;

    // El ROI debe empezar un poco más arriba del primer cursor para cubrir las mayúsculas
    cv::Rect roi(widget_x - 10, y_start - config.widget_font_px, bg_w, bg_h);
    
    // Intersección crítica para evitar crash
    roi &= cv::Rect(0, 0, frame.cols, frame.rows);

    if (roi.width > 5 && roi.height > 5) {
        // Dibujar el fondo semitransparente
        cv::Mat overlay = frame(roi).clone();
        overlay.setTo(cv::Scalar(0, 0, 0)); 
        cv::addWeighted(overlay, 0.5, frame(roi), 0.5, 0, frame(roi));
        
        // Borde HUD opcional
        cv::rectangle(frame, roi, cv::Scalar(0, 255, 255), 1);

        // Color adaptativo basado SOLO en lo que hay dentro del cuadro
        cv::Scalar font_color = get_adaptive_color(cv::mean(frame(roi)), true);
        font_color[3] = 255; // Asegurar opacidad total del texto

        // Dibujar líneas
        int current_y = y_start;
        for (const auto& l : lines) {
            ft2->putText(frame, l, cv::Point(widget_x, current_y), 
                         config.widget_font_px, font_color, -1, cv::LINE_AA, true);
            current_y += line_height;
        }
    }
}

