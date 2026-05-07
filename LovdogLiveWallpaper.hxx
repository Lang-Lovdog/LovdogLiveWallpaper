#include <vector>
#include <string>
#include <xcb/xcb.h>
#include <opencv2/opencv.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <xcb/randr.h>
#include <fstream>
#include <sys/types.h>
#include <getopt.h>
#include <opencv2/freetype.hpp>
#include <signal.h>
#include <filesystem>


// Representa un fragmento de texto con un único estilo
struct TextSegment {
    std::string text;
    char attributes; // Tu enum TermColor
};
// Cada línea del widget es ahora un vector de segmentos
typedef std::vector<TextSegment> ANSILine;
typedef std::vector<ANSILine> widget_text_color;

typedef cv::Ptr<cv::freetype::FreeType2> freetype2_t;
typedef int fifo_t;
typedef std::vector<std::string> widget_text;
typedef std::vector<std::string> widget_conf;
typedef std::vector<widget_text> Widgets;

enum InputType : size_t {
    TYPE_NONE        = 0b00000,
    TYPE_DESCRIPTOR  = 0b10000,
    TYPE_VIDEO       = 0b01000,
    TYPE_GIF         = 0b00100,
    TYPE_DIR_SLIDE   = 0b00010
};

enum RenderType : char {
    SCREEEN_FILL    = 0b1000,
    SCREEEN_CENTER  = 0b0100,
    SCREEEN_STRETCH = 0b0010,
};

enum CommandSent : unsigned int {
    COMMAND_RELOAD_WIDGETS  = 0b000001,
    COMMAND_RELOAD_CONFIG   = 0b000010,
    COMMAND_TOGGLE_CAVA     = 0b000100,
    COMMAND_TOGGLE_WIDGET   = 0b001000,
    COMMAND_STOP_LLW        = 0b010000,
};

enum TermColor : char{ 
    RED     = 0b00001,
    GREEN   = 0b00010,
    BLUE    = 0b00100,
    CYAN    = 0b00110,
    MAGENTA = 0b00101,
    YELLOW  = 0b00011,
    WHITE   = 0b00111,
    BLACK   = 0b00000,
    BRIGHT  = 0b01000,
    INVERT  = 0b10000
};

struct RuntimeOptions {
    bool          stop_previous      = false                     ;
    std::string   descriptor_file                                ;
    std::string   target_id                                      ;
    std::string   video_path                                     ;
    char          type                                           ;
    bool          enable_cava        = false                     ;
    bool          use_cava_range     = false                     ;
    bool          use_cava_adaptive  = false                     ;
    bool          enable_widgets     = false                     ;
    widget_conf   widget_config                                  ;
    fifo_t        fifo_fd            = -1                        ;
    volatile bool keep_running       = true                      ;
    bool          reload_widgets     = false                     ;
    bool          read_widget_config = true                      ;
    std::string   cava_file          =
                  "/tmp/lovdog_live_wallpaper/cava_fifo"         ;
    // Colores                                  BBB  GGG  RRR 8bit-decimal
    cv::Scalar    term_r             = cv::Scalar(000, 000, 127) ;
    cv::Scalar    term_g             = cv::Scalar(000, 127, 000) ;
    cv::Scalar    term_b             = cv::Scalar(127, 000, 000) ;
    cv::Scalar    term_c             = cv::Scalar(127, 127, 000) ;
    cv::Scalar    term_m             = cv::Scalar(127, 000, 127) ;
    cv::Scalar    term_y             = cv::Scalar(000, 127, 127) ;
    cv::Scalar    term_k             = cv::Scalar(000, 000, 000) ;
    cv::Scalar    term_w             = cv::Scalar(127, 127, 127) ;
    // Colores Bold
    cv::Scalar    term_R             = cv::Scalar(000, 000, 255) ;
    cv::Scalar    term_G             = cv::Scalar(000, 255, 000) ;
    cv::Scalar    term_B             = cv::Scalar(255, 000, 000) ;
    cv::Scalar    term_C             = cv::Scalar(255, 255, 000) ;
    cv::Scalar    term_M             = cv::Scalar(255, 000, 255) ;
    cv::Scalar    term_Y             = cv::Scalar(000, 255, 255) ;
    cv::Scalar    term_K             = cv::Scalar(000, 000, 000) ;
    cv::Scalar    term_W             = cv::Scalar(255, 255, 255) ;
    
    // ... otros flags
};

struct WallpaperConfig {
    std::string  path;
    int          delay_ms                  = 33                           ;
    int          media_fps                 = 30                           ;
    int          transition_delay          = 5000                         ;
    int          cava_fps                  = 24                           ;
    int          width                     = 1920                         ;
    int          height                    = 1080                         ;
    int          rn_width                  = 1920                         ;
    int          rn_height                 = 1080                         ;
    int          x_start                   = 0                            ;
    int          y_start                   = 0                            ;
    int          widget_delay              = 90                           ;
    float        widget_x_prop             = 0.05f                        ;
    float        widget_y_prop             = 0.05f                        ;
    float        widget_font_px            = 12.0f                        ;
    int          widget_font_sv            = 0                            ;
    int          widget_font_sh            = 0                            ;
    std::string  widget_font               = ""                           ;
    int          widget_text_border        = 2                            ;
    int          widget_box_w              = 0                            ;
    int          widget_box_h              = 0                            ;
    int          widget_box_sw             = 1                            ;
    int          widget_box_sh             = 1                            ;
    cv::Scalar   widget_border_color       = cv::Scalar(  3,   3,   1)    ;
    cv::Scalar   widget_background_color   = cv::Scalar(255, 000, 255)    ;
    float        widget_background_opacity = 0.5f                         ;
    float        widget_background_dimming = 0.05f                        ;
    std::string  sep_fill                  = " "                          ;
    std::string  prefix                    =
                   "/tmp/lovdog_live_wallpaper/w_fifo"                    ; 
    std::string  widgets_file              =
                    std::string(getenv("HOME")) +
                    "/.config/LovdogLiveWallpaper/widgets"                ;
    std::string  widgets_theme_file        =
                    std::string(getenv("HOME")) +
                    "/.config/LovdogLiveWallpaper/widgets.theme.toml"     ;
    std::string  config_file               =
                    std::string(getenv("HOME")) +
                    "/.config/LovdogLiveWallpaper/config.toml"            ;
    freetype2_t  ft2                                                      ;
    int          cava_num_bars             = 64                           ;
    float        cava_bars_height          = 0.25f                        ;
    cv::Scalar   cava_color                = cv::Scalar(200, 100, 050)    ;
    cv::Scalar   cava_rgb_min              = cv::Scalar(000, 000, 000)    ; // Color base
    cv::Scalar   cava_rgb_max              = cv::Scalar(255, 255, 255)    ; // Color fin del rango
    bool         cava_random_color         = false                        ;
    float        cava_transparency         = 0.6                          ;
    float        cava__border_transparency = 0.8                          ;
    int          delay_compensation        = 1                            ;
    int          rn_type                   = SCREEEN_FILL                 ;
    size_t       in_type                   = TYPE_NONE                    ;
    std::string  widget_cmd                = ""                           ;
};

struct WidgetElement {
    widget_text       widget                ;
    widget_text_color widget_color          ;
    char              position              ;
    int               box_width             ;
    int               box_height            ;
    int               box_x                 ;
    int               box_y                 ;
    cv::Scalar        border_color          ;
    cv::Scalar        background_color      ;
    float             background_opacity    ;
    float             border_opacity        ;
    int               ttl                   ; // Time To Live (15 iterations)
    std::string       name                  ;
    fifo_t            fifo_fd           = -1;
};

typedef std::vector<WidgetElement*> Widgets_t;

struct MonitorRect {
    int16_t x, y;
    uint16_t w, h;
};

typedef std::vector<std::string> slideshow_paths;

// General control routines
void signal_handler(int sig);
void parse_args(int argc, char** argv, WallpaperConfig& config, RuntimeOptions &options);
void handle_stop_previous();
void register_current_pid();
void update_root_atoms(xcb_connection_t* conn, xcb_window_t root, xcb_pixmap_t pmap);
// Input routines (data loading)
std::string find_wallpaper_path(const std::string& name);
bool load_descriptor(const std::string& file, const std::string& id, WallpaperConfig& config);
void routine_dir_slide(WallpaperConfig& config, cv::VideoCapture& cap);
void routine_video_capture(WallpaperConfig& config, cv::VideoCapture& cap);
void routine_descriptor_dir(const std::string& desc_file, const std::string& id, WallpaperConfig& config, cv::VideoCapture& cap);
bool prepare_capture(WallpaperConfig& config, RuntimeOptions& options, cv::VideoCapture& cap, slideshow_paths &slideshow_list);
// Animation loops
void setImageXCB(
        cv::Mat               &bgra_frame ,
        const WallpaperConfig &config     ,
        xcb_screen_t          *screen     ,
        xcb_connection_t      *conn       ,
        xcb_gcontext_t   &gc              ,
        xcb_pixmap_t     &pmap
);
void adjust_render_dims(WallpaperConfig& config, const xcb_screen_t* screen);
void loop_normal(WallpaperConfig& config, cv::VideoCapture& cap, xcb_screen_t* screen, xcb_connection_t* conn, xcb_gcontext_t& gc, xcb_pixmap_t& pmap);
void loop_slideshow(WallpaperConfig& config, slideshow_paths& cap, xcb_screen_t* screen, xcb_connection_t* conn, xcb_gcontext_t& gc, xcb_pixmap_t& pmap);
void intelligent_image_resize_keep_ratio(cv::Mat& img, int width, int height);
void loop_normal(
        WallpaperConfig  &config,
        RuntimeOptions   &options,
        cv::VideoCapture &cap,
        xcb_screen_t     *screen,
        xcb_connection_t *conn,
        xcb_gcontext_t   &gc,
        xcb_pixmap_t     &pmap
);
void loop_normal_cava(
        WallpaperConfig  &config,
        RuntimeOptions   &options,
        cv::VideoCapture &cap,
        xcb_screen_t     *screen,
        xcb_connection_t *conn,
        xcb_gcontext_t   &gc,
        xcb_pixmap_t     &pmap
);
void loop_slideshow(
        WallpaperConfig  &config,
        RuntimeOptions   &options,
        slideshow_paths  &slideshow_list,
        xcb_screen_t     *screen,
        xcb_connection_t *conn,
        xcb_gcontext_t   &gc,
        xcb_pixmap_t     &pmap
);
void start_loop(
        WallpaperConfig  &config,
        RuntimeOptions   &opts       ,
        slideshow_paths  &slideshow_list,
        cv::VideoCapture &cap,
        xcb_screen_t     *screen,
        xcb_connection_t *conn,
        xcb_gcontext_t   &gc,
        xcb_pixmap_t     &pmap
);
// Cava
void get_cava_bars(cv::Mat& frame, cv::Rect& roi_cava, const WallpaperConfig& config, const RuntimeOptions &options, int num_bars, int fifo);
cv::Scalar hexToScalar(std::string hex);
std::string resolve_font_name(const std::string& font_name);
// Widget
cv::Scalar term_to_scalar(char attr, RuntimeOptions &options);
void update_attributes(const std::string& attr_content, char& current_attr);
ANSILine parse_to_ansi_line(const std::string& line);
std::string get_command_output(const char* cmd);
std::string fetch_khal_agenda();
void draw_system_widget(cv::Mat& frame, const WallpaperConfig& config, std::list<WidgetElement>& active_widgets_list);
void draw_system_widget(cv::Mat& frame, WallpaperConfig& config, std::list<WidgetElement>& active_widgets_list, RuntimeOptions& opts);
std::string fetch_command_output(const std::string& cmd);
cv::Scalar get_adaptive_color(cv::Scalar avg, bool is_text);
void draw_bar_gradient(cv::Mat& bars_mat, const WallpaperConfig& config, int num_bars, int height, std::vector<uint8_t>heights, int bar_w);
void draw_bar(cv::Mat& bars_mat, const WallpaperConfig& config, int num_bars, int height, std::vector<uint8_t>heights, int bar_w);
size_t visual_width(const std::string& s);
void trim_string(std::string& s);
void load_widget_file(const std::string& path, widget_text& text_out);
void load_widget_from_fifo(int fd, widget_text_color& out);
void load_widget_from_fifo(int fd, widget_text& out);
void populate_widgets_from_layout(
    const std::string& layout_line, 
    const WallpaperConfig& cfg, 
    Widgets& widgets_out,
    char &h_gaps,
    std::list<WidgetElement>& active_widgets_list
);
void _populate_widgets_from_layout(
    const std::string& layout_line, 
    const WallpaperConfig& cfg, 
    Widgets& widgets_out
);
cv::Scalar get_ansi_color(int code, bool bold);//, const RuntimeOptions& opts);
void draw_ansi_widget(cv::Mat& frame, const WidgetElement& el, const WallpaperConfig& config, RuntimeOptions& opts);
void compute_max_width(const Widgets& widgets, size_t& max_w_out);
void compute_max_height(const Widgets& widgets, size_t& max_h_out);
std::string utf8_safe_substr(const std::string& s, size_t max_v_w);
void assemble_widgets_row(const Widgets& widgets, const WallpaperConfig& cfg, widget_text& row_out);
void render_widget_from_cmd(cv::Mat& frame, const WallpaperConfig& config, const std::string& text, int& y_cursor);
void render_widget_from_file(cv::Mat& frame, const WallpaperConfig& config, int& y_cursor);
void draw_single_widget(cv::Mat& frame, const WidgetElement& el, const WallpaperConfig& config);
void update_widgets_layout(cv::Mat& frame, const WallpaperConfig& config, const RuntimeOptions &options, int& y_cursor, std::list<WidgetElement>& active_widgets_list);
void cleanup_inactive_widgets(std::list<WidgetElement>& active_widgets_list);
// Conf file parsing
void load_theme_config(WallpaperConfig& config, RuntimeOptions& opts);
void load_main_config(WallpaperConfig& config, RuntimeOptions& opts);
void read_layout_file(const WallpaperConfig& config, RuntimeOptions& options);
void check_and_reload_configs(WallpaperConfig& config, RuntimeOptions& options);

