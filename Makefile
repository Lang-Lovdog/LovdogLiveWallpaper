OCV_FLAGS=`pkg-config --cflags --libs xcb opencv4`

build:
	g++ LovdogLiveWallpaper.cxx LovdogLiveWallpaper-frame-preprocess.cxx LovdogLiveWallpaper-main.cpp -o llw $(OCV_FLAGS) -Wall

install:
	sudo cp llw /usr/local/bin
