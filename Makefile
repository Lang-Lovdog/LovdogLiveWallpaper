OCV_FLAGS=`pkg-config --cflags --libs xcb opencv4` -O3
FILES=LovdogLiveWallpaper.cxx LovdogLiveWallpaper-frame-preprocess.cxx LovdogLiveWallpaper-loops.cxx LovdogLiveWallpaper-main.cpp LovdogLiveWallpaper-cava.cxx

build:
	g++ $(FILES) -o llw $(OCV_FLAGS) -Wall

install:
	sudo cp llw /usr/local/bin
