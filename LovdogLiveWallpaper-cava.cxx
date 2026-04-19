#include "LovdogLiveWallpaper.hxx"


// En LovdogLiveWallpaper-frame-preprocess.cxx o un nuevo LovdogLiveWallpaper-audio.cxx

extern const std::string cava_file;
extern RuntimeOptions options;

cv::Mat get_cava_bars(int width, int height, const WallpaperConfig& config, int num_bars, int fifo_fd=-1) {
    static std::vector<uint8_t> heights(num_bars, 0);

    // 1. Abrir el pipe si es la primera vez (Configurado en /tmp/cava_fifo)
    if (fifo_fd == -1) {
        fifo_fd = open(cava_file.c_str(), O_RDONLY | O_NONBLOCK);
    }

    // 2. Leer datos frescos de CAVA
    if (fifo_fd != -1) {
        uint8_t temp_buffer[num_bars];
        bool data_found = false;
        
        // Leemos en un bucle hasta que el pipe esté vacío
        // Esto descarta frames viejos y nos deja con el más reciente
        while (read(fifo_fd, temp_buffer, num_bars) == num_bars) {
            data_found = true;
        }

        if (data_found) {
            for (int i = 0; i < num_bars; i++) heights[i] = temp_buffer[i];
        } else {
            // Gravedad suave si no hubo datos nuevos
            for (auto& h : heights) if (h > 0) h -= 2;
        }
    }
    // 3. Dibujar en la Matriz
    cv::Mat bars_mat = cv::Mat::zeros(cv::Size(width, height), CV_8UC3);
    int bar_w = width / num_bars;
    if(options.use_cava_range){
        for (int i = 0; i < num_bars; i++) {
            int val = (heights[i] * height) / 255;
            
            // Calculamos el factor (0.0 a 1.0) según la posición de la barra
            float t = static_cast<float>(i) / (num_bars - 1);
            
            // Interpolación de B, G y R
            cv::Scalar bar_color(
                config.cava_rgb_min[0] + t * (config.cava_rgb_max[0] - config.cava_rgb_min[0]),
                config.cava_rgb_min[1] + t * (config.cava_rgb_max[1] - config.cava_rgb_min[1]),
                config.cava_rgb_min[2] + t * (config.cava_rgb_max[2] - config.cava_rgb_min[2])
            );

            cv::rectangle(bars_mat, 
                          cv::Point(i * bar_w, height), 
                          cv::Point((i + 1) * bar_w - 2, height - val), 
                          bar_color, -1);
        }
    }else{
        cv::Scalar current_color = config.cava_color;
        if (config.cava_random_color) {
            // Generar un color aleatorio vibrante si el flag está activo
            current_color = cv::Scalar(rand()%256, rand()%256, rand()%256);
        }
        for (int i = 0; i < num_bars; i++) {
            // Mapear el valor de CAVA (0-255) al alto de la pantalla
            int val = (heights[i] * height) / 255;
            // Aleatorizar colores
            
            // Dibujar barra (puedes usar Scalar(255,255,255) o colores Lovdog)
            cv::rectangle(bars_mat, 
                          cv::Point(i * bar_w, height), 
                          cv::Point((i + 1) * bar_w - 2, height - val), 
                          current_color, // Color de ejemplo
                          -1);
        }
    }

    return bars_mat;
}
