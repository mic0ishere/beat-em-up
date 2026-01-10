gcc -O2 -L /opt/homebrew/opt/sdl2/lib -o main.o main.cpp -lm -lSDL2 -lpthread -ldl

if [ "$1" = "run" ]; then
    ./main.o
fi