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

enum InputType : size_t {
    TYPE_NONE        = 0b0000,
    TYPE_DESCRIPTOR  = 0b1000,
    TYPE_VIDEO       = 0b0100,
    TYPE_GIF         = 0b0010,
    TYPE_DIR_SLIDE   = 0b0001
};

enum RenderType : char {
    SCREEEN_FILL    = 0b1000,
    SCREEEN_CENTER  = 0b0100,
    SCREEEN_STRETCH = 0b0010,
};

struct RuntimeOptions {
    bool        stop_previous = false;
    std::string descriptor_file;
    std::string target_id;
    std::string video_path;
    char        type;
    // ... otros flags
};

struct WallpaperConfig {
    std::string  path;
    int          delay_ms     = 33   ; // Default ~30fps
    int          width        = 1920 ;
    int          height       = 1080 ;
    int          rn_width     = 1920 ;
    int          rn_height    = 1080 ;
    int          x_start      = 0    ;
    int          y_start      = 0    ;
    int          rn_type      = SCREEEN_FILL;
    size_t       in_type      = TYPE_NONE;
};


std::string find_wallpaper_path(const std::string& name);
void signal_handler(int sig);
void parse_args(int argc, char** argv, WallpaperConfig& config);
bool load_descriptor(const std::string& file, const std::string& id, WallpaperConfig& config);
void handle_stop_previous();
void register_current_pid();
void update_root_atoms(xcb_connection_t* conn, xcb_window_t root, xcb_pixmap_t pmap);
void routine_dir_slide(WallpaperConfig& config, cv::VideoCapture& cap);
void routine_video_capture(WallpaperConfig& config, cv::VideoCapture& cap);
void routine_descriptor_dir(const std::string& desc_file, const std::string& id, WallpaperConfig& config, cv::VideoCapture& cap);
bool prepare_capture(WallpaperConfig& config, cv::VideoCapture& cap);
void adjust_render_dims(WallpaperConfig& config, const xcb_screen_t* screen);
void loop_normal(WallpaperConfig& config, cv::VideoCapture& cap, xcb_screen_t* screen, xcb_connection_t* conn, xcb_gcontext_t& gc, xcb_pixmap_t& pmap);

