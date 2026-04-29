#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;
using IconMap = std::map<std::string, std::string>;

struct TreeConfig {
    bool only_dirs = false;
    bool animated = false;
    bool loop     = false;
    bool scroll_mode = false;
    int max_depth = 1;
    int chunk_size = 0;
    int line_max_width = 20;
    int animation_delay_ms = 200000;
    int line_delay_ms = 200000;
    std::set<std::string> exclude_ext;
    std::set<std::string> include_only_ext;
    bool exclude_no_ext = false;
    bool exclude_hidden = true; // Por defecto ocultamos .archivos
};

const std::string ICON_EXEC = "󱆃 ";

// Utility para parsear listas separadas por coma
std::set<std::string> parse_list(std::string list, bool &special_0, bool &special_1) {
    std::set<std::string> s;
    std::stringstream ss(list);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item == "0") special_0 = true;
        else if (item == "1") special_1 = false; // "1" en tu lógica habilita ocultos
        else s.insert(item);
    }
    return s;
}

std::string truncate_name(std::string name, size_t max_len) {
    if (name.length() <= max_len) return name;
    return name.substr(0, max_len - 3) + "...";
}

std::string get_icon(const fs::directory_entry& entry, const IconMap& icon_map) {
    if (entry.is_directory()) return " ";
    std::string ext = entry.path().extension().string();
    if (icon_map.count(ext)) return icon_map.at(ext);
    auto perms = entry.status().permissions();
    if ((perms & fs::perms::owner_exec) != fs::perms::none) return ICON_EXEC;
    return "󰈔 ";
}

bool should_show(const fs::directory_entry& entry, const TreeConfig& cfg) {
    std::string name = entry.path().filename().string();
    std::string ext = entry.path().extension().string();

    if (cfg.exclude_hidden && name[0] == '.') return false;
    if (entry.is_directory()) return true;
    if (cfg.only_dirs) return false;

    // Filtros de extensión
    if (ext.empty() && cfg.exclude_no_ext) return false;
    
    if (!cfg.include_only_ext.empty()) {
        return cfg.include_only_ext.count(ext) > 0;
    }

    if (cfg.exclude_ext.count(ext)) return false;

    return true;
}

void print_tree(const fs::path& path, const IconMap& icon_map, const TreeConfig& cfg, 
                const std::string& prefix = "", int depth = 0, int* line_count = nullptr) {
    
    if (depth > cfg.max_depth || !fs::exists(path)) return;

    std::vector<fs::directory_entry> entries;
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (should_show(entry, cfg)) entries.push_back(entry);
        }
    } catch (...) { return; }

    for (size_t i = 0; i < entries.size(); ++i) {
        bool is_last = (i == entries.size() - 1);
        auto& entry = entries[i];

        // --- NUEVA ANIMACIÓN SUAVE ---
        if (cfg.animated) {
            std::cout << std::flush;
            // Usamos un retraso más pequeño por línea (ej. 1/4 del delay del chunk)
            usleep(cfg.animation_delay_ms / (cfg.chunk_size > 0 ? cfg.chunk_size : 5));
        }

        // Estructura
        std::cout << "\033[90m" << prefix << (is_last ? "└── " : "├── ") << "\033[0m";
        
        std::string icon = get_icon(entry, icon_map);
        
        // Color por tipo
        if (entry.is_directory()) std::cout << "\033[34m";
        else if (icon == ICON_EXEC) std::cout << "\033[32m";
        else std::cout << "\033[37m";

        // --- RECORTE DE NOMBRE ---
        // Limitamos a 20 caracteres por ejemplo, o puedes pasarlo en el TreeConfig
        std::string display_name = truncate_name(entry.path().filename().string(), cfg.line_max_width);
        std::cout << icon << display_name << "\033[0m" << std::endl;

        if (entry.is_directory()) {
            print_tree(entry.path(), icon_map, cfg, prefix + (is_last ? "    " : "│   "), depth + 1, line_count);
        }
    }
}

void collect_tree(const fs::path& path, const IconMap& icon_map, const TreeConfig& cfg, 
                  std::vector<std::string>& buffer, const std::string& prefix = "", int depth = 0) {
    if (depth > cfg.max_depth || !fs::exists(path)) return;

    std::vector<fs::directory_entry> entries;
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (should_show(entry, cfg)) entries.push_back(entry);
        }
    } catch (...) { return; }

    for (size_t i = 0; i < entries.size(); ++i) {
        bool is_last = (i == entries.size() - 1);
        auto& entry = entries[i];
        
        std::stringstream line;
        line << "\033[90m" << prefix << (is_last ? "└── " : "├── ") << "\033[0m";
        
        std::string icon = get_icon(entry, icon_map);
        if (entry.is_directory()) line << "\033[34m";
        else if (icon == ICON_EXEC) line << "\033[32m";
        else line << "\033[37m";

        line << icon << truncate_name(entry.path().filename().string(), 20) << "\033[0m";
        
        buffer.push_back(line.str());

        if (entry.is_directory()) {
            collect_tree(entry.path(), icon_map, cfg, buffer, prefix + (is_last ? "    " : "│   "), depth + 1);
        }
    }
}

void render_scroll_tree(const std::vector<std::string>& buffer, const TreeConfig& cfg) {
    // 1. Pre-llenamos con líneas vacías para fijar la altura del widget
    std::vector<std::string> visible_lines(cfg.chunk_size, std::string(cfg.line_max_width, ' ')); 
    
    for (const auto& new_line : buffer) {
        // 2. FIFO: Añadimos al final y quitamos la primera (que será un vacío al inicio)
        visible_lines.push_back(new_line);
        if (visible_lines.size() > cfg.chunk_size) {
            visible_lines.erase(visible_lines.begin());
        }

        // 3. Volcado ANSI: Usamos \033[H para sobreescribir siempre el mismo bloque
        // En tu caso, al ser un widget de Live Wallpaper, esto asegura que no parpadee
        std::cout << "\033[H"; 
        
        for (const auto& line : visible_lines) {
            // Si la línea está vacía, imprimimos una línea en blanco para mantener el hueco
            if (line.empty()) std::cout << "\n";
            else std::cout << line << "\n";
        }

        std::cout << std::flush;
        
        // Animación suave línea por línea
        usleep(cfg.animation_delay_ms);
    }
}

//void render_scroll_tree(const std::vector<std::string>& buffer, const TreeConfig& cfg) {
//    std::vector<std::string> visible_lines;
//    
//    for (const auto& new_line : buffer) {
//        // Añadimos la nueva línea al final (FIFO)
//        visible_lines.push_back(new_line);
//
//        // Si excedemos el tamaño del "chunk" (ventana visible), quitamos la más vieja
//        if (cfg.chunk_size > 0 && visible_lines.size() > cfg.chunk_size) {
//            visible_lines.erase(visible_lines.begin());
//        }
//
//        // Limpiar pantalla/sección (Escape ANSI para volver al inicio del widget)
//        // \033[H vuelve al inicio, \033[J borra hasta el final
//        std::cout << "\033[H"; 
//        
//        for (const auto& line : visible_lines) {
//            std::cout << line << "\n";
//        }
//
//        std::cout << std::flush;
//        usleep(cfg.animation_delay_ms);
//    }
//}

int main(int argc, char* argv[]) {
    TreeConfig cfg;
    std::string target = ".";
    int lines = 0;

    // Parser de argumentos manual para tus banderas
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-xf") cfg.only_dirs = true;
        else if (arg == "-a") cfg.animated = true;
        else if (arg == "-l") cfg.loop = true;
        else if (arg == "-s") cfg.scroll_mode = true;
        else if (arg == "-w") cfg.line_max_width = std::stoi(argv[++i]);
        else if (arg == "-D") cfg.animation_delay_ms = std::stoi(argv[++i])*1000;
        else if (arg == "-d" && i + 1 < argc) cfg.max_depth = std::stoi(argv[++i]);
        else if (arg == "-c" && i + 1 < argc) cfg.chunk_size = std::stoi(argv[++i]);
        else if (arg == "-xx" && i + 1 < argc) {
            bool dummy; // para el flag '1' que habilitaría ocultos
            cfg.exclude_ext = parse_list(argv[++i], cfg.exclude_no_ext, cfg.exclude_hidden);
        }
        else if (arg == "-lx" && i + 1 < argc) {
            bool dummy;
            cfg.include_only_ext = parse_list(argv[++i], cfg.exclude_no_ext, cfg.exclude_hidden);
        }
        else if (arg[0] != '-') target = arg;
    }

    IconMap icon_map = {{".c", " "}, {".cpp", " "}, {".tex", " "}, {".pdf", " "}, {".py", " "}};

    do {
        // Limpiamos la sección del widget antes de cada ciclo
        // \033[2J borra la pantalla, \033[H vuelve arriba
        if (cfg.loop) std::cout << "\033[2J\033[H" << std::flush;

        if (cfg.scroll_mode) {
            std::vector<std::string> tree_buffer;
            collect_tree(target, icon_map, cfg, tree_buffer);
            render_scroll_tree(tree_buffer, cfg);
        } else {
            print_tree(target, icon_map, cfg, "", 0, &lines);
        }

        if (cfg.loop) {
            // Pausa antes de volver a escanear el directorio
            // Por ejemplo, esperar 2 segundos antes de la siguiente "vuelta"
            usleep(cfg.animation_delay_ms); 
        }

    } while (cfg.loop);

    return 0;
}
