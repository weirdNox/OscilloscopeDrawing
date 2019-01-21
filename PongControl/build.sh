#!/usr/bin/env sh
if [ ! -d "build/" ]
then
    mkdir build
    cc -O2 ../External/rglfw.c -c -o build/rglfw.o
    c++ -O2 -I../External/ ../External/imgui/imgui.cpp -c -o build/imgui.o
    c++ -O2 -I../External/ ../External/imgui/imgui_draw.cpp -c -o build/imgui_draw.o
    c++ -O2 -I../External/ ../External/imgui/imgui_widgets.cpp -c -o build/imgui_widgets.o
    c++ -O2 -I../External/ -I../External/glfw/include ../External/imgui/imgui_impl_glfw.cpp -c -o build/imgui_impl_glfw.o
    c++ -O2 -I../External/ ../External/imgui/imgui_impl_opengl3.cpp -c -o build/imgui_impl_opengl3.o
fi
c++ -Wall -Wextra -Wno-unused-function -g3 -lGL -lX11 -ldl -lpthread -I../External/ -I../Shared main.cpp build/*.o -o build/PongControl
