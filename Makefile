game_engine_linux:
	clang++ src/*.cpp libraries/box2d/src/*.cpp \
	libraries/imgui/imgui.cpp libraries/imgui/imgui_demo.cpp \
	libraries/imgui/imgui_draw.cpp libraries/imgui/imgui_tables.cpp \
	libraries/imgui/imgui_widgets.cpp \
	libraries/imgui/backends/imgui_impl_sdl2.cpp \
	libraries/imgui/backends/imgui_impl_sdlrenderer2.cpp \
	$(shell pkg-config --cflags sdl2) \
	-std=c++17 -I./ -I./src/ \
	-I./libraries/ -I./libraries/glm/ -I./libraries/rapidjson/ \
	-I./libraries/lua/include \
	-I./libraries/box2d/include \
	-I./libraries/imgui -I./libraries/imgui/backends \
	-I./SDL2/ -I./SDL2_image/ -I./SDL2_mixer/ -I./SDL2_ttf/ \
	-lSDL2 -lSDL2_mixer -lSDL2_ttf -lSDL2_image -llua5.4 \
	-O3 -o game_engine_linux
