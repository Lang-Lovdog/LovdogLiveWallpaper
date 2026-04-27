#include "LovdogLiveWallpaper.hxx"


// En LovdogLiveWallpaper-frame-preprocess.cxx o un nuevo LovdogLiveWallpaper-audio.cxx

extern const std::string cava_file;
//extern RuntimeOptions options;

void draw_adaptive_gradient_bar(cv::Mat& frame, int x, int y, int w, int h, cv::Scalar avg) {
    if (h <= 0 || w <= 0) return;

    static cv::Scalar last_avg(-1, -1, -1);
    static cv::Mat gradient_lut(256, 1, CV_8UC4);
    static int update_counter = 0;

    if (update_counter++ % 30 == 0) {
        // Usamos una distancia euclidiana un poco más estricta para negros
        double dist = std::abs(avg[0]-last_avg[0]) + std::abs(avg[1]-last_avg[1]) + std::abs(avg[2]-last_avg[2]);

        if (dist > 5.0) { // Umbral más sensible para cambios en sombras
            // 1. APLICAMOS UN OFFSET (Bias)
            // Evitamos que el color base sea (0,0,0) absoluto. 
            // Un pequeño valor de 5-10 elimina el ruido de cuantización.
            //cv::Scalar bottom = avg * 0.1 + cv::Scalar(5, 5, 5); 

            cv::Scalar shifted_color(avg[2], avg[0], avg[1]); 
            
            // 2. LÓGICA ADAPTATIVA REFORZADA
            //cv::Scalar top_raw = get_adaptive_color(avg, true);
            //cv::Scalar top = top_raw * 0.7;
            cv::Scalar top    = shifted_color * 1.2; // Saturamos un poco
            cv::Scalar bottom = shifted_color * 0.5;

            cv::Scalar vibrant(avg[1], avg[2], avg[0]); // Rotación simple GBR

            //for (int i = 0; i < 256; i++) {
            //    float alpha = i / 255.0f;
            //    for(int j=0; j<3; j++) {
            //        // Forzamos que el color 'top' sea más saturado
            //        float val = (vibrant[j] * alpha * 1.5f) + (avg[j] * 0.1f);
            //        gradient_lut.at<cv::Vec4b>(i, 0)[j] = cv::saturate_cast<uchar>(val);
            //    }
            //}
            for (int i = 0; i < 256; i++) {
                float alpha = i / 255.0f;
                cv::Vec4b color;
                for(int j=0; j<3; j++) {
                    // Interpolación con clamp explícito para evitar artefactos
                    float val = bottom[j] * (1.0f - alpha) + top[j] * alpha;
                    color[j] = cv::saturate_cast<uchar>(std::max(0.0f, std::min(255.0f, val)));
                }
                gradient_lut.at<cv::Vec4b>(i, 0) = color;
            }
            last_avg = avg;
        }
    }

    cv::Rect bar_rect(x, y, w, h);
    if ((bar_rect & cv::Rect(0, 0, frame.cols, frame.rows)) == bar_rect) {
        cv::Mat bar_roi = frame(bar_rect);
        // Usamos INTER_AREA si la barra es más pequeña que la LUT, 
        // o INTER_LINEAR si es más grande. Para CAVA, AREA suele ser más limpio.
        cv::resize(gradient_lut, bar_roi, bar_roi.size(), 0, 0, cv::INTER_AREA);
        //cv::bitwise_not(bar_roi, bar_roi);
        bar_roi += cv::Scalar(0,0,0);
    }
}

void draw_bar_gradient(cv::Mat& bars_mat, const WallpaperConfig& config, int num_bars, int height, std::vector<uint8_t>heights, int bar_w) {
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
}

void draw_bar(cv::Mat& bars_mat, const WallpaperConfig& config, int num_bars, int height, std::vector<uint8_t>heights, int bar_w) {
        cv::Scalar current_color;
        if (config.cava_random_color) {
            // Generar un color aleatorio vibrante si el flag está activo
            current_color = cv::Scalar(rand()%256, rand()%256, rand()%256, 255);
        }
        for (int i = 0; i < num_bars; i++) {
            // Mapear el valor de CAVA (0-255) al alto de la pantalla
            int val = (heights[i] * height) / 255;
            current_color = config.cava_color;
            current_color[3]=200;
            // Aleatorizar colores
            
            // Dibujar barra (puedes usar Scalar(255,255,255) o colores Lovdog)
            cv::rectangle(bars_mat, 
                          cv::Point(i * bar_w, height), 
                          cv::Point((i + 1) * bar_w - 2, height - val), 
                          current_color, // Color de ejemplo
                          -1);
        }
}

void get_cava_bars(cv::Mat& barframe, cv::Rect& roi_cava, const WallpaperConfig& config, const RuntimeOptions &options, int num_bars, int fifo_fd) {
    // 1. Validar num_bars para evitar el warning y posibles crashes
    if (num_bars <= 0) return;
    size_t n_bytes = static_cast<size_t>(num_bars);

    static std::vector<uint8_t> heights;
    if (heights.size() != n_bytes) heights.resize(n_bytes, 0);

    // 2. Muestreo de color
    cv::Mat small;
    cv::resize(barframe, small, cv::Size(32, 32), 0, 0, cv::INTER_NEAREST);
    cv::Scalar avgColor = cv::mean(small);

    // 3. Leer datos frescos de CAVA (Vaciando el pipe)
    if (fifo_fd != -1) {
        // Usamos un vector local como buffer temporal para evitar VLA (Variable Length Arrays)
        std::vector<uint8_t> temp_buffer(n_bytes);
        bool data_found = false;

        // Leemos TODO lo que haya en el pipe. 
        // Solo nos quedamos con el último paquete (el más reciente).
        while (read(fifo_fd, temp_buffer.data(), n_bytes) == (ssize_t)n_bytes) {
            data_found = true;
            // Copia eficiente del buffer a heights
            std::copy(temp_buffer.begin(), temp_buffer.end(), heights.begin());
        }

        if (!data_found) {
            // Gravedad suave si el pipe está vacío
            for (auto& h : heights) if (h > 0) h = (h > 2) ? h - 2 : 0;
        }
    }

    // 4. Dibujo (Aseguramos 4 canales para concordar con BGRA)
    cv::Mat bars_mat = cv::Mat::zeros(roi_cava.size(), CV_8UC3);
    int bar_w = roi_cava.width / num_bars;

    if (options.use_cava_adaptive) {
        for (int i = 0; i < num_bars; i++) {
            int val = (heights[i] * roi_cava.height) / 255;
            draw_adaptive_gradient_bar(bars_mat, i * bar_w, roi_cava.height - val, bar_w - 2, val, avgColor);
            cv::addWeighted(barframe(roi_cava), 1.0, bars_mat, 0.8, 0.0, barframe(roi_cava));
        }
    } else if (options.use_cava_range) {
        draw_bar_gradient(bars_mat, config, num_bars, roi_cava.height, heights, bar_w);
        // 5. Mezcla final
        cv::add(barframe(roi_cava), bars_mat, barframe(roi_cava));
    } else {
        draw_bar(bars_mat, config, num_bars, roi_cava.height, heights, bar_w); 
        // 5. Mezcla final
        cv::add(barframe(roi_cava), bars_mat, barframe(roi_cava));
    }

}

