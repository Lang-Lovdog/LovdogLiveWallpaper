
all:
	echo "use make llwsensors or make llwsensorsi"

llwsensors:
	g++ -std=c++17 -O3 \
    -DBUILDING_STANDALONE \
    ../LovdogLiveWallpaper-sensors.cxx \
    -o llw-sensors \
    -lsensors -Wall -g

llwsensorsi:
	g++ -std=c++17 -O3 \
    -DBUILDING_STANDALONE \
    ../LovdogLiveWallpaper-sensors.cxx \
    -o llw-sensorsi \
    -lsensors -Wall -g

llwtree:
	g++ -std=c++17 ./LovdogLiveWallpaper-tree.cxx -o llw-tree

llwtreei:
	g++ -std=c++17 ./LovdogLiveWallpaper-tree.cxx -o llw-treei
