# Proyecto 1 - paralela

El proyecto trata acerca de la realización de un screensaver creativo, por medio de dos programas, uno secuencial y otro paralelizado, resolviendo el mismo problema, pero con diferentes enfoques. Usando la librería de OpenMp para la parte de paralelización y desplegando los FPS en los que corre el programa para lograr entender la experiencia del usuario y garantizar que no caigan los FPS.  

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

gcc -Ofast -march=native -ffast-math -fno-math-errno -funroll-loops   secuencial.c -o secuencial   $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf) -lGL -lGLU -lm && ./sec 10000

```

compilar y correr el paralelo:
```

gcc -Ofast -march=native -ffast-math -fno-math-errno -funroll-loops   -fopenmp paralelo.c -o paralelo   $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf) -lGL -lGLU -lm && ./paralelo 10000

```
-fopenmp: Activa OpenMP en el compilador
-Ofast: es para maximizar la optimizacion estandar
-march=native: Detecta automáticamente la arquitectura de tu CPU local
-ffast-math: sume que las operaciones matemáticas son ideales, es para acelerar el computo numerico
-fno-math-errno: ctualizan errno en caso de error
-funroll-loops: Convierte bucles cortos en secuencias repetidas
