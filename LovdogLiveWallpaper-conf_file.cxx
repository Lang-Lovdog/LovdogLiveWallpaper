#include "LovdogLiveWallpaper.hxx"
#include "toml.hpp" // asumiendo toml++

void load_main_config(WallpaperConfig& config, RuntimeOptions& options) {
    try {
        if (!std::filesystem::exists(config.config_file)) {
            std::cerr << "[LLW] Warning: Main config not found at " << config.config_file << std::endl;
            return;
        }

        auto tbl = toml::parse_file(config.config_file);

        // --- SECCIÓN CAVA ---
        if (auto cava = tbl["CAVA"].as_table()) {
            config.cava_num_bars      = (*cava)["bars"].value_or(64);
            config.cava_bars_height   = (*cava)["height_ratio"].value_or(0.25f);
            config.cava_random_color  = (*cava)["random_color"].value_or(false);
            options.use_cava_range    = (*cava)["color_range"].value_or(false);
            options.use_cava_adaptive = (*cava)["color_adaptive"].value_or(false);
            options.enable_cava       = (*cava)["enable"].value_or(false);
            config.cava_transparency  = (*cava)["transparency"].value_or(0.5f);


            // Cargar color base de CAVA si existe
            if (auto col_arr = (*cava)["color"].as_array()) {
                if (col_arr->size() >= 3) {
                    config.cava_color = cv::Scalar(
                        col_arr->at(0).value_or(0),
                        col_arr->at(1).value_or(0),
                        col_arr->at(2).value_or(0)
                    );
                }
            }
            
            // --- SECCIÓN GRADIENT (Sub-tabla de CAVA) ---
            if (auto grad = tbl["CAVA"]["GRADIENT"].as_table()) {
                auto parse_grad = [&](const std::string& key, cv::Scalar& target) {
                    if (auto arr = (*grad)[key].as_array()) {
                        target = cv::Scalar(arr->at(0).value_or(0), 
                                            arr->at(1).value_or(0), 
                                            arr->at(2).value_or(0));
                    }
                };
                parse_grad("rgb_min", config.cava_rgb_min);
                parse_grad("rgb_max", config.cava_rgb_max);
            }
        }

        // --- SECCIÓN WIDGETS ---
        if (auto w = tbl["WIDGETS"].as_table()) {
            config.widget_x_prop = (*w)["x_prop"].value_or(0.05f);
            config.widget_y_prop = (*w)["y_prop"].value_or(0.05f);
            
            // Alineamos con los nombres del TOML
            config.widget_font_px = (*w)["font_px"].value_or(12);
            config.widget_font = (*w)["font_name"].value_or("Monospace");
            std::cout << "[LLW] Font name: " << config.widget_font << std::endl;
            
            config.widget_background_opacity = (*w)["bg_opacity"].value_or(0.5f);
            config.widget_background_dimming = (*w)["bg_dimming"].value_or(0.05f);
            config.widget_font_sv = (*w)["font_sv"].value_or(0);
            config.widget_font_sh = (*w)["font_sh"].value_or(0);
        }

        std::cout << "[LLW] Main configuration loaded (Bars: " << config.cava_num_bars << ")." << std::endl;

    } catch (const toml::parse_error& err) {
        std::cerr << "Error de sintaxis en config.toml: " << err.description() 
                  << " en (" << err.source().begin.line << ":" << err.source().begin.column << ")" << std::endl;
    }
}

void load_theme_config(WallpaperConfig& config, RuntimeOptions& opts) {
    try {
        // 1. Verificar si el archivo existe antes de parsear
        if (!std::filesystem::exists(config.widgets_theme_file)) {
            std::cerr << "Error: No se encontró el tema en " << config.widgets_theme_file << std::endl;
            return;
        }

        auto tbl = toml::parse_file(config.widgets_theme_file);
        
        // 2. Acceder a la tabla de colores de forma segura
        auto colors = tbl["COLORS"].as_table();
        if (!colors) {
            std::cerr << "Error: No se encontró la sección [COLORS] en el TOML." << std::endl;
            return;
        }

        // 3. Lambda mejorada con validación de tipos
        auto parse_cv = [&](const std::string& key, cv::Scalar& target) {
            if (auto node = (*colors)[key]) {
                if (auto arr = node.as_array()) {
                    if (arr->size() >= 3) {
                        target = cv::Scalar(
                            arr->at(0).value_or(0), // B
                            arr->at(1).value_or(0), // G
                            arr->at(2).value_or(0)  // R
                        );
                        return;
                    }
                }
            }
            // Si llega aquí, el color no existe en el TOML; se queda el default que ya tenía opts
        };

        // Carga de paletas (Normal y Bright)
        parse_cv("term_r", opts.term_r); parse_cv("term_g", opts.term_g);
        parse_cv("term_b", opts.term_b); parse_cv("term_c", opts.term_c);
        parse_cv("term_m", opts.term_m); parse_cv("term_y", opts.term_y);
        parse_cv("term_k", opts.term_k); parse_cv("term_w", opts.term_w);

        parse_cv("term_R", opts.term_R); parse_cv("term_G", opts.term_G);
        parse_cv("term_B", opts.term_B); parse_cv("term_C", opts.term_C);
        parse_cv("term_M", opts.term_M); parse_cv("term_Y", opts.term_Y);
        parse_cv("term_K", opts.term_K); parse_cv("term_W", opts.term_W);

        auto ui = tbl["UI"].as_table();
        if (ui) {
            auto parse_ui = [&](const std::string& key, cv::Scalar& target) {
                if (auto node = (*ui)[key]) {
                    if (auto arr = node.as_array()) {
                        if (arr->size() >= 3) {
                            target = cv::Scalar(
                                arr->at(0).value_or(0), 
                                arr->at(1).value_or(0), 
                                arr->at(2).value_or(0)
                            );
                        }
                    }
                }
            };

            // Estos deben estar definidos en tu WallpaperConfig u options
            parse_ui("border_color", config.widget_border_color);
            parse_ui("background_color", config.widget_background_color);
        }

        std::cout << "[LLW] Tema aplicado correctamente." << std::endl;

    } catch (const toml::parse_error& err) {
        std::cerr << "Error de sintaxis en el TOML: " << err.description() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error inesperado cargando el tema: " << e.what() << std::endl;
    }
}

void read_layout_file(const WallpaperConfig& config, RuntimeOptions& options) {
    // 1. Limpiar el layout actual para que el reload no duplique widgets
    options.widget_config.clear();

    // 2. Abrir el archivo desde la ruta definida en tu struct
    std::ifstream file(config.widgets_file);
    
    if (!file.is_open()) {
        std::cerr << "Error: No se pudo abrir el archivo de layout: " 
                  << config.widgets_file << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Usamos tu función trim_string
        trim_string(line);

        // Ignorar líneas vacías y comentarios
        if (!line.empty() && line[0] != '#') {
            options.widget_config.push_back(line);
        }
    }

    // 3. Marcar como leído para que el loop principal deje de intentar leer
    options.read_widget_config = false;
    
    std::cout << "Layout cargado: " << options.widget_config.size() 
              << " líneas de configuración procesadas." << std::endl;
}

void check_and_reload_configs(WallpaperConfig& config, RuntimeOptions& options) {
    if (options.read_widget_config) {
        std::cout << "[LLW] Reloading all configurations..." << std::endl;
        read_layout_file(config, options);
        load_theme_config(config, options);
        load_main_config(config, options);
        config.ft2 = cv::freetype::createFreeType2();
        config.ft2->loadFontData(resolve_font_name(config.widget_font), 0);
        options.read_widget_config = false;
    }
}

