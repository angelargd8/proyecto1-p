# Proyecto 1 - paralela

# requerimientos
- tener instalado OpenMP
- tener OpenGl

Primero debe tener instalado openmp, verificar con el siguiente comando:
```
gcc -fopenmp --version
```

Intalar OpenGL:
```
sudo apt update
sudo apt install mesa-utils freeglut3-dev libglew-dev libglfw3-dev libglm-dev
```

compilar y correr el secuencial:
```
gcc -O2 secuencial.c -o sec $(sdl2-config --cflags --libs) -lSDL2_image -lGL -lGLU && ./sec 10
```


dependencias que instale: 
sudo apt update
sudo apt install build-essential libsdl2-dev libsdl2-image-dev libglu1-mesa-dev mesa-utils

sudo apt install libsdl2-dev libsdl2-image-dev

