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
#include <filesystem>

const std::string cava_file="/tmp/cava_fifo";


typedef std::vector<std::string> widget_text;


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

struct RuntimeOptions {
    bool        stop_previous      = false ;
    std::string descriptor_file            ;
    std::string target_id                  ;
    std::string video_path                 ;
    char        type                       ;
    bool        enable_cava        = false ;
    bool        use_cava_range     = false ;
    bool        use_cava_adaptive  = false ;
    bool        enable_widgets     = false ;
    // ... otros flags
};

struct WallpaperConfig {
    std::string  path;
    int          delay_ms          = 33                        ;
    int          transition_delay  = 5000                      ;
    int          cava_fps          = 24                        ;
    int          width             = 1920                      ;
    int          height            = 1080                      ;
    int          rn_width          = 1920                      ;
    int          rn_height         = 1080                      ;
    int          x_start           = 0                         ;
    int          y_start           = 0                         ;
    int          widget_delay      = 90                        ;
    float        widget_x_prop     = 0.05f                     ;
    float        widget_y_prop     = 0.05f                     ;
    float        widget_font_px    = 12.0f                     ;
    std::string  widget_font       = ""                        ;
    int          widget_text_border= 2                         ;
    size_t       widget_box_w      = 0                         ;
    size_t       widget_box_h      = 0                         ;
    size_t       widget_box_sw     = 1                         ;
    size_t       widget_box_sh     = 1                         ;
    std::string  sep_fill          = " "                       ;
    std::string  prefix            =
                   "/tmp/lovdog_live_wallpaper_widget_"        ; 
    std::string  widgets_file      =
                    std::string(getenv("HOME")) +
                    "/.config/LovdogLiveWallpaper/widgets"     ;
    int          cava_num_bars     = 64                        ;
    float        cava_bars_height  = 0.25f                     ;
    cv::Scalar   cava_color        = cv::Scalar(200, 100, 050) ;
    cv::Scalar   cava_rgb_min      = cv::Scalar(000, 000, 000) ; // Color base
    cv::Scalar   cava_rgb_max      = cv::Scalar(255, 255, 255) ; // Color fin del rango
    bool         cava_random_color = false                     ;
    int          rn_type           = SCREEEN_FILL              ;
    size_t       in_type           = TYPE_NONE                 ;
    std::string  widget_cmd        = ""                        ;
};

typedef std::vector<std::string> slideshow_paths;

// General control routines
void signal_handler(int sig);
void parse_args(int argc, char** argv, WallpaperConfig& config);
void handle_stop_previous();
void register_current_pid();
void update_root_atoms(xcb_connection_t* conn, xcb_window_t root, xcb_pixmap_t pmap);
// Input routines (data loading)
std::string find_wallpaper_path(const std::string& name);
bool load_descriptor(const std::string& file, const std::string& id, WallpaperConfig& config);
void routine_dir_slide(WallpaperConfig& config, cv::VideoCapture& cap);
void routine_video_capture(WallpaperConfig& config, cv::VideoCapture& cap);
void routine_descriptor_dir(const std::string& desc_file, const std::string& id, WallpaperConfig& config, cv::VideoCapture& cap);
bool prepare_capture(WallpaperConfig& config, cv::VideoCapture& cap, slideshow_paths &slideshow_list);
// Animation loops
void adjust_render_dims(WallpaperConfig& config, const xcb_screen_t* screen);
void loop_normal(WallpaperConfig& config, cv::VideoCapture& cap, xcb_screen_t* screen, xcb_connection_t* conn, xcb_gcontext_t& gc, xcb_pixmap_t& pmap);
void loop_slideshow(WallpaperConfig& config, slideshow_paths& cap, xcb_screen_t* screen, xcb_connection_t* conn, xcb_gcontext_t& gc, xcb_pixmap_t& pmap);
void intelligent_image_resize_keep_ratio(cv::Mat& img, int width, int height);
void start_loop(
        WallpaperConfig  &config,
        slideshow_paths  &slideshow_list,
        cv::VideoCapture &cap,
        xcb_screen_t     *screen,
        xcb_connection_t *conn,
        xcb_gcontext_t   &gc,
        xcb_pixmap_t     &pmap
);
// Cava
void get_cava_bars(cv::Mat& frame, cv::Rect& roi_cava, const WallpaperConfig& config, int num_bars, int fifo);
cv::Scalar hexToScalar(std::string hex);
// Widget
std::string get_command_output(const char* cmd);
std::string fetch_khal_agenda();
void draw_system_widget(cv::Mat& frame, const WallpaperConfig& config, const std::string& text);
std::string fetch_command_output(const std::string& cmd);
cv::Scalar get_adaptive_color(cv::Scalar avg, bool is_text);
void draw_bar_gradient(cv::Mat& bars_mat, const WallpaperConfig& config, int num_bars, int height, std::vector<uint8_t>heights, int bar_w);
void draw_bar(cv::Mat& bars_mat, const WallpaperConfig& config, int num_bars, int height, std::vector<uint8_t>heights, int bar_w);
size_t visual_width(const std::string& s);
void trim_string(std::string& s);
void load_widget_file(const std::string& path, widget_text& text_out);
void populate_widgets_from_layout(const std::string& layout_line, 
                                 const WallpaperConfig& cfg, 
                                 Widgets& widgets_out);
void compute_max_width(const Widgets& widgets, size_t& max_w_out);
void compute_max_height(const Widgets& widgets, size_t& max_h_out);
std::string utf8_safe_substr(const std::string& s, size_t max_v_w);
void assemble_widgets_row(const Widgets& widgets, const WallpaperConfig& cfg, widget_text& row_out);
void render_widget_from_cmd(cv::Mat& frame, const WallpaperConfig& config, const std::string& text, int& y_cursor);
void render_widget_from_file(cv::Mat& frame, const WallpaperConfig& config, int& y_cursor);
// Conf file parsing

