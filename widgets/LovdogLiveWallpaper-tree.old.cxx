#include <iostream>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#include <map>
#include <vector>
typedef std::map<std::string, std::string> IconMap;
const std::string ICON_EXEC = "󱆃 "; 
void print_tree(const fs::path& path, const IconMap& icon_map, const std::string& prefix, int depth, int max_depth);
std::string get_icon(const fs::directory_entry& entry, IconMap& icon_map);
void print_tree(const fs::path& path, IconMap& icon_map, const std::string& prefix, int depth, int max_depth);

// Definimos la base de datos de íconos
// Usamos un mapa para asociar extensión -> ícono directamente
std::string get_icon(const fs::directory_entry& entry, IconMap& icon_map) {
    if (entry.is_directory()) return " ";
    
    std::string ext = entry.path().extension().string();
    
    // Si la extensión está en nuestro mapa, la devolvemos
    if (icon_map.count(ext)) {
        return icon_map[ext];
    }
    
    // Verificación de ejecutables (mismo criterio de antes)
    auto perms = entry.status().permissions();
    if ((perms & fs::perms::owner_exec) != fs::perms::none) {
        return "󱆃 ";
    }
    
    // Ícono por defecto para archivos desconocidos
    return "󰈔 ";
}

void print_tree(const fs::path& path, IconMap& icon_map, const std::string& prefix = "", int depth = 0, int max_depth = 2) {
    if (depth > max_depth || !fs::exists(path)) return;

    std::vector<fs::directory_entry> entries;
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.path().filename().string()[0] != '.') {
                entries.push_back(entry);
            }
        }
    } catch (const fs::filesystem_error& e) {
        // En caso de directorios sin permisos de lectura (útil en /root o similares)
        return;
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        bool is_last = (i == entries.size() - 1);
        auto& entry = entries[i];
        
        // Estructura de la rama (Gris tenue)
        std::cout << "\033[90m" << prefix << (is_last ? "└── " : "├── ") << "\033[0m";
        
        // Obtenemos el ícono una sola vez para evitar múltiples búsquedas en el mapa
        std::string icon = get_icon(entry, icon_map);
        
        // Lógica de color según tipo de archivo
        if (entry.is_directory()) {
            std::cout << "\033[34m"; // Azul Salamanca para carpetas
        } else if (icon == ICON_EXEC) {
            std::cout << "\033[32m"; // Verde para ejecutables
        } else {
            std::cout << "\033[37m"; // Blanco estándar para el resto
        }

        std::cout << icon << entry.path().filename().string() << "\033[0m" << std::endl;

        // Llamada recursiva: Pasamos el mapa por referencia y actualizamos el prefijo
        if (entry.is_directory()) {
            print_tree(entry.path(), icon_map, prefix + (is_last ? "    " : "│   "), depth + 1, max_depth);
        }
    }
}

int main(int argc, char* argv[]) {
    std::string target = (argc > 1) ? argv[1] : ".";
    int depth = (argc > 2) ? std::stoi(argv[2]) : 1;

    IconMap icon_map = {
        {".c", " "}, {".cpp", " "}, {".cxx", " "}, {".h", " "}, {".hxx", " "},
        {".py", " "},
        {".rs", " "},
        {".js", " "},
        {".html", " "},
        {".css", " "},
        {".md", " "},
        {".pdf", " "},
        {".tex", " "}, // Muy importante para tu tesis
        {".zip", " "}, {".tar", " "}, {".gz", " "},
        {".png", "󰋩 "}, {".jpg", "󰋩 "}, {".svg", "󰋩 "},
        {".csv", "󰱾 "}, {".tsv", "󰱾 "},
        {".mp3", "󰎄 "}, {".wav", "󰎄 "},
        {".mp4", "󰃽 "}, {".mkv", "󰃽 "}, {".webm", "󰃽 "},
        {".gif", "󰤺 "}
    };
    std::cout << "\033[1;36m󰙅  Directory Tree: " << fs::absolute(target).string() << "\033[0m" << std::endl;
    print_tree(target, icon_map, "", 0, depth);
    
    return 0;
}
