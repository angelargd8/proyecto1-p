// libreriasss
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <SDL2/SDL.h> //crear la ventana, gestiona teclado/mouse
#include <SDL2/SDL_image.h> // carga imagenes
#include <GL/gl.h> // funciones de OpenGL
#include <GL/glu.h> // utilidades de OpenGL

int main(int argc, char**argv){
    int semilla = 42;
    srand(semilla);

    int n = atoi(argv[1]);

    if (n<=4){
        printf("El numero de elementos debe de ser mayor que 4");
        return 1;
    }

    //inicializar el sdl
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    //atributros de opengl antes de crear una ventana
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    //crear una ventana con el contexto de open gl
    SDL_Window *win = SDL_CreateWindow("Screensaver", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640,480, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glc = SDL_GL_CreateContext(win);
    if (!glc) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    // Estado basico de OpenGL
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    bool running = true;
    while(running){
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        int w, h;
        SDL_GetWindowSize(win, &w, &h);
        glViewport(0, 0, w, h);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(60.0, (double)w/(double)h, 0.1, 100.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        gluLookAt(0,0,3,  0,0,0,  0,1,0);

        // Limpiar la pantalla
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // aqui es donde se van a dibujar las cosas 

        SDL_GL_SwapWindow(win);
    }

    SDL_GL_DeleteContext(glc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}