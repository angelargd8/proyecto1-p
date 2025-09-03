# Proyecto 1 - paralela

# requerimientos
dependencias que deberian de estar instaladas: 
sudo apt update
sudo apt install build-essential libsdl2-dev libsdl2-image-dev libglu1-mesa-dev mesa-utils

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

gcc -O2 secuencial.c -o sec $(sdl2-config --cflags) $(sdl2-config --libs) -lSDL2_image -lSDL2_ttf -lGL -lGLU -lm && ./sec 1000

```

compilar y correr el paralelo:
```

gcc paralelo.c -o paralelo -fopenmp -lSDL2 -lSDL2_image -lSDL2_ttf -lGL -lm && ./paralelo 1000

```


