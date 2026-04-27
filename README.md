# LovdogLiveWallpaper

Live Wallpaper manager for `xcb` based window managers

Since I've seen most of the approaches tends to be focused on `xwinwrap` or bash scripts to loop using `feh`, I though it could be good to create the same `feh -D` slideshow but in the desktop root.

## Why this and not xwinwrap or feh

Mainly, less resources and less steps in the middle, so no bash interpreter and no bash loops which can slow the framerate and increment the resources consumption.

About `xwinwrap`, there's an important problem for any tiling window like `zwm` or `i3wm` which sometimes handle the new root window as another tiled window and they're not fullscreen.

![Example](./img/CavaScreenShot.png "My desktop with LLW integrated with CAVA")

![Example](./img/CavaScreenShot-16bit-color.png "My desktop with LLW integrated with CAVA and Widget 16-bit color")

[![Demo](./img/llwFullDemo.gif "Demo of LLW with Widgets and CAVA")](https://raw.githubusercontent.com/Lang-Lovdog/LovdogLiveWallpaper/original/img/llwFullDemo.mp4)

## Small documentation

### Compilation stuff

You'll need `make`, `gcc`, `opencv-devel`, `fontconfig-devel` and `xcb-devel` packages to compile this. May vary by distro I'm using void.
If you're on `debian`-based distros, it should be `dev` instead of `devel`. Check about underscores and naming conventions per distro.

To compile use `make build`
To compile use `make build`

### For usage

```
llw -v | --video            <video path>
llw -f | --descriptor-file  <descriptor.maww>
llw -i | --descriptor-id    <wallpaper id>
llw -s | --send-stop
llw -F | --bg-fill
llw -S | --bg-stretch
llw -C | --bg-center
llw -d | --slideshow        <directory>
llw -D | --slideshow-delay  <int_ms>
llw -T | --transition-delay <int_ms>
llw -c | --cava
llw -Z | --cava-fps         <int_framerate>
llw -H | --cava-height      <float_0_to_1_height_proportion>
llw -R | --cava-rgb         <'#XXXXXX'>
llw -K | --cava-rgb-rng     <'#XXXXXX:#YYYYYY'>
llw -r | --cava-rgb-rnd
llw -B | --cava-bars        <int_number_of_bars>
llw -A | --cava-rgb-ada
llw -w | --widget-cmd       <string executable|command reads stdout>
llw -P | --widget-pos       <x_proportional_to_width:y_proportional_to_height>
llw -X | --widget-fsz       <int font_size>
llw -M | --widget-fnt       <string font_name>
llw -M | --widget-delay     <int frames_to_count>
llw -W | --widget-reload
llw -h | --help
```

`--widget-reload` reloads all the `config` stuff. This command doesn't start a new instance, just send the message.

The `--send-stop` flag is used to stop the wallpaper. This is to avoid memory leaks due to the frame load.

Descriptor files corresponds to files created by the script `add_animated_wallpaper` which used `ffmpeg` or `ImageMagick` to extract video and gif files into directories for use with a modified version of `maww`.

Each descriptor is given by the syntax `-d id -s period :  width x height` The period is given by 1000/fps. As an example:

The `slideshow` does not respond yet to background styles options `fill`, `center` and `stretch`. The current version does a *fill*.

For `CAVA` implementation I did a script `SetUpCavaLLW` so you can modify it to better suit your needs.

Currently `CAVA` integration works only with `GIF/VIDEO` options. And is set to default it's width to the `GIF/VIDEO` width.

`LLW` is not intended to send commands to main instance except for `--send-stop`. Make sure to stop any `LLW` instance before running a new one.

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

### Important dirs

I must say there's **no** parser for any **config** file. But any descriptor file should be placed at (precedence order):
```
$HOME/.SYS_IMAGES
$HOME/.WallPapers
$HOME/local/sys_images
$HOME/local/wallpapers
```
Inside each one there could be any `VIDEOS` and or `GIFS` directory for the `SetDescriptorLLW` script which should be also placed in the *wallpapers* directory
That script is intended to be used as the *trigger*  for the wallpaper manager, performs a search and shuffles to initiate a random wallpaper. This script is nor needed nor important but an utility.

### Installation

In order to install, the command is pretty simple

The `make install` has sudo in it, so no need to `sudo make install` avoiding possible root ownership issues regarding additional steps in the future.

Now `m̀ake` with **no arguments** is enough to build and install. Please make sure, your instance is not running before updating.

```
make build
make install
```

### TODO

Since I'm in masters studies toward research curriculum I cannot assure I'll add every element in todo. At least, not as fast as I want.

- [ x ] Add screen pixmap resizing options
- [ x ] Add CAVA-like visualization
- [ x ] Add slideshow
- [  ] Add system info monitoring:  Widget support opened the doors for this one :3
- [ x ] Add widget-like element
- [ ! ] Config file support (WIP)

As the obsessive wolf I am, and with the high amount of ideas here's a list of maybes I got in my head:
 - Make LovdogWallpaperManager
 - Add extras like power management
 - Pop up notifications
 - Add mpv-like yt-dlp support for online videos
 - Add sound support if music/fx synced with wallpaper


### Widgets Utilities present

- SetDescriptorLLW: Opens normal LLW
- SetUpCavaLLW: Opens LLW with CAVA integration
- SetUpWidgetsLLW: Open LLW with Widgets
- SetUpWidgetsCavaLLW: Opens LLW with CAVA integration and Widgets
All `Set*LLW` will be replaced by llw-manager in a future (hopefully).

- widgets/get_widgets_data: is just a bash script to send widgets to fifo elements in /tmp/LovdogLiveWallpaper (future releases will be an actual command output manager called llw-widgets)
- widgets/khal_widget: Customized khal integration to widget (requires right iso week number and firstday monthname in khal config)
- widgets/get_wttr: The less conflicting widget, just weather in a simple 4 rows format
- widgets/llw-sensors: C++ implementation to get current cpu usage and temperature as well as battery. Requires `sensors-devel` to be compiled.

As you can see, most of widgets are actually very flavoured to my taste. So they aren't plug and play since further configurations or dependencies are required.

`llw` widget support are only non-formatted text output (further format options are in my mind but shall disappoint).

### Can I make my own widget? Yeah!
If you want to make your own widget this should be an scripted process involving `fifo` making and a constant info dumping.
In my experience, the best way to do it if your command does not send the full dump in one shot, you shall take the output into a bash variable and `echo` with the vairable quoted.

#### A very **Simple** example of wttr.in widget
```
#!/bin/bash

#### Here, the wttrin is the id to be used in widgets config file
#### The file searches for any /tmp/lovdog_live_wallpaper/w_fifo_* file
#### and displays its content once the widgets file shows its id
fifo_file=/tmp/lovdog_live_wallpaper/w_fifo_wttrin
mkfifo "$fifo_file"

keep_running=1

#### Maybe not needed, but keeps opened until some routine writes the file
cat "$fifo_file" > /dev/null &

### Please handle instance exit and stuff for redundant processes concurence problem avoiding.
### And to avoid any orphan zombiing
trap ExitHandler INT
trap ExitHandler EXIT

ExitHandler(){
  keep_running=0
}

while [ $keep_running -eq 1 ]; do
  weather_report="$(curl wttr.in/T&2 2>/dev/null)"

  echo "$weather_report" > "$fifo_file"
done

rm "$fifo_file"
exit
```

Then in the `widgets` file `$HOME/.config/LovdogLiveWallpaper/widgets`
Suppose there are other widgets `todo`, `sensors`, `cava`, `mff_dates`, `fwa_dates`.
The separator is `::` but it also provides **very basic** formatting.
For example,
Centered `wttrin` in the first row.
`fwa_dates` and `mff_dates` in the second row. Placed left.
`todo`, `sensors` and `cava`. Evenly spaced.
```
:: wttrin ::
fwa_dates :: mff_dates ::
todo :: sensors :: cava
```
Yup, if you thought the `::` can act as some kind of gap placeholder, you're kinda right. THE FORMATTING IS ONLY AVAILABLE IF **NO BOX MARGIN IS GIVEN: `box_sw=-1`** at this point, this is modifiable only in code (but it's modifiable) and the default behaviour is the explained above.

### Config file? Yeah, why not?
Current config example comes inside `config` dir, you need to create a `.config/LovdogLiveWallpaper/config.toml` and a `.config/LovdogLiveWallpaper/widgets.theme.toml`. The one here comes with `catpuccin machiato`.
Yeah, now widgets support 16-bit colors!

