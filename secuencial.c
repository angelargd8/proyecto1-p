// libreriasss
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <SDL2/SDL.h> //crear la ventana, gestiona teclado/mouse
#include <SDL2/SDL_image.h> // carga imagenes
#include <GL/gl.h> // funciones de OpenGL
#include <GL/glu.h> // utilidades de OpenGL
#include <SDL2/SDL_ttf.h> // textossss

#include <math.h>
#include <time.h>

#define TEX_COUNT 10

// imagenes 
static const char* waterTexture = "assets/agua.png";
static const char* snowTexture = "assets/snow.png";
static const char* iceTexture = "assets/blue_ice.png";
static const char* lavaTexture = "assets/lava.png";
static const char* diamondTexture = "assets/diamond_ore.png";
static const char* endStoneTexture = "assets/end_stone.png";
static const char* obsidianTexture = "assets/obsidian.png";
static const char* jackoLanternTexture = "assets/jack_o_lantern.png";

static const char* dirtTexture = "assets/dirt_128x128.png";
static const char* grassTexture = "assets/grass_block_top_128x128.png";


static const float TOTAL_TIME = 11.0f;
static const float FADE_TIME = 0.40f;

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

    SDL_Surface* surface = IMG_Load(path);

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

static void advance_seed_to_far_corner(int *seedRow, int *seedCol, int rows, int cols) {
    int sr = *seedRow, sc = *seedCol;

    // Distancias Manhattan a las 4 esquinas
    int d00 = sr + sc;                             // (0,0)
    int d01 = sr + (cols - 1 - sc);                // (0, cols-1)
    int d10 = (rows - 1 - sr) + sc;                // (rows-1, 0)
    int d11 = (rows - 1 - sr) + (cols - 1 - sc);   // (rows-1, cols-1)

    int r = 0, c = 0, maxd = d00;
    if (d01 > maxd) { maxd = d01; r = 0;         c = cols - 1; }
    if (d10 > maxd) { maxd = d10; r = rows - 1;  c = 0; }
    if (d11 > maxd) { maxd = d11; r = rows - 1;  c = cols - 1; }

    *seedRow = r;
    *seedCol = c;
}


static void drawGridCycle(
    GLuint *tex_cycle, int tex_count,
    int stage_idx, float stage_time,
    int rows, int cols, int w, int h,
    int seedRow, int seedCol, bool hasSeed
) {
    if (rows <= 0 || cols <= 0) return;

    float cellW= (float)w / (float)cols; //ancho de cada celda
    float cellH= (float)h / (float)rows; //alto de cada celda

    //distancia maxima manhattan desde 0,0 hasta cualquier celda
    int maxDist = (rows - 1) + (cols - 1);
    float dt = (maxDist > 0) ? (TOTAL_TIME / (float)maxDist) : 0.0f;

    GLuint UNDER = tex_cycle[stage_idx];
    GLuint OVER  = tex_cycle[(stage_idx + 1) % tex_count];

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.f, 1.f, 1.f, 1.f);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            // distancia en pasos desde la semilla de 0,0
            int d;
            if (hasSeed) {
                d = abs(r - seedRow) + abs(c - seedCol);
            } else {
                d = r + c; // fallback: esquina superior izquierda
            }
            float start_t = d * dt;

            //alpha de grass (0 a 1) la celda de la semilla d=0 empieza en grass
            float alpha = 0.f;
            if (d == 0) {
                alpha = 1.f; //semilla instantanea
            } else if (stage_time > start_t) {
                alpha = (FADE_TIME > 0.f) ? (stage_time - start_t) / FADE_TIME : 1.f;
                if (alpha > 1.f) alpha = 1.f;
                if (alpha < 0.f) alpha = 0.f;
            }

            float x = c * cellW, y = r * cellH;

            // UNDER textura base de la etapa
            glBindTexture(GL_TEXTURE_2D, UNDER);
            glColor4f(1.f, 1.f, 1.f, 1.f);
            glBegin(GL_QUADS);
                glTexCoord2f(0.f, 0.f); glVertex2f(x,          y);
                glTexCoord2f(1.f, 0.f); glVertex2f(x + cellW,  y);
                glTexCoord2f(1.f, 1.f); glVertex2f(x + cellW,  y + cellH);
                glTexCoord2f(0.f, 1.f); glVertex2f(x,          y + cellH);
            glEnd();

            // OVER se superpone con alpha
            if (alpha > 0.f) {
                glBindTexture(GL_TEXTURE_2D, OVER);
                glColor4f(1.f, 1.f, 1.f, alpha);
                glBegin(GL_QUADS);
                    glTexCoord2f(0.f, 0.f); glVertex2f(x,          y);
                    glTexCoord2f(1.f, 0.f); glVertex2f(x + cellW,  y);
                    glTexCoord2f(1.f, 1.f); glVertex2f(x + cellW,  y + cellH);
                    glTexCoord2f(0.f, 1.f); glVertex2f(x,          y + cellH);
                glEnd();
            }

        }
    }


    // Deshabilitar texturas
    glDisable(GL_TEXTURE_2D);
}



int main(int argc, char**argv){
    int seedRow = 0;
    int seedCol = 0;
    bool hasSeed = false;


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
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        SDL_Quit();
        return 1;
    }

    //atributros de opengl antes de crear una ventana
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    //crear una ventana con el contexto de open gl
    SDL_Window *win = SDL_CreateWindow("Screensaver", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1536, 1024, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

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

    SDL_GL_SetSwapInterval(1);

    // Estado basico de OpenGL
    // glEnable(GL_DEPTH_TEST);
    // glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // Tamaño inicial y grid fijo
    int w0, h0; SDL_GetWindowSize(win, &w0, &h0);
    Grid g = best_grid(n, w0, h0);  // lo fijamos al inicio

    // // Carga de texturas

    GLuint dirtTex   = load_texture(dirtTexture);
    GLuint grassTex  = load_texture(grassTexture);
    GLuint waterTex  = load_texture(waterTexture);
    GLuint snowTex   = load_texture(snowTexture);
    GLuint iceTex    = load_texture(iceTexture);
    GLuint lavaTex   = load_texture(lavaTexture);
    GLuint diamondTex= load_texture(diamondTexture);
    GLuint obsidianTex = load_texture(obsidianTexture);
    GLuint endStoneTex = load_texture(endStoneTexture);
    GLuint jackTex   = load_texture(jackoLanternTexture);

    if (!dirtTex || !grassTex || !waterTex || !snowTex || !iceTex ||  !lavaTex || !diamondTex || !obsidianTex || !endStoneTex || !jackTex) {
        fprintf(stderr, "No se pudieron cargar texturas\n");
        if (dirtTex)  glDeleteTextures(1, &dirtTex);
        if (grassTex) glDeleteTextures(1, &grassTex);
        if (waterTex) glDeleteTextures(1, &waterTex);
        if (iceTex)  glDeleteTextures(1, &iceTex);
        if (lavaTex) glDeleteTextures(1, &lavaTex);
        if (snowTex) glDeleteTextures(1, &snowTex);
        if (diamondTex) glDeleteTextures(1, &diamondTex);
        if (obsidianTex) glDeleteTextures(1, &obsidianTex);
        if (endStoneTex) glDeleteTextures(1, &endStoneTex);
        if (jackTex) glDeleteTextures(1, &jackTex);
        SDL_GL_DeleteContext(glc); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit(); return 1;
    }

    GLuint tex_cycle[TEX_COUNT] = {
    dirtTex, grassTex, waterTex, iceTex, snowTex, lavaTex, diamondTex, obsidianTex, endStoneTex, jackTex
    };

    // Timer
    Uint32 t0_ms = SDL_GetTicks();

    bool running = true;
    bool draw_lines = true;

    int prev_stage_idx = -1;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (e.key.keysym.sym == SDLK_g)      draw_lines = !draw_lines;
                if (e.key.keysym.sym == SDLK_r)      t0_ms = SDL_GetTicks(); // reinicia la ola
            }
             if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int w, h;
                SDL_GetWindowSize(win, &w, &h);
                float cellW = (float)w / g.cols;
                float cellH = (float)h / g.rows;

                seedCol = (int)(e.button.x / cellW);
                seedRow = (int)(e.button.y / cellH);

                hasSeed = true;
                t0_ms = SDL_GetTicks(); // reinicia el tiempo, la ola comienza en la nueva semilla
            }
        }

        

        int w, h; SDL_GetWindowSize(win, &w, &h);


        // Timer globalss
        float elapsed = (SDL_GetTicks() - t0_ms) / 1000.0f;

        // Cada etapa dura TOTAL_TIME + FADE_TIME
        const float STAGE_DURATION = TOTAL_TIME + FADE_TIME;

        // indice de etapa y tiempo dentro de la etapa
        int   stage_idx  = (int)floorf(elapsed / STAGE_DURATION) % TEX_COUNT;
        float stage_time = fmodf(elapsed, STAGE_DURATION);

        if (prev_stage_idx != stage_idx) {
            if (prev_stage_idx != -1) {
                // Cambió de etapa, mueve la semilla a donde terminó la ola anterior
                advance_seed_to_far_corner(&seedRow, &seedCol, g.rows, g.cols);
                hasSeed = true;
                // No resetea t0_ms: el tiempo global sigue y la nueva etapa arranca suave
                // printf("Nueva etapa %d, semilla en (%d,%d)\n", stage_idx, seedRow, seedCol);
            }
            prev_stage_idx = stage_idx;
        }


        // Dibujo
        glViewport(0, 0, w, h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, (GLdouble)w, (GLdouble)h, 0.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClear(GL_COLOR_BUFFER_BIT);

        drawGridCycle(tex_cycle, TEX_COUNT, stage_idx, stage_time,
                    g.rows, g.cols, w, h,
                    seedRow, seedCol, hasSeed);

        SDL_GL_SwapWindow(win);
    }

    glDeleteTextures(1, &dirtTex);
    glDeleteTextures(1, &grassTex);
    SDL_GL_DeleteContext(glc);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    return 0;
}