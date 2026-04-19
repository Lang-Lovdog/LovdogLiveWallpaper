#include "LovdogLiveWallpaper.hxx"
#include <algorithm>
#include <random>
#include <ctime>

extern bool keep_running;
extern const std::string cava_file;
extern RuntimeOptions options;

void loop_normal(
        WallpaperConfig  &config,
        cv::VideoCapture &cap,
        xcb_screen_t     *screen,
        xcb_connection_t *conn,
        xcb_gcontext_t   &gc,
        xcb_pixmap_t     &pmap
){
    cv::Mat frame, bgra_frame;

    // Bucle principal controlado por la señal
    while (keep_running) {
        cap >> frame;
        if (frame.empty()) {
            cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            continue;
        }

        cv::resize(frame, frame, cv::Size(config.rn_width, config.rn_height));
        cv::cvtColor(frame, bgra_frame, cv::COLOR_BGR2BGRA);

        xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pmap, gc,
                      config.rn_width, config.rn_height,
                      config.x_start, config.y_start, 0, screen->root_depth,
                      bgra_frame.total() * bgra_frame.elemSize(), bgra_frame.data);

        update_root_atoms(conn, screen->root, pmap);

        // Usar el delay del descriptor si existe
        usleep(config.delay_ms * 1000); 
    }
}

void loop_normal_cava(
        WallpaperConfig  &config,
        cv::VideoCapture &cap,
        xcb_screen_t     *screen,
        xcb_connection_t *conn,
        xcb_gcontext_t   &gc,
        xcb_pixmap_t     &pmap
){
    cv::Mat frame, barframe, bgra_frame, bars;

    int audio_fd = open(cava_file.c_str(), O_RDONLY | O_NONBLOCK);
    int cava_delay_ms = 1000 / config.cava_fps;
    int ms_acumulados = 0;
    int bars_h = config.rn_height * config.cava_bars_height;
    cv::Rect roi(0, config.rn_height - bars_h, config.rn_width, bars_h);

    // Bucle principal controlado por la señal
    while (keep_running) {
        if (ms_acumulados >= config.delay_ms || frame.empty()) {
            cap >> frame;
            if (frame.empty()) {
                cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                cap >> frame;
            }
            cv::resize(frame, frame, cv::Size(config.rn_width, config.rn_height));
            ms_acumulados = 0;
            // 2. Procesamiento de imagen
            cv::resize(frame, frame, cv::Size(config.rn_width, config.rn_height));
        }

        if (roi.width > 0 && roi.height > 0) {
            bars = get_cava_bars(roi.width, roi.height, config, config.cava_num_bars, audio_fd);
        }

        // 3. Mezcla sin IFs: Usamos ROI y suma de matrices
        // Esto asume que bars es del mismo tipo que frame
        frame.copyTo(barframe);
        cv::add(barframe(roi), bars, barframe(roi));

        cv::cvtColor(barframe, bgra_frame, cv::COLOR_BGR2BGRA);

        xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pmap, gc,
                      config.rn_width, config.rn_height,
                      config.x_start, config.y_start, 0, screen->root_depth,
                      bgra_frame.total() * bgra_frame.elemSize(), bgra_frame.data);

        update_root_atoms(conn, screen->root, pmap);

        // Usar el delay del descriptor si existe
//        usleep(config.delay_ms * 1000); 
        usleep(cava_delay_ms * 1000);
        ms_acumulados += cava_delay_ms;
    }
    if(audio_fd != -1) close(audio_fd);
}

void loop_slideshow(
        WallpaperConfig& config,
        slideshow_paths& slideshow_list,
        xcb_screen_t* screen,
        xcb_connection_t* conn,
        xcb_gcontext_t& gc,
        xcb_pixmap_t& pmap
){
    // Inicialización del generador de números aleatorios
    std::srand(std::time(0));

    // Imágenes
    std::string random_image;
    float fade_step=0.1f;
    fade_step=10/(float)config.transition_delay;
    while (keep_running) {
        random_image = slideshow_list[rand() % slideshow_list.size()];
        cv::Mat raw_frame = cv::imread(random_image);
        // 1. Crear el lienzo negro del tamaño de la pantalla (config.rn_width/height)
        cv::Mat canvas = cv::Mat::zeros(cv::Size(config.rn_width, config.rn_height), raw_frame.type());

        if (!raw_frame.empty()) {

            // 2. Redimensionar la imagen original manteniendo el ratio
            // Usamos una copia para no alterar el lienzo directamente aún
            cv::Mat resized_img = raw_frame.clone();
            intelligent_image_resize_keep_ratio(resized_img, config.rn_width, config.rn_height);

            // 3. Calcular coordenadas para centrar resized_img en el canvas
            int x_offset = (canvas.cols - resized_img.cols) / 2;
            int y_offset = (canvas.rows - resized_img.rows) / 2;

            // 4. "Pegar" la imagen redimensionada sobre el lienzo negro
            // Definimos el ROI (Region of Interest) en el canvas
            resized_img.copyTo(canvas(cv::Rect(x_offset, y_offset, resized_img.cols, resized_img.rows)));

            // --- Inicio de Transición de Brillo (Fade-In) ---
            for (float brillo = 0.0f; brillo <= 1.0f && keep_running; brillo += fade_step) {
                cv::Mat temp_draw = canvas * brillo; // Operación en el lienzo completo
                cv::Mat bgra_frame;
                cv::cvtColor(temp_draw, bgra_frame, cv::COLOR_BGR2BGRA);

                xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pmap, gc,
                              config.rn_width, config.rn_height,
                              config.x_start, config.y_start, 0, screen->root_depth,
                              bgra_frame.total() * bgra_frame.elemSize(), bgra_frame.data);

                update_root_atoms(conn, screen->root, pmap);
                xcb_flush(conn);
                usleep(config.transition_delay*100); 
            }
        }

        for(int i = 0; i < (config.delay_ms / 100) && keep_running; ++i) {
            usleep(100000); 
        }

        // --- Inicio de Transición de Brillo (Fade-Out) ---
        for (float brillo = 1.0f; brillo >= 0.0f && keep_running; brillo -= fade_step) {
            cv::Mat temp_draw = canvas * brillo; // Operación en el lienzo completo
            cv::Mat bgra_frame;
            cv::cvtColor(temp_draw, bgra_frame, cv::COLOR_BGR2BGRA);

            xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pmap, gc,
                          config.rn_width, config.rn_height,
                          config.x_start, config.y_start, 0, screen->root_depth,
                          bgra_frame.total() * bgra_frame.elemSize(), bgra_frame.data);

            update_root_atoms(conn, screen->root, pmap);
            xcb_flush(conn);
            usleep(config.transition_delay*100); 
        }
    }
}

void start_loop(
        WallpaperConfig  &config,
        slideshow_paths  &slideshow_list,
        cv::VideoCapture &cap,
        xcb_screen_t     *screen,
        xcb_connection_t *conn,
        xcb_gcontext_t   &gc,
        xcb_pixmap_t     &pmap
){
    std::cout << "Loop Selector ";
    switch(config.in_type){
        case TYPE_VIDEO:
        case TYPE_GIF:
        case TYPE_DESCRIPTOR:
            if(options.enable_cava){
                std::cout << "Animated Start With CAVA integration\n"
                          << config.cava_num_bars << "bars || " << config.cava_fps << "of framerate";
                loop_normal_cava(config, cap, screen, conn, gc, pmap);
            }else{
                std::cout << "Animated Start";
                loop_normal(config, cap, screen, conn, gc, pmap);
            }
            break;
        case TYPE_DIR_SLIDE:
            std::cout << "Slideshow List Start";
            loop_slideshow(config, slideshow_list, screen, conn, gc, pmap);
            break;
    }
}

