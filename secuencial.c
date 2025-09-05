// libreriasss
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <SDL2/SDL.h> //crear la ventana, gestiona teclado/mouse
#include <SDL2/SDL_image.h> // carga imagenes
#include <GL/gl.h> // funciones de OpenGL
#include <GL/glu.h> // utilidades de OpenGL
#include <SDL2/SDL_ttf.h> // textos

#include <math.h>
#include <time.h>

// configuracion
#define TEX_COUNT       10
#define MAX_SEEDS       100
#define DIFF_ITERS      1        
#define USE_GAUSS_BLUR  1        
#define GAUSS_ITERS     2

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


static const float TOTAL_TIME = 11.0f; //tiempo de propagacion base en secS
static const float FADE_TIME = 0.40f; //tiempo de mezcla por celda, igual en sec


//  Estructuras 
typedef struct {
    int rows;
    int cols;
    float** grid; // filas -> punteros a data contigua
} Grid;

typedef struct { 
    float x, y; 
    float alpha; 
} CellInfo;

typedef struct {
    int row, col;
    Uint32 startTime;
} Seed;

static Seed seeds[MAX_SEEDS];
static int   seedCount = 0;

// Utils 
// Limita un float al rango [0,1]. es el  x Valor a limitar.
static inline float clamp01(float x){ return x<0.f?0.f:(x>1.f?1.f:x); }
// limita un entero al rango [lo, hi], b es el valor a limitar y lo y hi son los limites
static inline int   clampi (int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }

/*
reserva una matriz de floats de rows x cols,
devuelve un puntero a la tabla de punteros y un puntero a la data contigua
*/
static float** alloc_grid2d(int rows, int cols, float** backing_out) {
    float **ptrs = (float**)malloc(rows * sizeof(float*));
    float *data  = (float*)malloc(rows * cols * sizeof(float));
    if (!ptrs || !data) { fprintf(stderr,"alloc_grid2d: OOM\n"); exit(1); }
    for (int r = 0; r < rows; ++r) ptrs[r] = data + r*cols;
    *backing_out = data;
    return ptrs;
}

/*
Libera una matriz de floats creada por alloc_grid2d 
*/
static void free_grid2d(float **ptrs, float *backing) {
    if (backing) free(backing);
    if (ptrs)    free(ptrs);
}

/*
reserva una matriz de int de rows x cols, 
devuelve un puntero a la tabla de punteros y un puntero a la data contigua
*/
static int** alloc_grid2d_i(int rows, int cols, int** backing_out) {
    int **ptrs = (int**)malloc(rows * sizeof(int*));
    int *data  = (int*)malloc(rows * cols * sizeof(int));
    if (!ptrs || !data) { fprintf(stderr,"alloc_grid2d_i: OOM\n"); exit(1); }
    for (int r = 0; r < rows; ++r) ptrs[r] = data + r*cols;
    *backing_out = data;
    return ptrs;
}

/*
Libera una matriz de int creada por alloc_grid2d_i 
*/
static void free_grid2d_i(int **ptrs, int *backing) {
    if (backing) free(backing);
    if (ptrs)    free(ptrs);
}

// swap O(1) de tablas de punteros (A <-> B)
static inline void swap_rows(float ***A, float ***B){
    float **tmp = *A; *A = *B; *B = tmp;
}

/*
Calcula el mejor grid para colocar n elementos intentando
aproximar el aspecto de (ancho:alto)
*/
static Grid best_grid(int n, int w, int h){
    if (n<=0 ) return (Grid){0,0,NULL};
    if (w<=0 || h<=0) return (Grid){1,n,NULL};
    double aspect = (double)w/(double)h;
    Grid best = (Grid){1,n,NULL};
    double best_err = fabs(((double)best.cols/(double)best.rows) - aspect);
    for (int d=1; d*d<=n; ++d){
        if (n % d) continue;
        int r1 = d, c1 = n/d;
        double e1 = fabs(((double)c1/(double)r1) - aspect);
        if (e1 < best_err){ best=(Grid){r1,c1,NULL}; best_err=e1; }
        int r2 = n/d, c2 = d;
        double e2 = fabs(((double)c2/(double)r2) - aspect);
        if (e2 < best_err){ best=(Grid){r2,c2,NULL}; best_err=e2; }
    }
    return best;
}


/*
Carga una imagen y la sube como textura de OpenGL y 
usa la SDL_image para decodificar y configura los parámetros 
de filtrado y wrap 
*/
static GLuint load_texture(const char* path){

    SDL_Surface* s = IMG_Load(path);
    if (!s){ 
        fprintf(stderr, "IMG_Load %s: %s\n", path, IMG_GetError()); 
        return 0; 
    }

    GLuint tex=0; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    GLenum format = GL_RGB;

    if (s->format->BytesPerPixel==4) format=(s->format->Rmask==0x000000ff)?GL_RGBA:GL_BGRA;
    else if (s->format->BytesPerPixel==3) format=(s->format->Rmask==0x000000ff)?GL_RGB:GL_BGR;

    else {
        SDL_Surface* conv = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0);
        SDL_FreeSurface(s); s=conv; if(!s){ glDeleteTextures(1,&tex); return 0; }
        format = GL_RGBA;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, (format==GL_RGB||format==GL_BGR)?GL_RGB:GL_RGBA,
                 s->w, s->h, 0, format, GL_UNSIGNED_BYTE, s->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    SDL_FreeSurface(s);
    return tex;
}

/*
Genera una textura con color con el texto del msg usando el 
font y el color de los parámetros. 
*/
static GLuint text_to_texture(TTF_Font* font, const char* msg,
                              SDL_Color col, int* out_w, int* out_h)
{
    SDL_Surface* src = TTF_RenderUTF8_Blended(font, msg, col);

    if (!src) return 0;

    SDL_Surface* s = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(src); if (!s) return 0;

    GLuint tex = 0; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->w, s->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (out_w) *out_w = s->w; if (out_h) *out_h = s->h;

    SDL_FreeSurface(s);
    return tex;
}

/*
Dibuja un quad 2D texturizado en coordenadas de la pantalla 
*/
static void draw_textured_quad(GLuint tex, float x, float y, float w, float h) {
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(1,1,1,1);
    glBegin(GL_QUADS);
      glTexCoord2f(0,0); glVertex2f(x,   y);
      glTexCoord2f(1,0); glVertex2f(x+w, y);
      glTexCoord2f(1,1); glVertex2f(x+w, y+h);
      glTexCoord2f(0,1); glVertex2f(x,   y+h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

// Abre una fuente TTF buscando en la ruta 
static TTF_Font* open_font_portable(int ptsize) {
    const char* rel = "assets/DejaVuSans.ttf";
    char *base = SDL_GetBasePath();
    TTF_Font* f = NULL;
    if (base) {
        size_t need = strlen(base) + strlen(rel) + 1;
        char *p = (char*)malloc(need);
        if (p) { snprintf(p, need, "%s%s", base, rel); f = TTF_OpenFont(p, ptsize); free(p); }
        SDL_free(base);
        if (f) return f;
    }
    return TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", ptsize);
}

// Semilla a esquina más lejana 
/*
Mueve la celda a la esquina Manhattan más lejana, 
calcula la distancia Manhattan a la semilla actual 
que sea máxima y actualiza la semilla a esa celda. 
*/
static void advance_seed_to_far_corner(int *seedRow, int *seedCol, int rows, int cols) {
    int sr=*seedRow, sc=*seedCol;
    int d00=sr+sc, d01=sr+(cols-1-sc), d10=(rows-1-sr)+sc, d11=(rows-1-sr)+(cols-1-sc);
    int r=0,c=0,maxd=d00;
    if (d01>maxd){
        maxd=d01;
        r=0;
        c=cols-1;
    }
    if (d10>maxd){
        maxd=d10;
        r=rows-1;
        c=0;
    }
    if (d11>maxd){
        maxd=d11;
        r=rows-1;
        c=cols-1;
    }
    *seedRow=r;
    *seedCol=c;
}


/*
Dibuja un grid de celdas mezclando texturas por celda y avanza latch
digamos que cada celda tiene una textura base y una overlay,
entonces para cada celda se calcula el pulso de mezcla y se aplica
desde una textura  a la siguiente textuea siguiendo una onda temporal 
basada en la distancia Manhattan a una semilla
*/
static void drawGridCycle(
    GLuint *tex_cycle, int tex_count,
    int global_stage_idx, float stage_time,
    int rows, int cols, int w, int h,
    int seedRow, int seedCol, bool hasSeed,
    float **grid,
    int **texBase,              // estado base por celda
    int **lastLatchedStage      // para no volver a "latch" en la misma etapa
) {
    if (rows <= 0 || cols <= 0) return;

    float cellW= (float)w / (float)cols;
    float cellH= (float)h / (float)rows;

    int   maxDist = (rows - 1) + (cols - 1);
    float dt = (maxDist > 0) ? (TOTAL_TIME / (float)maxDist) : 0.0f;

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {

            int d = hasSeed ? abs(r - seedRow) + abs(c - seedCol) : r + c;
            float start_t = d * dt;

            // Pulso 0→1→0 (subida y bajada de FADE_TIME cada una)
            float a_up = clamp01( (stage_time - start_t) / FADE_TIME );
            float a_dn = clamp01( (stage_time - (start_t + FADE_TIME)) / FADE_TIME );
            float alphaBase = a_up * (1.0f - a_dn);

            // Modulación por kernel
            float hval  = 0.5f * (grid ? (grid[r][c] + 1.0f) : 1.0f);
            float alpha = clamp01(alphaBase * (0.35f + 0.65f * hval));

            int baseIdx = texBase[r][c];
            int overIdx = (baseIdx + 1) % tex_count;

            float x = c * cellW, y = r * cellH;

            // Base (UNDER)
            glBindTexture(GL_TEXTURE_2D, tex_cycle[baseIdx]);
            glColor4f(1.f, 1.f, 1.f, 1.f);
            glBegin(GL_QUADS);
              glTexCoord2f(0.f, 0.f); glVertex2f(x,          y);
              glTexCoord2f(1.f, 0.f); glVertex2f(x+cellW,    y);
              glTexCoord2f(1.f, 1.f); glVertex2f(x+cellW,    y+cellH);
              glTexCoord2f(0.f, 1.f); glVertex2f(x,          y+cellH);
            glEnd();

            // Overlay 
            if (alpha > 0.f) {
                glBindTexture(GL_TEXTURE_2D, tex_cycle[overIdx]);
                glColor4f(1.f, 0.9f - 0.4f*hval, 0.9f - 0.4f*hval, alpha);
                glBegin(GL_QUADS);
                  glTexCoord2f(0.f, 0.f); glVertex2f(x,          y);
                  glTexCoord2f(1.f, 0.f); glVertex2f(x+cellW,    y);
                  glTexCoord2f(1.f, 1.f); glVertex2f(x+cellW,    y+cellH);
                  glTexCoord2f(0.f, 1.f); glVertex2f(x,          y+cellH);
                glEnd();
            }

            // cuando el pulso en esta celda ya terminó 
            // Fin del pulso local = start_t + 2*FADE_TIMEs
            if (stage_time >= start_t + 0.3f*FADE_TIME && lastLatchedStage[r][c] != global_stage_idx) {
                texBase[r][c] = overIdx;                  // avanza base a la siguiente texturas
                lastLatchedStage[r][c] = global_stage_idx; // marca que ya ancló en esta etapa
            }
        }
    }
    glDisable(GL_TEXTURE_2D);
}

//  Main 
/*
inicializa SDL y OpenGL
crea el grid
ejecuta el looop principal
inicializa SDL, las imagenes SDL_ttf para mostrar los FPS
determina las rows y cols del grid con best grid
en cada frame procesa eventos, actualiza la simulacion y dibuja
por ultimo imprime el resumen de la ejecucion y libera recursos
*/
int main(int argc, char**argv){
    // Semilla inicial para que la ola exista desde el primer frame
    int  seedRow = 0, seedCol = 0;
    bool hasSeed = true;


    if (argc < 2) { 
        fprintf(stderr, "Uso: %s <n>\n", argv[0]);
        return 1; 
    }
    int n = atoi(argv[1]);
    if (n <= 4) { 
        fprintf(stderr,"n debe ser > 4\n"); 
        return 1; 
    }

    // SDL/IMG
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { 
        fprintf(stderr,"SDL_Init: %s\n", SDL_GetError()); 
        return 1; 
    }
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) == 0) { 
        fprintf(stderr,"IMG_Init: %s\n", IMG_GetError()); 
        SDL_Quit(); 
        return 1; 
    }

    //
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    SDL_Window *win = SDL_CreateWindow("Screensaver",
                      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                      1536, 1024, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) { 
        fprintf(stderr,"SDL_CreateWindow: %s\n", SDL_GetError()); 
        IMG_Quit(); 
        SDL_Quit(); 
        return 1; 
    }

    SDL_GLContext glc = SDL_GL_CreateContext(win);
    if (!glc) { 
        fprintf(stderr,"SDL_GL_CreateContext: %s\n", SDL_GetError()); 
        SDL_DestroyWindow(win); 
        IMG_Quit(); 
        SDL_Quit(); 
        return 1; 
    }

    // VSync ON
    SDL_GL_SetSwapInterval(1);

    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f,0.0f,0.0f,0.0f);

    // Grid fijo
    int w0,h0; SDL_GetWindowSize(win,&w0,&h0);
    Grid g = best_grid(n, w0, h0);

    // filas apuntan a bloques contiguos
    float *A_back=NULL, *B_back=NULL;
    g.grid = alloc_grid2d(g.rows, g.cols, &A_back);
    float **B = alloc_grid2d(g.rows, g.cols, &B_back);
    for (int r=0; r<g.rows; ++r) for (int c=0; c<g.cols; ++c) g.grid[r][c]=0.0f;

    // estado de textura por celda 
    int *texBase_back=NULL, *latched_back=NULL;
    int **texBase = alloc_grid2d_i(g.rows, g.cols, &texBase_back);
    int **lastLatchedStage = alloc_grid2d_i(g.rows, g.cols, &latched_back);
    for (int r=0; r<g.rows; ++r) {
        for (int c=0; c<g.cols; ++c) {
            texBase[r][c] = 0;          // empieza con la primera textura del ciclo
            lastLatchedStage[r][c] = -1; // no anclado aún en ninguna etapa
        }
    }

    // TTF
    if (TTF_Init() != 0) { fprintf(stderr,"TTF_Init: %s\n", TTF_GetError());
        free_grid2d_i(lastLatchedStage, latched_back);
        free_grid2d_i(texBase, texBase_back);
        free_grid2d(B, B_back); 
        free_grid2d(g.grid, A_back);
        SDL_GL_DeleteContext(glc); 
        
        SDL_DestroyWindow(win); 
        IMG_Quit(); 
        SDL_Quit(); 
        return 1; 
    }

    TTF_Font* font = open_font_portable(18);
    if (!font) { fprintf(stderr,"No pude abrir fuente: %s\n", TTF_GetError());
        free_grid2d_i(lastLatchedStage, latched_back);
        free_grid2d_i(texBase, texBase_back);
        free_grid2d(B, B_back); free_grid2d(g.grid, A_back);
        TTF_Quit(); 
        SDL_GL_DeleteContext(glc); 
        SDL_DestroyWindow(win); 
        IMG_Quit(); SDL_Quit(); 
        return 1; 
    }

    // HUD FPS
    Uint32 fps_t0 = SDL_GetTicks(); 
    int fps_frames=0; 
    float fps_value=0.f;
    GLuint fps_tex=0; 
    int fps_w=0, fps_h=0; 
    char fps_msg[64]="FPS: 0.0";

    // Texturas
    GLuint dirtTex=load_texture(dirtTexture),   grassTex=load_texture(grassTexture);
    GLuint waterTex=load_texture(waterTexture), snowTex=load_texture(snowTexture);
    GLuint iceTex=load_texture(iceTexture),     lavaTex=load_texture(lavaTexture);
    GLuint diamondTex=load_texture(diamondTexture), obsidianTex=load_texture(obsidianTexture);
    GLuint endStoneTex=load_texture(endStoneTexture), jackTex=load_texture(jackoLanternTexture);

    if (!dirtTex||!grassTex||!waterTex||!snowTex||!iceTex||!lavaTex||!diamondTex||!obsidianTex||!endStoneTex||!jackTex){
        fprintf(stderr,"Fallo al cargar texturas\n");
        GLuint all[] = {dirtTex,grassTex,waterTex,iceTex,snowTex,lavaTex,diamondTex,obsidianTex,endStoneTex,jackTex};
        glDeleteTextures(sizeof all/sizeof all[0], all);
        TTF_CloseFont(font); TTF_Quit();
        free_grid2d_i(lastLatchedStage, latched_back);
        free_grid2d_i(texBase, texBase_back);
        free_grid2d(B, B_back); 
        free_grid2d(g.grid, A_back);
        SDL_GL_DeleteContext(glc); 
        SDL_DestroyWindow(win); 
        IMG_Quit(); 
        SDL_Quit(); 
        return 1;
    }
    GLuint tex_cycle[TEX_COUNT] = { dirtTex, grassTex, waterTex, iceTex, snowTex,
                                    lavaTex, diamondTex, obsidianTex, endStoneTex, jackTex };

    // Tiempos
    Uint32 t0_ms = SDL_GetTicks();
    bool running = true;
    int  prev_stage_idx = -1;

    Uint32 run_start_ms = SDL_GetTicks();
    unsigned long long total_frames = 0ULL;

    while (running) {
        // Eventos
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (e.key.keysym.sym == SDLK_r)      t0_ms = SDL_GetTicks();
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int w,h; SDL_GetWindowSize(win,&w,&h);
                float cellW=(float)w / g.cols, cellH=(float)h / g.rows;
                int col=(int)(e.button.x / cellW), row=(int)(e.button.y / cellH);
                col = clampi(col,0,g.cols-1); row = clampi(row,0,g.rows-1);
                if (seedCount < MAX_SEEDS) {
                    seeds[seedCount].row=row; seeds[seedCount].col=col;
                    seeds[seedCount].startTime=SDL_GetTicks(); seedCount++;
                }
                seedRow=row; seedCol=col; hasSeed=true;
            }

        }

        // Etapas 
        float elapsed = (SDL_GetTicks() - t0_ms) / 1000.0f;
        const float STAGE_DURATION = TOTAL_TIME + 2.0f * FADE_TIME; 
        int   stage_idx  = (int)floorf(elapsed / STAGE_DURATION) % TEX_COUNT;
        float stage_time = fmodf(elapsed, STAGE_DURATION);

        if (prev_stage_idx != stage_idx) {
            if (prev_stage_idx != -1) {
                advance_seed_to_far_corner(&seedRow,&seedCol,g.rows,g.cols);
                hasSeed = true;
            }
            prev_stage_idx = stage_idx;
        }

        
        const Uint32 nowTicks = SDL_GetTicks();
        const float t = nowTicks * 0.001f;
        const int rows = g.rows, cols = g.cols;
        const int scount = seedCount;


            // Ondas 
            
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    float val = 0.0f;

                    for (int s = 0; s < scount; ++s) {
                        float dr = (float)(r - seeds[s].row);
                        float dc = (float)(c - seeds[s].col);
                        float dist = sqrtf(dr*dr + dc*dc);
                        float elapsedSeed = (nowTicks - seeds[s].startTime) * 0.005f;
                        float wave  = sinf(0.25f*dist - elapsedSeed);
                        float atten = expf(-0.025f*dist);
                        val += wave * atten;
                    }

                    float tb  = sinf(0.061f*r + 0.043f*c + 0.90f*t);
                          tb += 0.5f  * cosf(0.113f*r - 0.027f*c + 1.73f*t);
                          tb += 0.25f * sinf(0.199f*r + 0.177f*c + 2.41f*t);
                    val += 0.8f * tb;

                    g.grid[r][c] = tanhf(val);
                }
            

            // Jacobi (ping-pong A<->B) — swap O(1) de punteros de filas
            for (int it = 0; it < DIFF_ITERS; ++it) {
                
                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        int r0 = (r>0? r-1:0), r2 = (r<rows-1? r+1:rows-1);
                        int c0 = (c>0? c-1:0),  c2 = (c<cols-1? c+1:cols-1);
                        float center = g.grid[r][c];
                        float up     = g.grid[r0][c];
                        float dn     = g.grid[r2][c];
                        float lf     = g.grid[r][c0];
                        float rt     = g.grid[r][c2];
                        B[r][c] = 0.2f * (center + up + dn + lf + rt);
                    }
                }
                
                { swap_rows(&g.grid, &B); } // O(1)
                
            }

            // Gauss 3×3
            #if USE_GAUSS_BLUR
            for (int it = 0; it < GAUSS_ITERS; ++it) {
                
                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        int r0 = (r>0? r-1:0), r2 = (r<rows-1? r+1:rows-1);
                        int c0 = (c>0? c-1:0),  c2 = (c<cols-1? c+1:cols-1);
                        float tl=g.grid[r0][c0], tc=g.grid[r0][c], tr=g.grid[r0][c2];
                        float cl=g.grid[r][c0],  cc=g.grid[r][c],  cr=g.grid[r][c2];
                        float bl=g.grid[r2][c0], bc=g.grid[r2][c], br=g.grid[r2][c2];
                        float sum = (tl+tr+bl+br) + 2.0f*(tc+cl+cr+bc) + 4.0f*cc;
                        B[r][c] = sum * (1.0f/16.0f);
                    }
                }
                
                { swap_rows(&g.grid, &B); } // O(1)
                
            }
            #endif
        } 

        // DIBUJO 
        int w,h; SDL_GetWindowSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glMatrixMode(GL_PROJECTION); 
        glLoadIdentity(); 
        glOrtho(0.0, (GLdouble)w, (GLdouble)h, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);  
        glLoadIdentity();
        glClear(GL_COLOR_BUFFER_BIT);

        drawGridCycle(tex_cycle, TEX_COUNT, stage_idx, stage_time,
                      g.rows, g.cols, w, h, seedRow, seedCol, hasSeed, g.grid,
                      texBase, lastLatchedStage);

        // HUD FPS
        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_t0 >= 500) {
            fps_value = (float)fps_frames * 1000.0f / (float)(now - fps_t0);
            fps_frames = 0; fps_t0 = now;
            snprintf(fps_msg, sizeof fps_msg, "FPS: %.1f", fps_value);
            if (fps_tex) glDeleteTextures(1, &fps_tex);
            SDL_Color white = {255,255,255,255};
            fps_tex = text_to_texture(font, fps_msg, white, &fps_w, &fps_h);
            printf("%s\n", fps_msg);
        }
        
        if (fps_tex) {
            float pad=10.f, bx=10.f, by=10.f, bw=fps_w+pad*2, bh=fps_h+pad*2;
            glDisable(GL_TEXTURE_2D);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.f,0.f,0.f,0.4f);
            glBegin(GL_QUADS);
              glVertex2f(bx,by); 
              glVertex2f(bx+bw,by); 
              glVertex2f(bx+bw,by+bh); 
              glVertex2f(bx,by+bh);
            glEnd();
            draw_textured_quad(fps_tex, bx+pad, by+pad, (float)fps_w, (float)fps_h);
        }

        SDL_GL_SwapWindow(win);
        //sumar 1 frame al total
        total_frames++;
    }

    Uint32 run_end_ms = SDL_GetTicks();
    double run_secs = (run_end_ms - run_start_ms) / 1000.0;
    double avg_fps  = (run_secs > 0.0) ? ((double)total_frames / run_secs) : 0.0;
    printf("=== RESUMEN ===\nDuración: %.2f s | Frames: %llu | FPS promedio: %.2f\n",
           run_secs, total_frames, avg_fps);

    // Limpieza
    GLuint all[] = {dirtTex, grassTex, waterTex, iceTex, snowTex, lavaTex, diamondTex, obsidianTex, endStoneTex, jackTex};
    glDeleteTextures(sizeof all/sizeof all[0], all);
    if (fps_tex) glDeleteTextures(1, &fps_tex);

    TTF_CloseFont(font); TTF_Quit();

    free_grid2d_i(lastLatchedStage, latched_back);
    free_grid2d_i(texBase, texBase_back);

    free_grid2d(B, B_back);
    free_grid2d(g.grid, A_back);

    SDL_GL_DeleteContext(glc);
    SDL_DestroyWindow(win);
    IMG_Quit(); 
    SDL_Quit();
    return 0;
}
