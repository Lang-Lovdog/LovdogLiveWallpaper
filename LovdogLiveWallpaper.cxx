#include "LovdogLiveWallpaper.hxx"

const char* PID_FILE = "/tmp/lovdog_live_wallpaper.pid";

bool keep_running = true;

RuntimeOptions options;

namespace fs = std::filesystem;

std::string find_wallpaper_path(const std::string& name) {
    const char* home = getenv("HOME");
    if (!home) return "";

    std::vector<std::string> search_paths = {
        std::string(home) + "/.local/wallpapers/",
        std::string(home) + "/.WallPapers/",
        std::string(home) + "/.SYS_IMAGES/",
        std::string(home) + "/.local/sys_images/"
    };

    std::vector<std::string> extensions = {".mp4", ".gif", ".jpg", "jpeg", ".png", ".bmp"};

    for (const auto& base : search_paths) {
        // 1. Intentar ruta directa
        if (fs::exists(base + name)) return base + name;
        
        // 2. Intentar con extensiones
        for (const auto& ext : extensions) {
            if (fs::exists(base + name + ext)) return base + name + ext;
        }
    }
    return "";
}

// Manejador de señales para que al hacer 'kill' se liberen los recursos de XCB
void signal_handler(int sig) {
    keep_running = false;
}

cv::Scalar hexToScalar(std::string hex) {
    if (hex[0] == '#') hex.erase(0, 1);
    
    // Si el string es corto (3 dígitos), expandirlo (ej. "F0F" -> "FF00FF")
    if (hex.length() == 3) {
        std::string full_hex = "";
        for(char c : hex) { full_hex += c; full_hex += c; }
        hex = full_hex;
    }

    int r, g, b;
    std::stringstream ss;
    ss << std::hex << hex.substr(0, 2); ss >> r; ss.clear();
    ss << std::hex << hex.substr(2, 2); ss >> g; ss.clear();
    ss << std::hex << hex.substr(4, 2); ss >> b;

    // Retorna en formato BGR para OpenCV
    return cv::Scalar(b, g, r);
}


void help_old(void){
    std::cout << "Usage: ./LovdogLiveWallpaper [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -s, --send-stop" << std::endl;
    std::cout << "  -f, --descriptor-file <file>" << std::endl;
    std::cout << "  -i, --descriptor-id <id>" << std::endl;
    std::cout << "  -v, --video <video/gif file>" << std::endl;
    std::cout << "  -F, --bg-fill" << std::endl;
    std::cout << "  -C, --bg-center" << std::endl;
    std::cout << "  -S, --bg-stretch" << std::endl;
}

void help(void) {
    std::cout << "=========================================================" << std::endl
              << "          Lovdog Live Wallpaper (LLW) - Help             " << std::endl
              << "=========================================================" << std::endl
              << "Usage: ./LovdogLiveWallpaper [options] [target]" << std::endl
              << "\nGeneral Options:" << std::endl
              << "  -s, --send-stop            Stop any previous instance of LLW." << std::endl
              << "  -h, --help                 Display this help message." << std::endl

              << "\nInput Sources:" << std::endl
              << "  -f, --descriptor-file <f>  Use a specific .maww descriptor file." << std::endl
              << "  -i, --descriptor-id <id>   Target ID inside the descriptor file." << std::endl
              << "  -v, --video <file>         Directly play a Video or GIF file." << std::endl
              << "  -d, --slideshow <dir>      Path to a directory for image slideshow." << std::endl

              << "\nBackground Scaling (Scaling Type):" << std::endl
              << "  -F, --bg-fill              Fill the screen (zoom to fit)." << std::endl
              << "  -C, --bg-center            Center the image (no scaling)." << std::endl
              << "  -S, --bg-stretch           Stretch to fit screen (ignore ratio)." << std::endl

              << "\nSlideshow Configuration:" << std::endl
              << "  -D, --slideshow-delay <ms> Time (ms) to show each image." << std::endl
              << "  -T, --transition-delay <ms>Speed of transition effects." << std::endl

              << "\nCAVA Audio Visualization:" << std::endl
              << "  -c, --cava                 Enable CAVA audio visualization." << std::endl
              << "  -Z, --cava-fps <int>       Framerate for audio processing (default: 24)." << std::endl
              << "  -B, --cava-bars <int>      Number of bars (must match CAVA config)." << std::endl
              << "  -H, --cava-height <int>    Proportion of the screen height for the bars to occupy." << std::endl
              << "  -R, --cava-rgb <hex>       Static color for bars (e.g., #FF00FF)." << std::endl
              << "  -r, --cava-rgb-rnd         Random color for bars every frame." << std::endl
              << "  -K, --cava-rgb-rng <h1:h2> Color range/gradient (e.g., #0000FF:#FF0000)." << std::endl
             
              << "\nExample:" << std::endl
              << "  ./llw --cava -B 64 -K \"#00FFFF:#FF00FF\" -v background.mp4" << std::endl
              << "=========================================================" << std::endl
    ;
}

// En LovdogLiveWallpaper.cxx 
void parse_args(int argc, char** argv, WallpaperConfig& config) {
    static struct option long_options[] = {
        {"send-stop"       , no_argument        , 0, 's'},
        {"descriptor-file" , required_argument  , 0, 'f'},
        {"descriptor-id"   , required_argument  , 0, 'i'},
        {"video"           , required_argument  , 0, 'v'},
        {"bg-fill"         , no_argument        , 0, 'F'},
        {"bg-center"       , no_argument        , 0, 'C'},
        {"bg-stretch"      , no_argument        , 0, 'S'},
        {"slideshow"       , required_argument  , 0, 'd'},
        {"slideshow-delay" , required_argument  , 0, 'D'},
        {"transition-delay", required_argument  , 0, 'T'},
        {"cava"            , no_argument        , 0, 'c'},
        {"cava-fps"        , required_argument  , 0, 'Z'},
        {"cava-bars"       , required_argument  , 0, 'B'},
        {"cava-height"     , required_argument  , 0, 'H'},
        {"cava-rgb"        , required_argument  , 0, 'R'},
        {"cava-rgb-rnd"    , no_argument        , 0, 'r'},
        {"cava-rgb-rng"    , required_argument  , 0, 'K'},
        {"help"            , no_argument        , 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    // Agregamos 'v:' y 'd:' a la cadena de opciones 
    while ((opt = getopt_long(argc, argv, "sf:i:v:d:FCSd:D:T:cZ:B:H:R:rK:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 's': 
                options.stop_previous = true; 
                break;
            case 'f': 
                options.descriptor_file = optarg;
                config.in_type = static_cast<InputType>(config.in_type | TYPE_DESCRIPTOR);
                break;
            case 'i': 
                options.target_id = optarg; 
                break;
            case 'v': 
                options.video_path = optarg;
                config.path = optarg; // Asignación directa para simplificar el main 
                config.in_type = static_cast<InputType>(config.in_type | TYPE_VIDEO);
                break;
            case 'd':
                config.path = optarg;
                config.in_type = static_cast<InputType>(config.in_type | TYPE_DIR_SLIDE);
                break;
            case 'F':
                config.rn_type = SCREEEN_FILL;
                break;
            case 'C':
                config.rn_type = SCREEEN_CENTER;
                break;
            case 'S':
                config.rn_type = SCREEEN_STRETCH;
                break;
            case 'D':
                config.delay_ms= atoi(optarg);
                break;
            case 'T':
                config.transition_delay = atoi(optarg);
            case 'c':
                options.enable_cava=true;
                break;
            case 'Z':
                config.cava_fps = atoi(optarg);
                break;
            case 'B':
                config.cava_num_bars = atoi(optarg);
                break;
            case 'H':{
                    float height = atof(optarg);
                    if(height>1) height = 1;
                    if(height<0) height = 0.3;
                    config.cava_bars_height = height;
                }
                break;
            case 'R': {
                std::string hex = optarg;
                if(hex[0] == '#') hex.erase(0, 1);
                int r, g, b;
                sscanf(hex.c_str(), "%02x%02x%02x", &r, &g, &b);
                config.cava_color = cv::Scalar(b, g, r); // OpenCV usa BGR
                break;
            }
            case 'r':
                config.cava_random_color = true;
                break;
            case 'K': {
                options.use_cava_range = true;
                std::string range = optarg;
                size_t pos = range.find(':');
                if (pos != std::string::npos) {
                    std::string hex1 = range.substr(0, pos);
                    std::string hex2 = range.substr(pos + 1);
                    // Función helper para convertir hex a Scalar...
                    config.cava_rgb_min = hexToScalar(hex1);
                    config.cava_rgb_max = hexToScalar(hex2);
                }
                std::cout << "CAVA rgb Range: "
                          << "min: " << config.cava_rgb_min
                          << " max: " << config.cava_rgb_max
                          << std::endl
                ;
                break;
            }
            case 'h':
                help();
                break;
        }
    }
}

bool load_descriptor(const std::string& file, const std::string& id, WallpaperConfig& config) {
    std::ifstream fs(file);
    if (!fs.is_open()) return false;

    std::string line;
    while (std::getline(fs, line)) {
        if (line.find("-d " + id) != std::string::npos) {
            char name[256];
            float delay;
            int w, h;
            // Siguiendo tu formato del archivo .maww 
            if (sscanf(line.c_str(), "-d %s -s %f : %d x %d", name, &delay, &w, &h) == 4) {
                config.path = find_wallpaper_path(name);
                config.delay_ms = static_cast<int>(delay);
                config.width = w;
                config.height = h;
                
                if (config.path.empty()) {
                    std::cerr << "Error: No se encontró el recurso para " << name << std::endl;
                    return false;
                }
                return true;
            }
        }
    }
    return false;
}

void intelligent_image_resize_keep_ratio(cv::Mat& img, int width, int height) {
    if (img.size().width > width || img.size().height > height) {
        double ratio = std::min((double)width / img.size().width, (double)height / img.size().height);
        width = static_cast<int>(img.size().width * ratio);
        height = static_cast<int>(img.size().height * ratio);
        cv::resize(img, img, cv::Size(width, height));
    }
}

void adjust_render_dims(WallpaperConfig& config,const xcb_screen_t* screen){
    switch(config.rn_type){
        case SCREEEN_FILL:{
            int  xwidth=0, xheight=0;
            double ratio;

            std::cout << "Dims original: " << config.width << "x" << config.height << std::endl;
            if(screen->width_in_pixels  < screen->height_in_pixels){ // Si el ancho es el más corto, procedemos a partir del alto
                std::cout << "Ancho" << std::endl;
                ratio   = (double)config.height/config.width;
                xwidth  = screen->width_in_pixels           ;
                xheight = xwidth * ratio                    ;
                config.y_start = (screen->height_in_pixels - xheight) / 2;
            }else{
                std::cout << "Alto" << config.width << std::endl;
                ratio   = (double)config.width/config.height;
                xheight = screen->height_in_pixels          ;
                xwidth  = xheight * ratio                   ;
                config.x_start = (screen->width_in_pixels - xwidth) / 2;
            }
            std::cout << "Ratio: " << ratio << std::endl;
            config.rn_height = xheight;
            config.rn_width  = xwidth ;
            break;
        }
        case SCREEEN_CENTER:
            config.rn_height = config.width;
            config.rn_width  = config.height;
            config.y_start = (screen->height_in_pixels - config.rn_height) / 2;
            config.x_start = (screen->width_in_pixels  - config.rn_width ) / 2;
            break;
        case SCREEEN_STRETCH:
            config.rn_height = screen->height_in_pixels;
            config.rn_width  = screen->width_in_pixels;
            break;
    }
}

void handle_stop_previous() {
    std::ifstream infile(PID_FILE);
    pid_t old_pid;
    
    if (infile >> old_pid) {
        std::cout << "Terminando instancia previa (PID: " << old_pid << ")..." << std::endl;
        kill(old_pid, SIGTERM); // Envía señal de terminación limpia
        usleep(100000);         // Espera 100ms para que XCB libere el Pixmap
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

