// libreriasss
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <omp.h>

#include <SDL2/SDL.h> //crear la ventana, gestiona teclado/mouse
#include <SDL2/SDL_image.h> // carga imagenes
#include <GL/gl.h> // funciones de OpenGL
#include <GL/glu.h> // utilidades de OpenGL
#include <SDL2/SDL_ttf.h> // textos

#include <math.h>
#include <time.h>

// configuracion
#define TEX_COUNT 10
#define MAX_SEEDS 100

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


static const float TOTAL_TIME = 11.0f; //tiempo de propagacion base en sec
static const float FADE_TIME = 0.40f; //tiempo de mezcla por celda, igual en sec

/** @struct Grid
 *  @brief Representa la malla de celdas y un buffer asociado para cómputos por celda.
 *
 *  @var Grid::rows  Número de filas.
 *  @var Grid::cols  Número de columnas.
 *  @var Grid::grid  Matriz [rows][cols] de valores float (reservada dinámicamente).
 *
 *  @note En este programa, grid se usa como buffer de cómputo (kernel paralelo)
 *        y se reserva por filas (puntero a puntero). Debe liberarse tras su uso.
 */
typedef struct {
    int rows;
    int cols;
    float** grid; // puntero a puntero
} Grid;


/** @struct CellInfo
 *  @brief Información precalculada por celda para render (posición y alpha).
 *
 *  @var CellInfo::x      Coordenada X (en píxeles) de la esquina superior-izquierda.
 *  @var CellInfo::y      Coordenada Y (en píxeles) de la esquina superior-izquierda.
 *  @var CellInfo::alpha  Factor de mezcla [0..1] de la textura OVER sobre UNDER.
 */
typedef struct {
    float x, y;
    float alpha;
} CellInfo;


/** @struct Seed
 *  @brief Semilla de propagación para iniciar una onda desde una celda específica.
 *
 *  @var Seed::row        Fila de la celda semilla.
 *  @var Seed::col        Columna de la celda semilla.
 *  @var Seed::startTime  Tiempo SDL (ms) cuando inició la propagación desde esta semilla.
 */
typedef struct {
    int row;
    int col;
    Uint32 startTime; // momento en que comenzó la propagación de esta semilla
} Seed;

/** @brief Arreglo global de semillas activas y contador asociado.
 *
 * @var seeds      Buffer de hasta MAX_SEEDS semillas agregadas mediante clicks.
 * @var seedCount  Número de semillas almacenadas actualmente (0..MAX_SEEDS).
 */
Seed seeds[MAX_SEEDS];
int seedCount = 0;


/** @brief Calcula la mejor malla (rows x cols) para acomodar @p n elementos
 *         intentando aproximar el aspecto de @p w:@p h (ancho:alto).
 *
 * Recorre divisores de @p n y escoge (rows, cols) cuyo ratio cols/rows
 * minimiza el error contra (w/h).
 *
 * @param n  Número total de celdas/elementos a acomodar (n > 0).
 * @param w  Ancho de la ventana en píxeles.
 * @param h  Alto de la ventana en píxeles.
 * @return   Grid {rows, cols}. Si w<=0 o h<=0, retorna {1, n}.
 *           Si n<=0, retorna {0,0}.
 *
 * @note   Solo calcula la malla ideal; no crea ni redimensiona ventana.
 */
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

/** @brief Carga una imagen (PNG/JPG) desde disco y la sube como textura OpenGL.
 *
 * Usa SDL_image para decodificar y configura parámetros de filtrado y wrap.
 *
 * @param path  Ruta al archivo de imagen.
 * @return      ID de textura OpenGL (GLuint) válido si tuvo éxito; 0 si falla.
 *
 * @pre   IMG_Init() debe haber sido llamado con soporte para PNG/JPG.
 * @pre   Debe existir contexto OpenGL actual (SDL_GL_CreateContext).
 * @post  La textura queda ligada a GL_TEXTURE_2D con filtros LINEAR y CLAMP.
 *
 * @warning Devuelve 0 en error y escribe el motivo en stderr.
 * @note    El formato de subida se elige según BytesPerPixel y máscara de R.
 */
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


/** @brief Renderiza texto con SDL_ttf a un SDL_Surface y lo sube como textura.
 *
 * Genera una textura RGBA con el texto @p msg usando la @p font y @p col.
 *
 * @param font   Fuente TTF abierta (TTF_OpenFont).
 * @param msg    Cadena UTF-8 a rasterizar.
 * @param col    Color del texto (SDL_Color).
 * @param out_w  [out] Ancho en píxeles del bitmap generado (puede ser NULL).
 * @param out_h  [out] Alto en píxeles del bitmap generado (puede ser NULL).
 * @return       ID de textura OpenGL (GLuint) con el texto; 0 si falla.
 *
 * @pre   TTF_Init() y una @p font válida.
 * @pre   Contexto OpenGL actual.
 * @post  Textura GL_RGBA con filtros LINEAR y CLAMP.
 *
 * @note  Útil para HUD (p. ej., contador de FPS). El fondo es transparente.
 */
// Renderiza texto con SDL_ttf a una textura OpenGL
static GLuint text_to_texture(TTF_Font* font, const char* msg,
                              SDL_Color col, int* out_w, int* out_h)
{
    //Renderiza el texto
    SDL_Surface* src = TTF_RenderUTF8_Blended(font, msg, col);
    if (!src) return 0;

    //convertirlo a un formato conocido (ABGR8888)
    SDL_Surface* s = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(src);
    if (!s) return 0;

    // Sube como GL_RGBA (memoria ABGR8888 en little-endian corresponde a GL_RGBA)
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); 
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->w, s->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (out_w) *out_w = s->w;
    if (out_h) *out_h = s->h;

    SDL_FreeSurface(s);
    return tex;
}

/** @brief Dibuja un quad 2D texturizado en coordenadas de pantalla.
 *
 * Asume una proyección ortográfica (0..w, 0..h) ya configurada.
 *
 * @param tex  ID de textura OpenGL a usar (GL_TEXTURE_2D).
 * @param x    Coordenada X de la esquina superior izquierda.
 * @param y    Coordenada Y de la esquina superior izquierda.
 * @param w    Ancho del quad.
 * @param h    Alto del quad.
 *
 * @pre   Contexto OpenGL válido, proyección/modelview configuradas.
 * @post  Restablece GL_TEXTURE_2D a deshabilitado.
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


/** @brief Abre una fuente TTF de forma "portable".
 *
 * Intenta primero encontrar "assets/DejaVuSans.ttf" relativo al ejecutable,
 * y si falla, prueba una ruta de sistema Linux estándar.
 *
 * @param ptsize  Tamaño de la fuente en puntos.
 * @return        Puntero a TTF_Font abierto; NULL si no pudo abrir ninguna.
 *
 * @pre   TTF_Init() debe haberse llamado.
 * @note  Adecuado para empaquetar la fuente junto al binario.
 */
static TTF_Font* open_font_portable(int ptsize) {
    const char* rel = "assets/DejaVuSans.ttf";
    char *base = SDL_GetBasePath();
    TTF_Font* f = NULL;

    if (base) {
        size_t need = strlen(base) + strlen(rel) + 1;
        char *p = (char*)malloc(need);
        if (p) {
            snprintf(p, need, "%s%s", base, rel);
            f = TTF_OpenFont(p, ptsize);
            free(p);
        }
        SDL_free(base);
        if (f) return f;
    }

    f = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", ptsize);
    if (f) return f;
}


/** @brief Mueve una semilla (celda) a la esquina Manhattan más lejana.
 *
 * Dado (@p seedRow, @p seedCol) dentro de [0..rows-1]x[0..cols-1], calcula
 * la esquina cuya distancia Manhattan a la semilla actual sea máxima y
 * actualiza la semilla a esa celda.
 *
 * @param seedRow  [in,out] Fila actual de la semilla; se sobrescribe.
 * @param seedCol  [in,out] Columna actual de la semilla; se sobrescribe.
 * @param rows     Número de filas del grid.
 * @param cols     Número de columnas del grid.
 *
 * @pre   0 <= *seedRow < rows y 0 <= *seedCol < cols, con rows,cols > 0.
 * @post  (*seedRow,*seedCol) queda en { (0,0), (0,cols-1), (rows-1,0), (rows-1,cols-1) }.
 */
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


/** @brief Dibuja una malla de celdas que hacen "fade" desde una textura a la siguiente
 *         siguiendo una onda temporal basada en distancia Manhattan a una semilla.
 *
 * Este variante precalcula por celda su posición y alpha en un buffer temporal
 * (@c CellInfo[]). El cálculo de alpha sigue la lógica:
 *   start_t = d * dt  con dt = TOTAL_TIME / maxDist,
 *   alpha   = clamp((stage_time - start_t) / FADE_TIME, 0, 1), con alpha=1 si d==0.
 *
 * @param tex_cycle   Arreglo circular de texturas de tamaño @p tex_count.
 * @param tex_count   Número total de texturas en el ciclo (>=2).
 * @param stage_idx   Índice de la textura base (UNDER) para la etapa actual.
 * @param stage_time  Tiempo transcurrido dentro de la etapa [0, TOTAL_TIME+FADE_TIME).
 * @param rows        Filas del grid.
 * @param cols        Columnas del grid.
 * @param w           Ancho de la ventana en píxeles.
 * @param h           Alto de la ventana en píxeles.
 * @param seedRow     Fila de la semilla (origen de la ola si @p hasSeed es true).
 * @param seedCol     Columna de la semilla (origen de la ola si @p hasSeed es true).
 * @param hasSeed     Si true, usa (seedRow,seedCol); si false, usa (0,0) como semilla.
 *
 * @pre   Contexto OpenGL válido y proyección ortográfica configurada.
 * @pre   Texturas de @p tex_cycle válidas y enlazables a GL_TEXTURE_2D.
 * @post  Dibuja todo el grid en el framebuffer actual.
 *
 * @note  UNDER = tex_cycle[stage_idx], OVER = tex_cycle[(stage_idx+1)%tex_count].
 * @note  Reserva y libera un buffer temporal de @c CellInfo por frame.
 */
static void drawGridCycle(
    GLuint *tex_cycle, int tex_count,
    int stage_idx, float stage_time,
    int rows, int cols, int w, int h,
    int seedRow, int seedCol, bool hasSeed
) {
    if (rows <= 0 || cols <= 0) return;

    float cellW= (float)w / (float)cols;
    float cellH= (float)h / (float)rows;

    int maxDist = (rows - 1) + (cols - 1);
    float dt = (maxDist > 0) ? (TOTAL_TIME / (float)maxDist) : 0.0f;

    GLuint UNDER = tex_cycle[stage_idx];
    GLuint OVER  = tex_cycle[(stage_idx + 1) % tex_count];

    int total = rows * cols;
    CellInfo *cells = (CellInfo*)malloc(sizeof(CellInfo) * total);

    for (int r = 0; r < rows; ++r) {
        
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;

            int d = hasSeed ? abs(r - seedRow) + abs(c - seedCol) : r + c;
            float start_t = d * dt;

            float alpha = 0.f;
            if (d == 0) {
                alpha = 1.f;
            } else if (stage_time > start_t) {
                alpha = (FADE_TIME > 0.f) ? (stage_time - start_t) / FADE_TIME : 1.f;
                if (alpha > 1.f) alpha = 1.f;
                if (alpha < 0.f) alpha = 0.f;
            }

            cells[idx].x = c * cellW;
            cells[idx].y = r * cellH;
            cells[idx].alpha = alpha;
        }
    }

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (int i = 0; i < total; i++) {
        float x = cells[i].x;
        float y = cells[i].y;
        float alpha = cells[i].alpha;

        // UNDER
        glBindTexture(GL_TEXTURE_2D, UNDER);
        glColor4f(1.f, 1.f, 1.f, 1.f);
        glBegin(GL_QUADS);
            glTexCoord2f(0.f, 0.f); glVertex2f(x,          y);
            glTexCoord2f(1.f, 0.f); glVertex2f(x+cellW,    y);
            glTexCoord2f(1.f, 1.f); glVertex2f(x+cellW,    y+cellH);
            glTexCoord2f(0.f, 1.f); glVertex2f(x,          y+cellH);
        glEnd();

        // OVER
        if (alpha > 0.f) {
            glBindTexture(GL_TEXTURE_2D, OVER);
            glColor4f(1.f, 1.f, 1.f, alpha);
            glBegin(GL_QUADS);
                glTexCoord2f(0.f, 0.f); glVertex2f(x,          y);
                glTexCoord2f(1.f, 0.f); glVertex2f(x+cellW,    y);
                glTexCoord2f(1.f, 1.f); glVertex2f(x+cellW,    y+cellH);
                glTexCoord2f(0.f, 1.f); glVertex2f(x,          y+cellH);
            glEnd();
        }
    }

    glDisable(GL_TEXTURE_2D);
    free(cells);}


/** @brief Punto de entrada: inicializa SDL/GL/TTF, carga texturas, ejecuta el loop,
 *         maneja eventos y limpia recursos. Implementación **paralela** (OpenMP).
 *
 *  - @p n = número de celdas totales del grid (debe ser > 4).
 *  - Teclas: ESC para salir, 'g' alterna líneas (placeholder), 'r' reinicia ola.
 *  - Click izquierdo: agrega una semilla (row,col) a @c seeds[] y reinicia localmente.
 *
 * @param argc  Recuento de argumentos.
 * @param argv  Vector de argumentos.
 * @return      0 si todo OK; 1 en caso de error de inicialización/recursos.
 *
 * @details
 *  - Configura OpenMP con @c omp_set_num_threads(omp_get_max_threads()).
 *  - Si rows*cols > 5000, se paraleliza el **kernel por celda**:
 *      @code
 *      #pragma omp parallel for collapse(2) schedule(dynamic, 8)
 *      for (r=0..rows-1) for (c=0..cols-1) {
 *          // acumula ondas por semilla: distancias + seno con tiempo
 *          g.grid[r][c] = value_clamped;
 *      }
 *      @endcode
 *    *collapse(2)* combina ambos bucles y *schedule(dynamic,8)* reparte
 *    bloques de 8 iteraciones para balancear carga cuando hay celdas “más caras”.
 *  - Calcula FPS cada 0.5 s y los muestra en HUD (textura de texto) y consola.
 *  - La semilla “automática” salta a la esquina más lejana al cambiar de etapa.
 *
 * @pre   Requiere enlazar con OpenMP (-fopenmp) y contexto SDL/OpenGL/TTF válido.
 * @post  Libera texturas, fuente, contexto GL y subsistemas SDL/IMG/TTF.
 *
 * @warning Se reserva @c g.grid por frame; libera cada fila y el vector para evitar fugas.
 * @warning Asegura que TEX_COUNT coincide con el tamaño de @c tex_cycle[].
 */
int main(int argc, char**argv){
    int seedRow = 0;
    int seedCol = 0;
    bool hasSeed = false;

    omp_set_num_threads(omp_get_max_threads()); // usa todos los cores
    // si es más de un argumento
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <n>\n", argv[0]);
        return 1;
    }

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
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // Tamaño inicial y grid fijo
    int w0, h0; SDL_GetWindowSize(win, &w0, &h0);
    Grid g = best_grid(n, w0, h0);  // lo fijamos al inicio

    if (TTF_Init() != 0) { fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError()); SDL_GL_DeleteContext(glc); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit(); return 1; }
    TTF_Font* font = open_font_portable(18);
    if (!font) { fprintf(stderr, "No pude abrir ninguna fuente: %s\n", TTF_GetError()); TTF_Quit(); SDL_GL_DeleteContext(glc); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit(); return 1; }

    // FPS state
    Uint32 fps_t0 = SDL_GetTicks();
    int    fps_frames = 0;
    float  fps_value  = 0.f;
    GLuint fps_tex    = 0;
    int    fps_w = 0, fps_h = 0;
    char   fps_msg[64] = "FPS: 0.0";

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
        TTF_CloseFont(font); TTF_Quit();
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
        g.grid = malloc(g.rows * sizeof(float*));

        // #pragma omp parallel for 
        for (int r = 0; r < g.rows; r++) {
            g.grid[r] = malloc(g.cols * sizeof(float));
        }


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

                int col = (int)(e.button.x / cellW);
                int row = (int)(e.button.y / cellH);
                if (col < 0) col = 0;
                if (col >= g.cols) col = g.cols - 1;
                if (row < 0) row = 0;
                if (row >= g.rows) row = g.rows - 1;

                if (seedCount < MAX_SEEDS) {
                    seeds[seedCount].row = row;
                    seeds[seedCount].col = col;
                    seeds[seedCount].startTime = SDL_GetTicks();
                    seedCount++;
                }

                seedRow = row;
                seedCol = col;
                hasSeed = true;
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

        // FPS update cada 0.5s
        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_t0 >= 500) {
            fps_value  = (float)fps_frames * 1000.0f / (float)(now - fps_t0);
            fps_frames = 0;
            fps_t0     = now;
            printf("FPS: %.1f\n", fps_value);

            snprintf(fps_msg, sizeof fps_msg, "FPS: %.1f", fps_value);
            if (fps_tex) glDeleteTextures(1, &fps_tex);
            SDL_Color white = {255,255,255,255};
            fps_tex = text_to_texture(font, fps_msg, white, &fps_w, &fps_h);
        }


        // Dibujo
        glViewport(0, 0, w, h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, (GLdouble)w, (GLdouble)h, 0.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClear(GL_COLOR_BUFFER_BIT);
        

        if (g.rows * g.cols > 5000) { // solo se aplica cuando es necesario

            // se busca mejorar la eficiencia haciendo calculos fuera de los bucles que se necesitan 1 sola vez
            Uint32 nowTicks = SDL_GetTicks();

            // Este bloque usa OpenMP para paralelizar el doble bucle sobre filas y columnas del grid. La directiva `#pragma omp parallel for collapse(2) schedule(dynamic, 8)` 
            // combina ambos bucles en uno solo (`collapse(2)`) y reparte dinámicamente las iteraciones entre los hilos en bloques de 8 (`schedule(dynamic, 8)`), de modo que cada hilo 
            // procesa varias celdas del grid en paralelo y, al terminar, toma más iteraciones disponibles, mejorando la eficiencia cuando algunas celdas tardan más en calcularse.

            #pragma omp parallel for collapse(2) schedule(dynamic, 8)
            for (int r = 0; r < g.rows; r++) {
                for (int c = 0; c < g.cols; c++) {
                    float value = 0.0f;

                    for (int s = 0; s < seedCount; s++) {
                        Uint32 elapsed = nowTicks - seeds[s].startTime; 
                        float dist = sqrtf((r - seeds[s].row)*(r - seeds[s].row) +
                                        (c - seeds[s].col)*(c - seeds[s].col));
                        float wave = sinf(dist - elapsed * 0.01f);
                        value += wave;
                    }

                    if (value > 1.0f) value = 1.0f;
                    if (value < -1.0f) value = -1.0f;

                    g.grid[r][c] = value;
                }
            }
        }else{ // cuando es necesario calcular en paralelo, no se usa para que el resultado no sea contraproducente
            for (int r = 0; r < g.rows; r++) {
                for (int c = 0; c < g.cols; c++) {
                    float value = 0.0f;

                    for (int s = 0; s < seedCount; s++) {
                        Uint32 elapsed = SDL_GetTicks() - seeds[s].startTime;
                        float dist = sqrtf((r - seeds[s].row)*(r - seeds[s].row) +
                                        (c - seeds[s].col)*(c - seeds[s].col));
                        float wave = sinf(dist - elapsed * 0.01f); // velocidad ajustable
                        value += wave;
                    }

                    // Limitar valores si quieres
                    if (value > 1.0f) value = 1.0f;
                    if (value < -1.0f) value = -1.0f;

                    g.grid[r][c] = value;
                }
            }SDL_GetTicks;
        }

        drawGridCycle(tex_cycle, TEX_COUNT, stage_idx, stage_time,
                    g.rows, g.cols, w, h,
                    seedRow, seedCol, hasSeed);

        // HUD FPS
        if (fps_tex) {
            float pad = 10.f;
            float bx = 10.f, by = 10.f, bw = fps_w + pad*2, bh = fps_h + pad*2;

            glDisable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.f, 0.f, 0.f, 0.4f);
            glBegin(GL_QUADS);
              glVertex2f(bx,     by);
              glVertex2f(bx+bw,  by);
              glVertex2f(bx+bw,  by+bh);
              glVertex2f(bx,     by+bh);
            glEnd();

            draw_textured_quad(fps_tex, bx + pad, by + pad, (float)fps_w, (float)fps_h);
        }

        SDL_GL_SwapWindow(win);
    }

    // Limpieza
    GLuint all[] = {dirtTex, grassTex, waterTex, iceTex, snowTex, lavaTex, diamondTex, obsidianTex, endStoneTex, jackTex};
    glDeleteTextures(sizeof all / sizeof all[0], all);
    if (fps_tex) glDeleteTextures(1, &fps_tex);

    TTF_CloseFont(font);
    TTF_Quit();

    SDL_GL_DeleteContext(glc);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    return 0;
}