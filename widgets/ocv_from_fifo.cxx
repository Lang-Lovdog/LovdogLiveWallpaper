#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/freetype.hpp>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include "../LovdogLiveWallpaper.hxx" // Contiene TermColor y TextSegment 

// Instancia global para las funciones de color
RuntimeOptions options; 

int main(int argc, char** argv) {
    std::string fifo_path;
    
    // 1. Manejo de argumentos básico
    if (argc < 3 || std::string(argv[1]) != "-f") {
        std::cerr << "Uso: " << argv[0] << " -f /tmp/khal_fifo" << std::endl;
        return -1;
    }
    fifo_path = argv[2];

    // 2. Configuración de opciones y recursos
    options.term_r = cv::Scalar(0, 0, 180); 
    options.term_R = cv::Scalar(0, 0, 255);
    options.term_w = cv::Scalar(220, 220, 220);
    
    auto ft2 = cv::freetype::createFreeType2();
    ft2->loadFontData(resolve_font_name("VictorMono Nerd Font"), 0);

    // 3. Abrir FIFO y leer datos
    int fd = open(fifo_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        perror("Error al abrir FIFO");
        return -1;
    }

    widget_text_color current_widget;
    cv::Mat canvas = cv::Mat::zeros(800, 1000, CV_8UC3);
    
    std::cout << "Esperando datos en " << fifo_path << "..." << std::endl;

    while (true) {
        // Intentar cargar datos del FIFO con la nueva lógica de segmentos
        load_widget_from_fifo(fd, current_widget);

        if (!current_widget.empty()) {
            canvas.setTo(cv::Scalar(0, 0, 0)); // Limpiar
            int ty = 40;
            int font_px = 20;

            for (const auto& line : current_widget) {
                int tx = 20;
                for (const auto& segment : line) {
                    cv::Scalar fg_color = term_to_scalar(segment.attributes);
                    
                    // 1. Calcular dimensiones del segmento
                    int bl = 0;
                    cv::Size sz = ft2->getTextSize(segment.text, font_px, -1, &bl);

                    // 2. Lógica de INVERT
                    if (segment.attributes & TermColor::INVERT) {
                        // Dibujamos el "bloque" de fondo. 
                        // Usamos el color que sería del texto como fondo.
                        cv::Rect bg_rect(tx, ty - font_px, sz.width, font_px + bl);
                        cv::rectangle(canvas, bg_rect, fg_color, cv::FILLED);
                        
                        // El texto ahora lo pintamos en negro (o color de fondo del widget)
                        // para que contraste sobre el bloque sólido.
                        fg_color = cv::Scalar(0, 0, 0); 
                    }
                    
                    // 3. Dibujar texto (encima del rectángulo si hubo invert)
                    ft2->putText(canvas, segment.text, cv::Point(tx, ty), 
                                 font_px, fg_color, -1, cv::LINE_AA, true);

                    // 4. Avanzar cursor
                    tx += sz.width;
                }
                ty += font_px + 10;
            }
            cv::imshow("FIFO Debugger - The Forest", canvas);
        }

        if (cv::waitKey(100) == 27) break; // ESC para salir
    }

    close(fd);
    return 0;
}
