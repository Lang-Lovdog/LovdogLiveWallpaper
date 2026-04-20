#include "LovdogLiveWallpaper.hxx"
#include <ctime>

extern bool keep_running;
extern const std::string cava_file;
extern RuntimeOptions options;

void clean_screen(
    WallpaperConfig &config,
    xcb_screen_t    *screen,
    xcb_connection_t *conn,
    xcb_gcontext_t  &gc,
    xcb_pixmap_t    &pmap
) {
    // 1. Crear matriz negra (3 canales BGR)
    cv::Mat black = cv::Mat::zeros(config.rn_height, config.rn_width, CV_8UC3);
    
    // 2. Convertir a BGRA (4 canales)
    cv::Mat black_bgra;
    cv::cvtColor(black, black_bgra, cv::COLOR_BGR2BGRA);

    // 3. Empujar la imagen al Pixmap de X11
    xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pmap, gc,
                  config.rn_width, config.rn_height,
                  config.x_start, config.y_start, 0, screen->root_depth,
                  black_bgra.total() * black_bgra.elemSize(), black_bgra.data);

    // 4. Notificar al sistema y forzar el dibujado
    update_root_atoms(conn, screen->root, pmap);
    xcb_flush(conn);
}

void loop_normal(
        WallpaperConfig  &config,
        cv::VideoCapture &cap,
        xcb_screen_t     *screen,
        xcb_connection_t *conn,
        xcb_gcontext_t   &gc,
        xcb_pixmap_t     &pmap
){
    cv::Mat frame, bgra_frame;
    std::string current_widget_text = "";
    long long frame_count = 0;

    // Bucle principal controlado por la señal
    while (keep_running) {
        auto start_time = std::chrono::steady_clock::now();
        cap >> frame;
        if (frame.empty()) {
            cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            continue;
        }

        cv::resize(frame, frame, cv::Size(config.rn_width, config.rn_height));
        if (options.enable_widgets && !config.widget_cmd.empty()) {
            // Actualizar cada N frames según tu config
            if (frame_count % config.widget_delay == 0) {
                current_widget_text = fetch_command_output(config.widget_cmd);
                //std::cout << current_widget_text << std::endl;
            }
            // Dibujar
            draw_system_widget(frame, config, current_widget_text);
        }

        cv::cvtColor(frame, bgra_frame, cv::COLOR_BGR2BGRA);
        xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pmap, gc,
                      config.rn_width, config.rn_height,
                      config.x_start, config.y_start, 0, screen->root_depth,
                      bgra_frame.total() * bgra_frame.elemSize(), bgra_frame.data);

        update_root_atoms(conn, screen->root, pmap);

        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        int sleep_time = config.delay_ms - (int)elapsed;

        // Usar el delay del descriptor si existe
        if (sleep_time > 0) {
            usleep(sleep_time * 1000);
            frame_count+=sleep_time;
        } else{
            frame_count+=elapsed;
        }
    }
    std::cout << "\nLimpiando pantalla..." << std::endl;
    clean_screen(config, screen, conn, gc, pmap);
}

void loop_normal_cava(
        WallpaperConfig  &config,
        cv::VideoCapture &cap,
        xcb_screen_t     *screen,
        xcb_connection_t *conn,
        xcb_gcontext_t   &gc,
        xcb_pixmap_t     &pmap
){
    cv::Mat frame, work_frame, barframe, bgra_frame, bars;

    int audio_fd = open(cava_file.c_str(), O_RDONLY | O_NONBLOCK);
    int cava_delay_ms = 1000 / config.cava_fps;
    int ms_acumulados_anim = 0;
    int ms_acumulados_widg = 0;
    int bars_h = config.rn_height * config.cava_bars_height;
    cv::Rect roi_cava(0, config.rn_height - bars_h, config.rn_width, bars_h);
    std::string current_widget_text = "";
    int ms_por_frame = config.delay_ms-5;
    ms_acumulados_anim=ms_por_frame;
    ms_acumulados_widg=config.widget_delay;

    barframe = cv::Mat::zeros(cv::Size(config.rn_width, config.rn_height), CV_8UC4);
    // Bucle principal controlado por la señal
    while (keep_running) {
        auto start_time = std::chrono::steady_clock::now();
        if (ms_acumulados_anim >= ms_por_frame) {
            cap >> frame;
            if (frame.empty()) { 
                cap.set(cv::CAP_PROP_POS_FRAMES, 0); 
                cap >> frame; 
            }
            cv::resize(frame, frame, cv::Size(config.rn_width, config.rn_height));
            ms_acumulados_anim = 0; // Reset del acumulador
        }
        
        // 1. INICIALIZAR MEMORIA (Lo que ya tenías)
        if (work_frame.size() != frame.size() || work_frame.type() != frame.type()) {
            work_frame = cv::Mat(frame.size(), frame.type());
        }

        // 2. COPIAR PIXELES (¡ESTO FALTABA!)
        // Sin esto, work_frame está lleno de basura o ceros (negro)
        frame.copyTo(work_frame);

        if (options.enable_cava) {
            roi_cava &= cv::Rect(0, 0, work_frame.cols, work_frame.rows);
            if (roi_cava.width > 0 && roi_cava.height > 0) {
                get_cava_bars(work_frame, roi_cava, config, config.cava_num_bars, audio_fd);
            }
        }

        if (options.enable_widgets && ms_acumulados_widg >= config.widget_delay) {
            draw_system_widget(work_frame, config, current_widget_text);
        }

        // 3. CONVERTIR A BGRA PARA X11 (¡ESTO TAMBIÉN FALTABA!)
        // Si no haces el cvtColor, bgra_frame se queda con lo que tenía o vacío
        bgra_frame = cv::Mat(work_frame.size(), CV_8UC4);
        cv::cvtColor(work_frame, bgra_frame, cv::COLOR_BGR2BGRA);

        // 4. ENVIAR A X11
        xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pmap, gc,
                      config.rn_width, config.rn_height,
                      config.x_start, config.y_start, 0, screen->root_depth,
                      bgra_frame.total() * bgra_frame.elemSize(), bgra_frame.data);

        update_root_atoms(conn, screen->root, pmap);
        xcb_flush(conn);

        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        int sleep_time = cava_delay_ms - (int)elapsed;
        if (sleep_time > 0) {
            usleep(sleep_time * 1000);
            ms_acumulados_anim += cava_delay_ms; // Sumamos el paso fijo de CAVA
        } else{
            ms_acumulados_anim += elapsed;
            ms_acumulados_widg += elapsed;
        }// Si el sistema es lento, sumamos lo que tardó
    }
    if(audio_fd != -1) close(audio_fd);
    std::cout << "\nLimpiando pantalla..." << std::endl;
    clean_screen(config, screen, conn, gc, pmap);
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
    std::cout << "\nLimpiando pantalla..." << std::endl;
    clean_screen(config, screen, conn, gc, pmap);
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
    clean_screen(config, screen, conn, gc, pmap);
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

