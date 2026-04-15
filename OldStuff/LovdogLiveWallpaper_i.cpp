#include <vector>
#include <string>
#include <xcb/xcb.h>
#include <opencv2/opencv.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <sys/types.h>
#include <getopt.h>
#include <signal.h>

const char* PID_FILE = "/tmp/lovdog_live_wallpaper.pid";

struct RuntimeOptions {
    bool stop_previous = false;
    std::string descriptor_file;
    std::string target_id;
    // ... otros flags
};

struct WallpaperConfig {
    std::string path;
    int  delay_ms     = 33   ; // Default ~30fps
    int  width        = 1920 ;
    int  height       = 1080 ;
    bool is_video     = false;
    bool is_directory = false;
};

RuntimeOptions options;

void parse_args(int argc, char** argv) {
    static struct option long_options[] = {
        {"send-stop", no_argument, 0, 's'},
        {"descriptor-file", required_argument, 0, 'f'},
        {"descriptor-id", required_argument, 0, 'i'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "sf:i:", long_options, nullptr)) != -1) {
        switch (opt) {
            case 's': options.stop_previous   = true  ; break;
            case 'f': options.descriptor_file = optarg; break;
            case 'i': options.target_id       = optarg; break;
        }
    }
}

void load_descriptor(const std::string& file, const std::string& id) {
    std::ifstream fs(file);
    std::string line;
    while (std::getline(fs, line)) {
        // Buscamos la línea que contiene el ID (ej: LoneWolf)
        if (line.find("-d " + id) != std::string::npos) {
            // Ejemplo de línea: -d LoneWolf -s 200 : 1000 x 800
            // Usar sscanf para parsing rápido y robusto
            char name[256];
            float delay;
            int w, h;
            if (sscanf(line.c_str(), "-d %s -s %f : %d x %d", name, &delay, &w, &h) == 4) {
                std::cout << "Configuración cargada: " << id 
                          << " [Delay: " << delay << "ms, Res: " << w << "x" << h << "]" << std::endl;
                // Aquí actualizarías el usleep y el cv::resize del motor
            }
        }
    }
}

void handle_stop_previous() {
    std::ifstream infile(PID_FILE);
    pid_t old_pid;
    
    if (infile >> old_pid) {
        std::cout << "Terminando instancia previa (PID: " << old_pid << ")..." << std::endl;
        kill(old_pid, SIGTERM); // Envía señal de terminación limpia
        usleep(100000);        // Espera 100ms para que XCB libere el Pixmap
    }
}

void register_current_pid() {
    std::ofstream outfile(PID_FILE);
    outfile << getpid();
    outfile.close();
}

// Helper to set the atoms feh uses to communicate with WMs/Compositors
void update_root_atoms(xcb_connection_t* conn, xcb_window_t root, xcb_pixmap_t pmap) {
    auto get_atom = [&](const char* name) {
        xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, strlen(name), name);
        xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(conn, cookie, NULL);
        xcb_atom_t atom = reply->atom;
        free(reply);
        return atom;
    };

    xcb_atom_t pmap_id = get_atom("_XROOTPMAP_ID");
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, pmap_id, XCB_ATOM_PIXMAP, 32, 1, &pmap);
    
    uint32_t values[] = { pmap };
    xcb_change_window_attributes(conn, root, XCB_CW_BACK_PIXMAP, values);
    xcb_clear_area(conn, 0, root, 0, 0, 0, 0);
    xcb_flush(conn);
}

int main() {
    // 1. Setup XCB
    xcb_connection_t* conn = xcb_connect(NULL, NULL);
    const xcb_setup_t* setup = xcb_get_setup(conn);
    xcb_screen_t* screen = xcb_setup_roots_iterator(setup).data;

    // 2. Setup OpenCV (Treating video as a slideshow of frames)
    cv::VideoCapture cap("wallpaper.mp4"); 
    if(!cap.isOpened()) return -1;

    // 3. Create the persistent Pixmap (The "Canvas")
    xcb_pixmap_t pmap = xcb_generate_id(conn);
    xcb_create_pixmap(conn, screen->root_depth, pmap, screen->root, 
                      screen->width_in_pixels, screen->height_in_pixels);

    xcb_gcontext_t gc = xcb_generate_id(conn);
    xcb_create_gc(conn, gc, pmap, 0, NULL);

    cv::Mat frame, bgra_frame;
    while (true) {
        cap >> frame;
        if (frame.empty()) {
            cap.set(cv::CAP_PROP_POS_FRAMES, 0); // Loop slideshow
            continue;
        }

        // Resize to screen and convert to BGRA (X11 likes 32-bit alignment)
        cv::resize(frame, frame, cv::Size(screen->width_in_pixels, screen->height_in_pixels));
        cv::cvtColor(frame, bgra_frame, cv::COLOR_BGR2BGRA);

        // 4. Push frame to X Server
        xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pmap, gc,
                      screen->width_in_pixels, screen->height_in_pixels,
                      0, 0, 0, screen->root_depth,
                      bgra_frame.total() * bgra_frame.elemSize(), bgra_frame.data);

        // 5. Notify the System
        update_root_atoms(conn, screen->root, pmap);

        // Control the "Slideshow" speed (e.g., 30 FPS)
        usleep(33333); 
    }

    xcb_disconnect(conn);
    return 0;
}
