#include <vector>
#include <string>
#include <xcb/xcb.h>
#include <opencv2/opencv.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <getopt.h>
#include "LovdogLiveWallpaper.hxx"
#include <opencv2/opencv.hpp>

extern const char* PID_FILE;

extern bool keep_running;

extern RuntimeOptions options;

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    WallpaperConfig config;
    // 1. Parsear argumentos
    parse_args(argc, argv, config);

    // Limpieza de instancia previa
    if (options.stop_previous) handle_stop_previous();
    cv::VideoCapture cap;
    if (!prepare_capture(config, cap)) {
        std::cerr << "Error en la rutina de adquisición." << std::endl;
        return -1;
    }

    // Registrar el proceso actual
    register_current_pid();
    
    // Configurar manejo de señales (SIGTERM es la que envía handle_stop_previous)
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    // 3. Setup XCB
    xcb_connection_t* conn = xcb_connect(NULL, NULL);
    const xcb_setup_t* setup = xcb_get_setup(conn);
    xcb_screen_t* screen = xcb_setup_roots_iterator(setup).data;

    // Pixmap y GC
    xcb_pixmap_t pmap = xcb_generate_id(conn);
    xcb_create_pixmap(conn, screen->root_depth, pmap, screen->root, 
                      screen->width_in_pixels, screen->height_in_pixels);
    xcb_gcontext_t gc = xcb_generate_id(conn);
    xcb_create_gc(conn, gc, pmap, 0, NULL);

    cv::Mat frame, bgra_frame;
    
    // Bucle principal controlado por la señal
    while (keep_running) {
        cap >> frame;
        if (frame.empty()) {
            cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            continue;
        }

        cv::resize(frame, frame, cv::Size(screen->width_in_pixels, screen->height_in_pixels));
        cv::cvtColor(frame, bgra_frame, cv::COLOR_BGR2BGRA);

        xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pmap, gc,
                      screen->width_in_pixels, screen->height_in_pixels,
                      0, 0, 0, screen->root_depth,
                      bgra_frame.total() * bgra_frame.elemSize(), bgra_frame.data);

        update_root_atoms(conn, screen->root, pmap);

        // Usar el delay del descriptor si existe
        usleep(config.delay_ms * 1000); 
    }

    // Limpieza al salir
    std::cout << "Limpiando recursos..." << std::endl;
    unlink(PID_FILE); // Borrar el archivo PID
    xcb_free_pixmap(conn, pmap);
    xcb_free_gc(conn, gc);
    xcb_disconnect(conn);
    
    return 0;
}
