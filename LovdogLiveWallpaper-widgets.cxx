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

WidgetElement& find_or_create_element(const std::string& name, std::list<WidgetElement>& active_widgets_list) {
    // 1. Buscar si ya existe un widget con ese nombre
    for (auto& el : active_widgets_list) {
        if (el.name == name) {
            return el; 
        }
    }

    // 2. Si no existe, crear un nuevo elemento base
    WidgetElement new_el;
    new_el.name = name;
    new_el.fifo_fd = -1; // Se abrirá después
    new_el.ttl = 15;     // Valor por defecto o desde config
    
    std::cout << "\nWidget '" << name << "' created.\n";
    active_widgets_list.push_back(std::move(new_el));
    return active_widgets_list.back();
}

void load_widget_from_fifo(int fd, widget_text& out) {
    char buffer[8192]; // Un poco más grande para el clima
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        std::string raw_data(buffer);
        
        // Creamos un contenedor temporal
        std::vector<std::string> temp_lines;
        std::stringstream ss(raw_data);
        std::string line;
        
        while (std::getline(ss, line)) {
            if (!line.empty()) temp_lines.push_back(line);
        }

        // CRÍTICO: Solo actualizamos si el mensaje parece sustancial
        // Si wttr mandó solo un pedacito, ignoramos la actualización 
        // y mantenemos el caché del frame anterior.
        if (temp_lines.size() > 2) { 
            out = std::move(temp_lines); 
        }
    }
}

void populate_widgets_from_layout(
    const std::string& layout_line, 
    const WallpaperConfig& cfg, 
    Widgets_t& widgets_out,
    char &h_gaps,
    std::list<WidgetElement>& active_widgets_list
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

        if (!token.empty()) {
            // 1. Intentamos encontrar si este elemento ya existe en nuestra lista persistente
            WidgetElement& el = find_or_create_element(token, active_widgets_list);
            el.ttl = 15;
            el.border_color       = cfg.widget_border_color      ;
            el.background_color   = cfg.widget_background_color  ;
            el.background_opacity = cfg.widget_background_opacity;

            // 2. Si el FIFO no está abierto, lo abrimos una sola vez
            if (el.fifo_fd == -1) {
                std::string full_path = (token[0] == '/') ? token : cfg.prefix + "_" + token;
                el.fifo_fd = open(full_path.c_str(), O_RDONLY | O_NONBLOCK);
                std::cout << "Opening '" << full_path << "' with fd " << el.fifo_fd << std::endl;
            }

            // 3. Si ya está abierto (o lo acabamos de abrir), intentamos leer
            if (el.fifo_fd != -1) {
                // Esta función lee el "merequetengue" del clima de Salamanca
                // y actualiza el campo el.widget solo si hay datos nuevos.
                load_widget_from_fifo(el.fifo_fd, el.widget);
            }

            widgets_out.push_back(&el); 
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

void update_widgets_layout(cv::Mat& frame, const WallpaperConfig& config, int& y_cursor, std::list<WidgetElement>& active_widgets_list) {
    std::vector<std::string> layout_lines;
    
    // 1. Leer el archivo de configuración del layout (no los widgets en sí)
    {
        std::ifstream file(config.widgets_file);
        if (!file.is_open()) return;
        std::string l;
        while (std::getline(file, l)) {
            trim_string(l);
            if (!l.empty() && l[0] != '#') layout_lines.push_back(l);
        }
    }

    int line_h = config.widget_font_px; // Un pequeño margen entre líneas

    for (const auto& line : layout_lines) {
        Widgets_t row_widgets; // Vector de punteros WidgetElement*
        char h_gaps = 0;
        
        // Esta función ahora nos devuelve punteros a la lista estática global
        populate_widgets_from_layout(line, config, row_widgets, h_gaps, active_widgets_list);

        if (row_widgets.empty()) continue;

        // 2. Calcular dimensiones de la fila usando los punteros
        int total_row_w = 0;
        int max_row_h = 0;
        int gap = (config.widget_box_sw == -1) ? 20 : (config.widget_box_sw * 10);

        for (auto* el : row_widgets) {
            int max_w = 0;
            for (const auto& txt : el->widget) {
                int bl = 0;
                cv::Size sz = ft2->getTextSize(txt, config.widget_font_px, -1, &bl);
                if (sz.width > max_w) max_w = sz.width;
            }
            el->box_width = max_w + 20;
            el->box_height = (el->widget.size() * line_h) + 10;
            
            if (el->box_height > max_row_h) max_row_h = el->box_height;
            total_row_w += el->box_width;
        }

        // 3. Posicionamiento X (Tu lógica de bitmasking de gaps)
        int start_x = config.rn_width * config.widget_x_prop;
        int current_x = start_x;
        int right_limit = config.rn_width - start_x;
        int active = row_widgets.size();

        switch (h_gaps) {
            case 0b11: current_x = (config.rn_width - (total_row_w + (active - 1) * gap)) / 2; break;
            case 0b10: current_x = right_limit - (total_row_w + (active - 1) * gap); break;
            case 0b00: 
                if (config.widget_box_sw == -1 && active > 1)
                    gap = (right_limit - start_x - total_row_w) / (active - 1);
                break;
        }

        // 4. Aplicar geometría y PINTAR
        for (auto* el : row_widgets) {
            el->box_x = current_x;
            el->box_y = y_cursor;

            // Aquí llamas a una función de dibujo limpia
            draw_single_widget(frame, *el, config);

            current_x += el->box_width + gap;
        }
        y_cursor += max_row_h + (config.widget_box_sh * 10);
    }
    cleanup_inactive_widgets(active_widgets_list);
}

void draw_single_widget(cv::Mat& frame, const WidgetElement& el, const WallpaperConfig& config) {
    cv::Rect roi(el.box_x, el.box_y, el.box_width, el.box_height);
    roi &= cv::Rect(0, 0, frame.cols, frame.rows);

    if (roi.width <= 5 || roi.height <= 5) return;

    // 1. EXTRAER COPIA: No trabajes directamente sobre 'frame' todavía
    cv::Mat roi_src = frame(roi).clone(); 
    cv::Mat background = cv::Mat::zeros(roi_src.size(), roi_src.type());

    // 2. MEZCLA LIMPIA: Mezclamos el negro con la copia de la imagen
    cv::Mat blended;
    cv::addWeighted(background, el.background_opacity, roi_src, 1.0f - el.background_opacity, 0, blended);

    // 3. COPIAR DE VUELTA: Ahora sí, pegamos el resultado en el frame original
    blended.copyTo(frame(roi));
    
    // Borde
    cv::rectangle(frame, roi, el.border_color, 1);

    // Texto: Usar un ajuste de línea más preciso
    int line_h = config.widget_font_px;
    int ty = el.box_y + config.widget_font_px + 5; 

    for (const auto& txt : el.widget) {
        if (txt.empty()) { ty += line_h; continue; }
        ft2->putText(frame, txt, cv::Point(el.box_x + 10, ty), 
                     config.widget_font_px, cv::Scalar(255,255,255), -1, cv::LINE_AA, true);
        ty += line_h;
    }
}

void cleanup_inactive_widgets(std::list<WidgetElement>& active_widgets_list) {
    // Usamos un iterador para poder borrar elementos de la lista estática de forma segura
    for (auto it = active_widgets_list.begin(); it != active_widgets_list.end(); ) {
        it->ttl--; // Decrementar vida en cada frame

        if (it->ttl <= 0) {
            // 1. Cerrar el descriptor de archivo para no dejar fugas (leaks)
            if (it->fifo_fd != -1) {
                close(it->fifo_fd);
                it->fifo_fd = -1;
            }
            // 2. Eliminar de la lista persistente
            it = active_widgets_list.erase(it);
        } else {
            ++it;
        }
    }
}

void render_widget_from_cmd(cv::Mat& frame, const WallpaperConfig& config, int& y_cursor) {
    std::string text = fetch_command_output(config.widget_cmd);
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

void draw_system_widget(cv::Mat& frame, const WallpaperConfig& config, std::list<WidgetElement>& active_widgets_list) {
    if (ft2.empty()) {
        ft2 = cv::freetype::createFreeType2();
        ft2->loadFontData(resolve_font_name(config.widget_font), 0);
    }

    int y_cursor = config.rn_height * config.widget_y_prop;

    if (config.widget_cmd == ".") update_widgets_layout(frame, config, y_cursor, active_widgets_list);
    else render_widget_from_cmd(frame, config, y_cursor);
}

