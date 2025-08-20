// libreriasss
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <SDL2/SDL.h> //crear la ventana, gestiona teclado/mouse
#include <SDL2/SDL_image.h> // carga imagenes
#include <GL/gl.h> // funciones de OpenGL
#include <GL/glu.h> // utilidades de OpenGL

#include <math.h>

static const char* dirtTexture = "assets/dirt_64x64.png";

typedef struct {
    int rows, cols;
} Grid;

//seleccionar n = rows x cols y el ratio tiene que ser cols/rols = w/h
static Grid best_grid(int n, int w, int h){
    if (n<=0 ) return (Grid){0,0};
    if (w <= 0 || h<= 0) return (Grid){1,n}; //si no hay tamano de ventana, poner todo en una fila

    double aspect = (double)w / (double)h;
    Grid best = {1, n};

    double best_err = fabs( ( (double)best.cols/(double)best.rows ) - aspect );
    
    for (int d =1; d *d <=n; ++d){
        if ( n% d) continue; //d no es divisor de n

        int r1 = d, c1 = n/d; //opcion A

        double err1 = fabs( ( (double)c1/(double)r1 ) - aspect );
        if (err1 < best_err){
            best = (Grid){r1, c1};
            best_err = err1;
        }

        int r2 = n/d, c2 = d; //transpuesta
        double err2 = fabs( ( (double)c2/(double)r2 ) - aspect );
        if (err2 < best_err){
            best = (Grid){r2, c2};
            best_err = err2;
        }

    }
    return best;
}


static GLuint load_texture(const char* path){

    SDL_Surface* surface = IMG_load(path);

    if (!surface){
        fprintf(stderr, "IMG_Load failed for %s: %s\n", path, IMG_GetError());
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    GLenum format = GL_RGB;
    if (surface->format->BytesPerPixel == 4){
        format = ( surface->format->Rmask == 0x000000ff) ? GL_RGBA : GL_BGRA;
    }
    else if (surface->format->BytesPerPixel == 3){
        format = ( surface->format->Rmask == 0x000000ff) ? GL_RGB : GL_BGR;
    }else{
        SDL_Surface* conv = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
        SDL_FreeSurface(surface);
        surface = conv;
        if (!surface){
            glDeleteTextures(1, &tex);
            return 0;
        }
        format = GL_RGBA;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, (format==GL_RGB||format==GL_BGR)?GL_RGB:GL_RGBA,
                 surface->w, surface->h, 0, format, GL_UNSIGNED_BYTE, surface->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    SDL_FreeSurface(surface);
    return tex;
}



int main(int argc, char**argv){
    int semilla = 42;
    srand(semilla);

    int n = atoi(argv[1]);

    if (n<=4){
        printf("El numero de elementos debe de ser mayor que 4");
        return 1;
    }

    //llamara las funciones para calcular el grid y cargar la textura 

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