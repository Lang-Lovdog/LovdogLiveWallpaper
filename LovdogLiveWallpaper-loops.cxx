#include "LovdogLiveWallpaper.hxx"

extern bool keep_running;

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
