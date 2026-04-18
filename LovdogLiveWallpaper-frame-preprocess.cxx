#include "LovdogLiveWallpaper.hxx"

extern const char* PID_FILE;

extern bool keep_running;

extern RuntimeOptions options;

namespace fs = std::filesystem;

// Gestor para Video y GIF (OpenCV los trata igual)
void routine_video_capture(WallpaperConfig& config, cv::VideoCapture& cap) {
    cap.open(config.path);
    if(cap.isOpened()) {
        double fps = cap.get(cv::CAP_PROP_FPS);
        if (fps > 0) config.delay_ms = static_cast<int>(1000.0 / fps);
    }
}

// Gestor para Descriptores (Directorios con prefijo Dir/Dir-id.ext)
void routine_descriptor_dir(const std::string& desc_file, const std::string& id, WallpaperConfig& config, cv::VideoCapture& cap) {
    // 1. Cargar datos del archivo .maww (delay, dimensiones)
    if (load_descriptor(desc_file, id, config)) {
        
        // 2. Buscar la ruta física (find_wallpaper_path ya busca en .WallPapers, .SYS_IMAGES, etc.)
        // Si el descriptor dice '-d LoneWolf', find_wallpaper_path devuelve la ruta a la carpeta
        config.path = find_wallpaper_path(id); 

        if (config.path.empty()) {
            std::cerr << "Error: No se encontró la carpeta o archivo para el ID: " << id << std::endl;
            return;
        }

        if (std::filesystem::is_directory(config.path)) {
            // Obtener el nombre base (ej. LoneWolf)
            std::string dir_name = std::filesystem::path(config.path).filename().string();
            
            // 3. Construir el patrón según tu script de Bash: dir/dir-%06d.jpg
            // OpenCV requiere printf-style: %06d para leer 000001, 000002...
            std::string pattern = config.path + "/" + dir_name + "-%06d.jpg";
            
            std::cout << "Cargando secuencia de imágenes: " << pattern << std::endl;
            cap.open(pattern, cv::CAP_IMAGES); 
        } else {
            // Es un video directo (ej. .mp4)
            cap.open(config.path);
        }
    }
}

// Gestor para Slideshow (Presentación de imágenes de los directorios)
void routine_dir_slide(WallpaperConfig& config, cv::VideoCapture& cap) {
    // Aquí podrías forzar un delay más lento, p.ej. 5 segundos
    config.delay_ms = 5000; 
    // OpenCV también puede abrir directorios de imágenes como secuencias
    cap.open(config.path + "/*.jpg"); 
}

bool prepare_capture(WallpaperConfig& config, cv::VideoCapture& cap, slideshow_paths &slideshow_list) {
    switch (config.in_type) {
        case TYPE_VIDEO:
        case TYPE_GIF:
            std::cout<< "Video/Gif: " << config.path << std::endl;
            cap.open(config.path);
            if (cap.isOpened()) {
                double fps   = cap.get(cv::CAP_PROP_FPS);
                config.width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
                config.height= cap.get(cv::CAP_PROP_FRAME_HEIGHT);
                if (fps > 0) config.delay_ms = static_cast<int>(1000.0 / fps);
            }
            break;

        case TYPE_DESCRIPTOR:
            std::cout << "Descriptor: " << config.path << std::endl;
            // 1. Cargamos delay y metadatos desde el .maww usando el ID
            if (load_descriptor(options.descriptor_file, options.target_id, config)) {
                
                // 2. Buscamos la carpeta física que se llama igual que el ID 
                // (ej. LoneWolf o Asciiquarium) en los directorios del sistema
                config.path = find_wallpaper_path(options.target_id);

                if (config.path.empty()) {
                    std::cerr << "[Error] No se encontró el directorio para: " << options.target_id << std::endl;
                    break;
                }

                std::string first_file;
                for (const auto& entry : fs::directory_iterator(config.path)) {
                    if (entry.is_regular_file()) {
                        first_file = entry.path().filename().string();
                        break;
                    }
                }

                if (std::filesystem::is_directory(config.path)) {
                    // 3. Construimos el patrón con %06d (el de tu script de Bash)
                    size_t dot = first_file.find_last_of('.');
                    std::string ext = (dot != std::string::npos) ? first_file.substr(dot) : ".jpg";
                    std::string prefix = std::filesystem::path(config.path).filename().string();
                    std::string pattern = config.path + "/" + prefix + "-%06d" + ext;
                    
                    std::cout << "[Info] Iniciando secuencia: " << pattern << std::endl;
                    cap.open(pattern, cv::CAP_IMAGES);
                } else {
                    // Por si el descriptor apunta a un archivo directo
                    cap.open(config.path);
                }
            } else {
                std::cerr << "[Error] ID '" << options.target_id << "' no encontrado en " << options.descriptor_file << std::endl;
            }
            break;

        case TYPE_DIR_SLIDE:
            std::cout << "Slide Mode" << std::endl;
            if(config.delay_ms==33) config.delay_ms=5000;
            for (const auto& entry : std::filesystem::directory_iterator(config.path))
                if (entry.is_regular_file()) slideshow_list.push_back(entry.path().string());
            return !slideshow_list.empty();
            break;

        default:
            return false;
    }
    return cap.isOpened();
}

