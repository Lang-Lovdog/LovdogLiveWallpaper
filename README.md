# LovdogLiveWallpaper

Live Wallpaper manager for `xcb` based window managers

Since I've seen most of the approaches tends to be focused on `xwinwrap` or bash scripts to loop using `feh`, I though it could be good to create the same `feh -D` slideshow but in the desktop root.

## Why this and not xwinwrap or feh

Mainly, less resources and less steps in the middle, so no bash interpreter and no bash loops which can slow the framerate and increment the resources consumption.

About `xwinwrap`, there's an important problem for any tiling window like `zwm` or `i3wm` which sometimes handle the new root window as another tiled window and they're not fullscreen.

## Small documentation

### Compilation stuff

You'll need `make`, `gcc`, `opencv-devel` and `xcb-devel` packages to compile this. May vary by distro I'm using void.

To compile use `make build`
To compile use `make build`

### For usage

```
llw -v | --video <video path>
llw -f | --descriptor-file <descriptor.maww> -i <wallpaper id>
llw -s | --send-stop
```

The `--send-stop` flag is used to stop the wallpaper. This is to avoid memory leaks due to the frame load.

Descriptor files corresponds to files created by the script `add_animated_wallpaper` which used `ffmpeg` or `ImageMagick` to extract video and gif files into directories for use with a modified version of `maww`.

Each descriptor is given by the syntax `-d id -s period :  width x height` The period is given by 1000/fps. As an example:
```
-d Asciiquarium -s 100 :  640 x 360
```

### Compatible formats
At the moment, tested video formats are
```
mkv
webm
mp4
```

About length of the video, I didn't used videos larger than 2min., so larger ones may result on further RAM usage, **be careful**
