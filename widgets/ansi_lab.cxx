#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/freetype.hpp>
#include <iostream>
#include <sstream>
#include <vector>

// Heredamos tus estructuras para asegurar compatibilidad
#include "../LovdogLiveWallpaper.hxx" 

RuntimeOptions options;

enum class TokenType { TEXT, ATTR_CHANGE };

struct PenState {
    cv::Scalar color;
    bool bold;
    bool reverse_mode;

    // Inicialización con el tema por defecto de las opciones
    PenState() : color(options.term_w), bold(false) {}

    void reset() {
        color = options.term_w;
        bold = false;
    }
};

struct Token {
    TokenType type;
    std::string content; // El texto plano O la secuencia numérica (ej: "1;31")
};

std::vector<Token> tokenize_ansi(const std::string& line) {
    std::vector<Token> tokens;
    size_t pos = 0;

    while (pos < line.size()) {
        size_t esc_pos = line.find("\033[", pos);

        // 1. Guardar el texto plano antes del escape
        if (esc_pos > pos) {
            tokens.push_back({TokenType::TEXT, line.substr(pos, esc_pos - pos)});
        }

        if (esc_pos == std::string::npos) break;

        // 2. Buscar el terminador de la secuencia (m para SGR, o cualquier otro)
        size_t end_pos = line.find_first_of("mABCDEFGHJKSTfink", esc_pos + 2);
        
        if (end_pos != std::string::npos) {
            char terminator = line[end_pos];
            // Solo nos interesan los tokens de cambio de atributo ('m')
            if (terminator == 'm') {
                std::string attr_content = line.substr(esc_pos + 2, end_pos - (esc_pos + 2));
                tokens.push_back({TokenType::ATTR_CHANGE, attr_content});
            }
            // Si el terminador no es 'm', simplemente ignoramos la secuencia (limpieza)
            pos = end_pos + 1;
        } else {
            // Si no hay terminador, tratamos el resto como texto para no perder datos
            pos = esc_pos + 1;
        }
    }
    return tokens;
}

void render_tokens(cv::Mat& canvas, const std::vector<Token>& tokens, cv::Ptr<cv::freetype::FreeType2>& ft2, int start_x, int start_y, int font_size) {
    PenState pen;
    int tx = start_x;
    int ty = start_y;

    for (const auto& token : tokens) {
        if (token.type == TokenType::ATTR_CHANGE) {
            // Procesar sub-códigos (ej: "1;31")
            std::stringstream ss(token.content);
            std::string item;
            while (std::getline(ss, item, ';')) {
                if (item.empty()) continue;
                try {
                    int code = std::stoi(item);
                    if (code == 0) pen.reset();
                    else if (code == 1) pen.bold = true;
                    else if ((code > 29 && code < 38) || (code > 89 && code < 98)) {
                        // Dentro del bucle de procesamiento de códigos
                        pen.color = get_ansi_color(code, pen.bold);
                    }
                    if (code == 7) pen.reverse_mode = true;
                    else if (code == 27) pen.reverse_mode = false; // Código para desactivar inverso
                } catch (...) { /* Ignorar secuencias mal formadas */ }
            }
        } 
        else if (token.type == TokenType::TEXT) {
            // Dibujar segmento de texto con el estado actual de la pluma
            ft2->putText(canvas, token.content, cv::Point(tx, ty), 
                         font_size, pen.color, -1, cv::LINE_AA, true);

            // Avanzar el cursor X basado en el tamaño real del texto dibujado
            int baseline = 0;
            cv::Size sz = ft2->getTextSize(token.content, font_size, -1, &baseline);
            tx += sz.width;
        }
    }
}

// Simulamos las opciones que vendrán del motor principal
RuntimeOptions mock_opts; 

void setup_mock_theme() {
    mock_opts.term_r = cv::Scalar(0, 0, 180);   // Rojo bosque
    mock_opts.term_g = cv::Scalar(0, 180, 0);   // Verde musgo
    mock_opts.term_w = cv::Scalar(200, 200, 200); // Blanco base
    // ... llenar el resto de la paleta
}


int main(int argc, char** argv) {
    // 1. Configuración del "Tema" (Colores de RuntimeOptions)
    // Aquí puedes jugar con los valores para probar tus esquemas de colores
    options.term_r = cv::Scalar(0, 0, 150);   // Rojo
    options.term_R = cv::Scalar(0, 0, 255);   // Rojo Brillante
    options.term_g = cv::Scalar(0, 150, 0);   // Verde
    options.term_w = cv::Scalar(200, 200, 200); // Blanco base
    options.term_W = cv::Scalar(255, 255, 255); // Blanco puro

    // 2. Inicializar recursos de renderizado
    auto ft2 = cv::freetype::createFreeType2();
    std::string font_path = resolve_font_name("VictorMono Nerd Font"); // Tu función con fontconfig
    if (font_path.empty()) {
        std::cerr << "Error: No se encontró la fuente Victor Mono." << std::endl;
        return -1;
    }
    ft2->loadFontData(font_path, 0);

    // 3. Captura de datos (Soporta Pipe y Argumentos)
    std::vector<std::string> lines;
    if (argc > 1) {
        lines.push_back(argv[1]); // Uso: ./ansi_lab "texto"
    } else {
        std::string line;
        while (std::getline(std::cin, line)) {
            lines.push_back(line); // Uso: cal --color=always | ./ansi_lab
        }
    }

    // 4. Preparar el lienzo (Canvas)
    // Ajustamos el tamaño según la cantidad de líneas recibidas
    int font_size = 20;
    int line_height = font_size;
    int canvas_h = std::max(400, (int)lines.size() * line_height + 100);
    cv::Mat canvas = cv::Mat::zeros(canvas_h, 1200, CV_8UC3)*255;

    // 5. Procesamiento y Dibujo
    int cur_y = font_size + 20;
    for (const auto& raw_line : lines) {
        // Tokenizamos la línea usando la lógica de seguridad diseñada
        auto tokens = tokenize_ansi(raw_line);
        
        // Renderizamos los tokens en el canvas
        render_tokens(canvas, tokens, ft2, 20, cur_y, font_size);
        
        cur_y += line_height;
    }

    // 6. Despliegue con HighGUI
    if (canvas.empty()) {
        std::cerr << "Error: El canvas está vacío." << std::endl;
        return -1;
    }

    cv::imshow("ANSI Lab - The Forest Engine Debugger", canvas);
    std::cout << "Presiona cualquier tecla en la ventana para salir..." << std::endl;
    cv::waitKey(0);

    return 0;
}
