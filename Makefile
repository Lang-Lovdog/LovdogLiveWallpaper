OCV_FLAGS=`pkg-config --cflags --libs xcb opencv4` -O3 -lfreetype -lfontconfig
FILES=LovdogLiveWallpaper.cxx LovdogLiveWallpaper-frame-preprocess.cxx LovdogLiveWallpaper-loops.cxx LovdogLiveWallpaper-main.cpp LovdogLiveWallpaper-cava.cxx LovdogLiveWallpaper-widgets.cxx

all: build install

build:
	g++ $(FILES) -o llw $(OCV_FLAGS) -Wall

buildi:
	g++ $(FILES) -o llwi $(OCV_FLAGS) -Wall

install:
	sudo cp llw /usr/local/bin
