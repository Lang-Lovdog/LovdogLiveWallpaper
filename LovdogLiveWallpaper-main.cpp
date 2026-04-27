#include <xcb/xcb.h>
#include <opencv2/opencv.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <getopt.h>
#include "LovdogLiveWallpaper.hxx"
#include <opencv2/opencv.hpp>

extern const char* PID_FILE;
extern RuntimeOptions* g_options;

int main(int argc, char** argv) {
    WallpaperConfig config;
    RuntimeOptions options;
    g_options = &options;
    // 1. Parsear argumentos
    options.read_widget_config = true;
    check_and_reload_configs(config, options);

    parse_args(argc, argv, config, options);

    if (options.reload_widgets) {
        std::ifstream infile(PID_FILE);
        pid_t pid;
        if (infile >> pid) {
            kill(pid, SIGUSR1);
            std::cout << "Señal de recarga enviada al proceso " << pid << std::endl;
        } else {
            std::cerr << "No hay una instancia de LLW ejecutándose." << std::endl;
        }
        exit(0); // Terminamos este proceso "mensajero"
    }

    // Limpieza de instancia previa
    if (options.stop_previous) handle_stop_previous();
    cv::VideoCapture cap;
    slideshow_paths slideshow_list;
    if (!prepare_capture(config, options, cap, slideshow_list)) {
        std::cerr << "Error en la rutina de adquisición." << std::endl;
        return -1;
    }

    // Registrar el proceso actual
    register_current_pid();
    
    // Configurar manejo de señales (SIGTERM es la que envía handle_stop_previous)
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGUSR1, signal_handler);

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

    adjust_render_dims(config,screen);
    std::cout << "Iniciando con "<< config.rn_width << " x " << config.rn_height << " pixeles" << std::endl;
    start_loop(config, options, slideshow_list, cap, screen, conn, gc, pmap);

    // Limpieza al salir
    std::cout << "Limpiando recursos..." << std::endl;
    unlink(PID_FILE); // Borrar el archivo PID
    xcb_free_pixmap(conn, pmap);
    xcb_free_gc(conn, gc);
    xcb_disconnect(conn);
    
    return 0;
}
