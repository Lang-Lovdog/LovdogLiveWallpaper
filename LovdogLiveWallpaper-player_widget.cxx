#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <algorithm> // Para remove

// --- CONFIGURACIÓN DE DIMENSIONES ---
const int TOTAL_WIDTH = 38;
const int COL_LABEL   = 8;
const int COL_CONTENT = 26;

// --- UTILIDADES DE TEXTO ---
void clean_string(std::string& s) {
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    // Si después de limpiar solo quedan espacios, la vaciamos
    if (s.find_first_not_of(' ') == std::string::npos) s = "";
}

// Cuenta caracteres lógicos UTF-8 (no bytes)
int visual_len(const std::string& s) {
    int len = 0;
    for (size_t i = 0; i < s.length(); i++) {
        if ((s[i] & 0xc0) != 0x80) len++;
    }
    return len;
}

// Imprime texto y rellena con espacios hasta alcanzar el ancho deseado
void print_padded(const std::string& text, int width) {
    std::cout << text;
    int padding = width - visual_len(text);
    for (int i = 0; i < padding; ++i) std::cout << " ";
}

// Divide el texto en líneas para que quepa en la celda
std::vector<std::string> wrap_text(std::string text, int width) {
    std::vector<std::string> lines;
    if (text.empty()) return {" "};
    
    while (visual_len(text) > width) {
        size_t pos = text.find_last_of(' ', width);
        if (pos == std::string::npos || pos == 0) pos = width;
        lines.push_back(text.substr(0, pos));
        text = text.substr(pos);
        if (!text.empty() && text[0] == ' ') text.erase(0, 1);
    }
    if (!text.empty()) lines.push_back(text);
    return lines;
}

// --- RENDERIZADO DE TABLA ---

void draw_sep(std::string type) {
    if (type == "top")    std::cout << "┌──────────┬────────────────────────────┐" << std::endl;
    if (type == "mid")    std::cout << "├──────────┼────────────────────────────┤" << std::endl;
    if (type == "bottom") std::cout << "└──────────┴────────────────────────────┘" << std::endl;
}

void draw_row(std::string icon, std::string text) {
    auto lines = wrap_text(text, COL_CONTENT);
    for (size_t i = 0; i < lines.size(); ++i) {
        std::cout << "│ ";
        if (i == 0) {
            print_padded("  " + icon, COL_LABEL);
        } else {
            print_padded("", COL_LABEL);
        }
        std::cout << " │ ";
        print_padded(lines[i], COL_CONTENT);
        std::cout << " │" << std::endl;
    }
}

// --- LÓGICA DE DATOS ---

std::string get_mpris() {
    std::shared_ptr<FILE> pipe(popen("playerctl metadata --format '{{playerName}}|{{status}}|{{title}}|{{artist}}|{{album}}' 2>/dev/null", "r"), pclose);
    if (!pipe) return "";
    char buffer[256];
    std::string result = "";
    while (fgets(buffer, 256, pipe.get()) != nullptr) result += buffer;
    return result;
}

int main() {
    std::string raw = get_mpris();
    if (raw.empty()) {
        std::cout << "[none]\n";
        draw_sep("top");
        std::cout << "│ 󰝚  No media active                │" << std::endl;
        draw_sep("bottom");
        return 0;
    }

    std::stringstream ss(raw);
    std::string p_name, p_status, p_title, p_artist, p_album;
    std::getline(ss, p_name, '|');
    std::getline(ss, p_status, '|');
    std::getline(ss, p_title, '|');
    std::getline(ss, p_artist, '|');
    std::getline(ss, p_album, '|');

    // LIMPIEZA QUIRÚRGICA
    clean_string(p_name);
    clean_string(p_status);
    clean_string(p_title);
    clean_string(p_artist);
    clean_string(p_album);

    // Fallback para álbum vacío
    if (p_album.empty()) p_album = "Ax moixmati";

    std::string st_icon = (p_status == "Playing" ? "󰐊 ON" : "󰏤 OFF");

    // RENDER FINAL
    std::cout << "[" << p_name << "]" << std::endl;
    draw_sep("top");
    
    // Fila de Status
    std::cout << "│ STATUS   │ ";
    print_padded(st_icon, COL_CONTENT);
    std::cout << " │" << std::endl;

    draw_sep("mid");
    draw_row("󰎈", p_title);
    draw_sep("mid");
    draw_row("󰠃", p_artist);
    draw_sep("mid");
    draw_row("󰀥", (p_album.empty() ? "Web Stream" : p_album));
    draw_sep("bottom");

    return 0;
}
