#include <cstdio>
#include <memory>
#include <string>
#include <array>
#include <opencv2/freetype.hpp>
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

static cv::Ptr<cv::freetype::FreeType2> ft2;

void draw_system_widget(cv::Mat& frame, const WallpaperConfig& config, const std::string& text) {
    if (text.empty()) return;

    std::stringstream ss(text);
    std::string line;
    int y_cursor = config.rn_height*config.widget_y_prop;
    int widget_x = config.rn_width *config.widget_x_prop;
    cv::Scalar avgColor = cv::mean(frame);
// Invertir el color promedio para el texto
    cv::Scalar font_color;
    cv::Scalar font_border;

    if (ft2.empty()) {
        ft2 = cv::freetype::createFreeType2();
        ft2->loadFontData(resolve_font_name(config.widget_font), 0);
    }

    font_color=get_adaptive_color(avgColor, true);
    font_border=get_adaptive_color(avgColor, false);
    font_color[3] = 255;  // Opaco
    font_border[3] = 255; // Opaco

    while (std::getline(ss, line)) {
        // --- EFECTO DE BORDE (OUTLINE) ---
        // Dibujamos en los 8 desplazamientos para un borde sólido y legible
        for(int dx = -1; dx <= 1; dx++) {
            for(int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                ft2->putText(frame, line, cv::Point(widget_x + dx, y_cursor + dy), 
                             config.widget_font_px, font_border, -1, cv::LINE_AA, true);
            }
        }

        // --- TEXTO PRINCIPAL ---
        ft2->putText(frame, line, cv::Point(widget_x, y_cursor), 
                     config.widget_font_px, font_color, -1, cv::LINE_AA, true);
        
        y_cursor += config.widget_font_px + 10; // Espaciado vertical
    }

    //while (std::getline(ss, line)) {
    //    for(int dx=-1; dx<=1; dx++)
    //        for(int dy=-1; dy<=1; dy++)
    //            cv::putText(frame, line, cv::Point(widget_x+dx, y_cursor+dy), font, font_scale, font_border, cv::LINE_AA);
    //    // 1. Sombreado para legibilidad (Drop shadow)
    //    cv::putText(frame, line, cv::Point(widget_x + 1, y_cursor + 1), 
    //                cv::FONT_HERSHEY_SIMPLEX, font_scale, font_color, 1, cv::LINE_AA);
    //    
    //    // 2. Texto principal (usando el color de cava para consistencia estética)
    //    cv::putText(frame, line, cv::Point(widget_x, y_cursor), 
    //                cv::FONT_HERSHEY_SIMPLEX, font_scale, font_color, 1, cv::LINE_AA);
    //    
    //    y_cursor += config.widget_font_px + 10; // Espaciado entre líneas
    //}
}

