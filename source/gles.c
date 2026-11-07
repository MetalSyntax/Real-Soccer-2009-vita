// OpenGL ES 1.1 de iOS -> vitaGL.
//
// Base: gles.c de Inotia2-vita. El juego dibuja en el renderbuffer que EAGLContext le da para su
// CAEAGLLayer (320x480 vertical en los juegos de esta época, aunque se jueguen apaisados: rotan
// la proyección ellos mismos). Acá ese renderbuffer es un FBO de vitaGL con textura + profundidad;
// viewport, scissor y glCopyTexImage2D siguen en el espacio de iOS. gles_present lo dibuja girado
// (según port_config.h) y escalado a 960x544.
//
// FBOs OES del juego: el que tiene el renderbuffer del drawable como color = nuestro FBO de pantalla;
// los demás (render-to-texture) se crean de verdad en vitaGL.
// GL_FIXED: vitaGL no lo acepta en arrays; se convierte a float al dibujar (también desde VBOs,
// con una copia en CPU de los datos de cada buffer).
#include <math.h>
#include <psp2/kernel/processmgr.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>

#include "gles.h"
#include "hle.h"
#include "imports.h"
#include "log.h"

#define SCR_W 960
#define SCR_H 544
#define GL_FRAMEBUFFER_COMPLETE_OES 0x8CD5
#define GL_RENDERBUFFER_WIDTH_OES 0x8D42
#define GL_RENDERBUFFER_HEIGHT_OES 0x8D43
#define GL_RENDERBUFFER_INTERNAL_FORMAT_OES 0x8D44
#define GL_FRAMEBUFFER_BINDING_OES 0x8CA6
#define GL_RENDERBUFFER_BINDING_OES 0x8CA7
#define GL_COLOR_ATTACHMENT0_OES 0x8CE0
#define GL_DEPTH_ATTACHMENT_OES 0x8D00
#define GL_MATRIX_PALETTE_OES 0x8840

int fb_w = 320, fb_h = 480;
static GLuint scr_fbo, scr_tex, scr_depth;

// ---------------------------------------------------------------- arrays (GL_FIXED -> float)

typedef struct {
    GLint size;
    GLenum type;
    GLsizei stride;
    const void *ptr;
    GLuint vbo; // buffer ligado al fijar el puntero (ptr es un offset)
} array_ptr;
enum { A_VTX, A_NRM, A_COL, A_TC0, A_TC1, A_N };
static array_ptr arr[A_N];
static int client_unit;
static GLint env_mode = GL_MODULATE;
static GLfixed color[4] = {0x10000, 0x10000, 0x10000, 0x10000};
static GLuint bound_array_buffer, bound_element_buffer;

// Copias en CPU de los VBO (para convertir GL_FIXED y para índices en glDrawElements).
#define MAX_VBO 4096
static struct {
    GLuint id;
    void *data;
    int size;
} vbo_copy[MAX_VBO];
static void *vbo_data(GLuint id) {
    for (int i = 0; i < MAX_VBO; i++)
        if (vbo_copy[i].id == id)
            return vbo_copy[i].data;
    return NULL;
}
static void g_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    GLuint id = target == GL_ARRAY_BUFFER ? bound_array_buffer : bound_element_buffer;
    int free_slot = -1;
    for (int i = 0; i < MAX_VBO; i++) {
        if (vbo_copy[i].id == id) {
            free_slot = i;
            break;
        }
        if (free_slot < 0 && !vbo_copy[i].id)
            free_slot = i;
    }
    if (free_slot >= 0 && id) {
        vbo_copy[free_slot].id = id;
        vbo_copy[free_slot].data = realloc(vbo_copy[free_slot].data, size);
        vbo_copy[free_slot].size = size;
        if (data)
            memcpy(vbo_copy[free_slot].data, data, size);
    }
    glBufferData(target, size, data, usage);
}
static void g_glBufferSubData(GLenum target, GLintptr off, GLsizeiptr size, const void *data) {
    GLuint id = target == GL_ARRAY_BUFFER ? bound_array_buffer : bound_element_buffer;
    for (int i = 0; i < MAX_VBO; i++)
        if (vbo_copy[i].id == id && off + size <= vbo_copy[i].size)
            memcpy((uint8_t *)vbo_copy[i].data + off, data, size);
    glBufferSubData(target, off, size, data);
}
static void g_glBindBuffer(GLenum target, GLuint id) {
    if (target == GL_ARRAY_BUFFER)
        bound_array_buffer = id;
    else
        bound_element_buffer = id;
    glBindBuffer(target, id);
}
static void g_glDeleteBuffers(GLsizei n, const GLuint *ids) {
    for (int k = 0; k < n; k++)
        for (int i = 0; i < MAX_VBO; i++)
            if (vbo_copy[i].id == ids[k]) {
                free(vbo_copy[i].data);
                vbo_copy[i].data = NULL;
                vbo_copy[i].id = 0;
            }
    glDeleteBuffers(n, ids);
}

static float *conv_buf[A_N];
static int conv_cap[A_N];

static const void *fixed_to_float(const array_ptr *a, int n, int slot) {
    int need = n * a->size;
    if (need > conv_cap[slot]) {
        conv_buf[slot] = realloc(conv_buf[slot], need * sizeof(float));
        conv_cap[slot] = need;
    }
    const uint8_t *base = a->vbo ? (const uint8_t *)vbo_data(a->vbo) + (uintptr_t)a->ptr : a->ptr;
    if (!base)
        return NULL;
    int stride = a->stride ? a->stride : a->size * 4;
    float *dst = conv_buf[slot];
    for (int i = 0; i < n; i++) {
        const int32_t *src = (const int32_t *)(base + i * stride);
        for (int c = 0; c < a->size; c++)
            dst[i * a->size + c] = src[c] * (1.0f / 65536.0f);
    }
    return dst;
}

static void set_pointer(int k, int n) {
    array_ptr *a = &arr[k];
    if (!a->size)
        return;
    const void *p = a->ptr;
    GLenum type = a->type;
    GLsizei stride = a->stride;
    int unbind = 0;
    if (type == GL_FIXED) {
        if (!n)
            return; // se convierte al dibujar
        p = fixed_to_float(a, n, k);
        type = GL_FLOAT;
        stride = 0;
        unbind = 1; // puntero de CPU: sin VBO ligado
    } else if (a->vbo != bound_array_buffer) {
        glBindBuffer(GL_ARRAY_BUFFER, a->vbo);
    }
    if (unbind && bound_array_buffer)
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    switch (k) {
    case A_VTX:
        glVertexPointer(a->size, type, stride, p);
        break;
    case A_NRM:
        glNormalPointer(type, stride, p);
        break;
    case A_COL:
        glColorPointer(a->size, type, stride, p);
        break;
    default:
        glClientActiveTexture(GL_TEXTURE0 + (k - A_TC0));
        glTexCoordPointer(a->size, type, stride, p);
        glClientActiveTexture(GL_TEXTURE0 + client_unit);
    }
    if (unbind && bound_array_buffer)
        glBindBuffer(GL_ARRAY_BUFFER, bound_array_buffer);
    else if (!unbind && a->vbo != bound_array_buffer)
        glBindBuffer(GL_ARRAY_BUFFER, bound_array_buffer);
}

static void g_glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) {
    arr[A_VTX] = (array_ptr){size, type, stride, ptr, bound_array_buffer};
    set_pointer(A_VTX, 0);
}
static void g_glNormalPointer(GLenum type, GLsizei stride, const void *ptr) {
    arr[A_NRM] = (array_ptr){3, type, stride, ptr, bound_array_buffer};
    set_pointer(A_NRM, 0);
}
static void g_glColorPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) {
    arr[A_COL] = (array_ptr){size, type, stride, ptr, bound_array_buffer};
    set_pointer(A_COL, 0);
}
static void g_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) {
    arr[A_TC0 + client_unit] = (array_ptr){size, type, stride, ptr, bound_array_buffer};
    set_pointer(A_TC0 + client_unit, 0);
}
static void g_glClientActiveTexture(GLenum unit) {
    client_unit = (unit - GL_TEXTURE0) & 1;
    glClientActiveTexture(unit);
}

static void convert_fixed(int n) {
    for (int k = 0; k < A_N; k++)
        if (arr[k].size && arr[k].type == GL_FIXED)
            set_pointer(k, n);
}

static uint32_t draw_calls;
uint32_t gles_get_draw_calls(void) { return draw_calls; }
// ponytail: perfil temporal (µs acumulados) para la corrida 013 de rendimiento; quitar al optimizar.
static uint64_t t_conv, t_draw, t_present;
#define T_NOW() sceKernelGetProcessTimeWide()
void gles_prof(uint64_t out[3]) {
    out[0] = t_conv, out[1] = t_draw, out[2] = t_present;
    t_conv = t_draw = t_present = 0;
}

static int skin(int n);
static void unskin(void);
static void g_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    draw_calls++;
    uint64_t t0 = T_NOW();
    convert_fixed(first + count);
    int skinned = skin(first + count);
    uint64_t t1 = T_NOW();
    glDrawArrays(mode, first, count);
    t_draw += T_NOW() - t1, t_conv += t1 - t0;
    if (skinned)
        unskin();
}

static void g_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *idx) {
    draw_calls++;
    uint64_t t0 = T_NOW();
    extern int palette_active(void);
    int need_fixed = palette_active();
    for (int k = 0; k < A_N; k++)
        need_fixed |= arr[k].size && arr[k].type == GL_FIXED;
    int maxi = 0;
    if (need_fixed) {
        const void *ip = bound_element_buffer ? (const uint8_t *)vbo_data(bound_element_buffer) + (uintptr_t)idx : idx;
        for (int i = 0; ip && i < count; i++) {
            int v = type == GL_UNSIGNED_BYTE ? ((const uint8_t *)ip)[i] : ((const uint16_t *)ip)[i];
            if (v > maxi)
                maxi = v;
        }
        convert_fixed(maxi + 1);
    }
    int skinned = skin(maxi + 1);
    uint64_t t1 = T_NOW();
    glDrawElements(mode, count, type, idx);
    t_draw += T_NOW() - t1, t_conv += t1 - t0;
    if (skinned)
        unskin();
}

// ---------------------------------------------------------------- estado *x / variantes

static void g_glColor4x(GLfixed r, GLfixed g, GLfixed b, GLfixed a) {
    color[0] = r, color[1] = g, color[2] = b, color[3] = a;
    glColor4x(r, g, b, a);
}
static void g_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    color[0] = r * 65536, color[1] = g * 65536, color[2] = b * 65536, color[3] = a * 65536;
    glColor4f(r, g, b, a);
}
static void g_glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    g_glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}
static void g_glTexEnvi(GLenum target, GLenum pname, GLint v) {
    if (pname == GL_TEXTURE_ENV_MODE)
        env_mode = v;
    glTexEnvi(target, pname, v);
}
static void g_glTexEnvf(GLenum target, GLenum pname, GLfloat v) {
    // los enums llegan como float
    if (pname == GL_TEXTURE_ENV_MODE || pname == GL_COMBINE_RGB || pname == GL_COMBINE_ALPHA ||
        (pname >= GL_SRC0_RGB && pname <= GL_OPERAND2_ALPHA))
        g_glTexEnvi(target, pname, (GLint)v);
    else
        glTexEnvf(target, pname, v);
}
static void g_glTexEnvx(GLenum target, GLenum pname, GLfixed v) {
    if (pname == GL_RGB_SCALE || pname == GL_ALPHA_SCALE)
        glTexEnvf(target, pname, v / 65536.0f);
    else
        g_glTexEnvi(target, pname, v); // enums sin escalar
}
static void g_glTexEnvxv(GLenum target, GLenum pname, const GLfixed *v) {
    if (pname == GL_TEXTURE_ENV_COLOR) {
        float f[4] = {v[0] / 65536.0f, v[1] / 65536.0f, v[2] / 65536.0f, v[3] / 65536.0f};
        glTexEnvfv(target, pname, f);
    } else {
        g_glTexEnvx(target, pname, v[0]);
    }
}
static void g_glTexParameterx(GLenum target, GLenum pname, GLfixed v) { glTexParameteri(target, pname, v); }
static void g_glTexParameterf(GLenum target, GLenum pname, GLfloat v) {
    if (pname == GL_TEXTURE_MAX_ANISOTROPY_EXT)
        glTexParameterf(target, pname, v);
    else
        glTexParameteri(target, pname, (GLint)v);
}
static void g_glLightModelx(GLenum pname, GLfixed v) { glLightModelxv(pname, &v); }
static void g_glLightModelf(GLenum pname, GLfloat v) { glLightModelfv(pname, &v); }
static void g_glLightf(GLenum l, GLenum pname, GLfloat v) { glLightfv(l, pname, &v); }
static void g_glLightx(GLenum l, GLenum pname, GLfixed v) { g_glLightf(l, pname, v / 65536.0f); }
static void g_glMaterialxv(GLenum f, GLenum p, const GLfixed *v) {
    float x[4] = {v[0] / 65536.0f, v[1] / 65536.0f, v[2] / 65536.0f, v[3] / 65536.0f};
    glMaterialfv(f, p, x);
}
static void g_glGetFixedv(GLenum pname, GLfixed *out) {
    float f[16];
    int n = 1;
    if (pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX || pname == GL_TEXTURE_MATRIX)
        n = 16;
    else if (pname == GL_CURRENT_COLOR) {
        memcpy(out, color, sizeof(color));
        return;
    }
    glGetFloatv(pname, f);
    for (int i = 0; i < n; i++)
        out[i] = (GLfixed)(f[i] * 65536.0f);
}
static void g_glGetLightxv(GLenum light, GLenum pname, GLfixed *out) { memset(out, 0, 4 * sizeof(GLfixed)); }
static void g_glGetPointerv(GLenum pname, void **out) {
    if (pname == GL_VERTEX_ARRAY_POINTER)
        *out = (void *)arr[A_VTX].ptr;
    else if (pname == GL_TEXTURE_COORD_ARRAY_POINTER)
        *out = (void *)arr[A_TC0 + client_unit].ptr;
    else
        glGetPointerv(pname, out);
}

// ---------------------------------------------------------------- GL_OES_matrix_palette (skinning)
//
// vitaGL no lo tiene. Se emula en CPU: las matrices de paleta se llevan acá (glMatrixMode
// MATRIX_PALETTE + glCurrentPaletteMatrixOES + glLoad*/Mult*/Translate/...); al dibujar con la paleta
// activa, cada vértice (y normal) se transforma como sum(w_k * P[i_k] * v) y se dibuja con modelview
// identidad (en OES_matrix_palette la paleta reemplaza a la modelview; la proyección se aplica igual).
#define MAX_PALETTE 32
static float palette[MAX_PALETTE][16];
static int cur_palette, palette_mode, palette_on, mi_on, wt_on;
static GLenum real_matrix_mode = GL_MODELVIEW;
static array_ptr mi_arr, wt_arr;

static void mat_mul(float *r, const float *a, const float *b) { // r = a * b (column-major)
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++)
            t[c * 4 + rr] = a[rr] * b[c * 4] + a[4 + rr] * b[c * 4 + 1] + a[8 + rr] * b[c * 4 + 2] + a[12 + rr] * b[c * 4 + 3];
    memcpy(r, t, sizeof(t));
}
static void pal_mult(const float *m) { mat_mul(palette[cur_palette], palette[cur_palette], m); }
static void fx_to_f(const GLfixed *x, float *f) {
    for (int i = 0; i < 16; i++)
        f[i] = x[i] / 65536.0f;
}

static void g_glMatrixMode(GLenum m) {
    palette_mode = m == GL_MATRIX_PALETTE_OES;
    if (!palette_mode) {
        real_matrix_mode = m;
        glMatrixMode(m);
    }
}
static void g_glLoadIdentity(void) {
    if (!palette_mode)
        return glLoadIdentity();
    static const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    memcpy(palette[cur_palette], id, sizeof(id));
}
static void g_glLoadMatrixf(const GLfloat *m) {
    if (!palette_mode)
        return glLoadMatrixf(m);
    memcpy(palette[cur_palette], m, 16 * sizeof(float));
}
static void g_glLoadMatrixx(const GLfixed *m) {
    if (!palette_mode)
        return glLoadMatrixx(m);
    fx_to_f(m, palette[cur_palette]);
}
static void g_glMultMatrixf(const GLfloat *m) {
    if (!palette_mode)
        return glMultMatrixf(m);
    pal_mult(m);
}
static void g_glMultMatrixx(const GLfixed *m) {
    if (!palette_mode)
        return glMultMatrixx(m);
    float f[16];
    fx_to_f(m, f);
    pal_mult(f);
}
static void g_glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    if (!palette_mode)
        return glTranslatef(x, y, z);
    const float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
    pal_mult(t);
}
static void g_glTranslatex(GLfixed x, GLfixed y, GLfixed z) { g_glTranslatef(x / 65536.0f, y / 65536.0f, z / 65536.0f); }
static void g_glScalef(GLfloat x, GLfloat y, GLfloat z) {
    if (!palette_mode)
        return glScalef(x, y, z);
    const float t[16] = {x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1};
    pal_mult(t);
}
static void g_glScalex(GLfixed x, GLfixed y, GLfixed z) { g_glScalef(x / 65536.0f, y / 65536.0f, z / 65536.0f); }
static void g_glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z) {
    if (!palette_mode)
        return glRotatef(a, x, y, z);
    float l = sqrtf(x * x + y * y + z * z);
    if (l == 0)
        return;
    x /= l, y /= l, z /= l;
    float c = cosf(a * 3.14159265f / 180), s_ = sinf(a * 3.14159265f / 180), k = 1 - c;
    const float t[16] = {x * x * k + c,     y * x * k + z * s_, x * z * k - y * s_, 0, x * y * k - z * s_, y * y * k + c,
                         y * z * k + x * s_, 0, x * z * k + y * s_, y * z * k - x * s_, z * z * k + c, 0, 0, 0, 0, 1};
    pal_mult(t);
}
static void g_glRotatex(GLfixed a, GLfixed x, GLfixed y, GLfixed z) {
    g_glRotatef(a / 65536.0f, x / 65536.0f, y / 65536.0f, z / 65536.0f);
}
// Pilas propias: vitaGL (SKIP_ERROR_HANDLING) no controla límites (modelview 32, proyección/textura 2,
// contador uint8) y RF09 hace 100 glPushMatrix seguidos en GlRenderer::Init -> pisaba globales de vitaGL.
// Semántica GL: push con pila llena y pop con pila vacía no hacen nada.
// ponytail: una sola pila de textura (unidad 0); separar por unidad si un juego apila la 1.
#define MAT_DEPTH 32
static float mat_stack[3][MAT_DEPTH][16];
static int mat_depth[3];
static int mat_slot(GLenum *q) {
    switch (real_matrix_mode) {
    case GL_PROJECTION: *q = GL_PROJECTION_MATRIX; return 1;
    case GL_TEXTURE: *q = GL_TEXTURE_MATRIX; return 2;
    default: *q = GL_MODELVIEW_MATRIX; return 0;
    }
}
static void g_glPushMatrix(void) {
    GLenum q;
    int k = mat_slot(&q);
    if (palette_mode || mat_depth[k] >= MAT_DEPTH)
        return;
    glGetFloatv(q, mat_stack[k][mat_depth[k]++]);
}
static void g_glPopMatrix(void) {
    GLenum q;
    int k = mat_slot(&q);
    if (palette_mode || !mat_depth[k])
        return;
    glLoadMatrixf(mat_stack[k][--mat_depth[k]]);
}
static void g_glCurrentPaletteMatrixOES(GLuint i) { cur_palette = i < MAX_PALETTE ? i : 0; }
static void g_glLoadPaletteFromModelViewMatrixOES(void) { glGetFloatv(GL_MODELVIEW_MATRIX, palette[cur_palette]); }
static void g_glMatrixIndexPointerOES(GLint size, GLenum type, GLsizei stride, const void *p) {
    mi_arr = (array_ptr){size, type, stride, p, bound_array_buffer};
}
static void g_glWeightPointerOES(GLint size, GLenum type, GLsizei stride, const void *p) {
    wt_arr = (array_ptr){size, type, stride, p, bound_array_buffer};
}

static const uint8_t *arr_base(const array_ptr *a) {
    return a->vbo ? (const uint8_t *)vbo_data(a->vbo) + (uintptr_t)a->ptr : (const uint8_t *)a->ptr;
}
static int type_size(GLenum t) {
    return t == GL_BYTE || t == GL_UNSIGNED_BYTE ? 1 : t == GL_SHORT || t == GL_UNSIGNED_SHORT ? 2 : 4;
}
static float read_comp(const uint8_t *p, GLenum t, int i) {
    switch (t) {
    case GL_FLOAT:
        return ((const float *)p)[i];
    case GL_FIXED:
        return ((const int32_t *)p)[i] / 65536.0f;
    case GL_SHORT:
        return ((const int16_t *)p)[i];
    case GL_UNSIGNED_SHORT:
        return ((const uint16_t *)p)[i];
    case GL_BYTE:
        return ((const int8_t *)p)[i];
    default:
        return ((const uint8_t *)p)[i];
    }
}
static float *skin_v, *skin_n;
static int skin_cap;
// Transforma n vértices con la paleta; deja los punteros de vértice/normal apuntando al resultado.
static int skin(int n) {
    if (!palette_on || !mi_on || !wt_on || !arr[A_VTX].size)
        return 0;
    const uint8_t *vb = arr_base(&arr[A_VTX]), *nb = arr[A_NRM].size ? arr_base(&arr[A_NRM]) : NULL;
    const uint8_t *ib = arr_base(&mi_arr), *wb = arr_base(&wt_arr);
    if (!vb || !ib || !wb)
        return 0;
    if (n > skin_cap) {
        skin_cap = n;
        skin_v = realloc(skin_v, n * 3 * sizeof(float));
        skin_n = realloc(skin_n, n * 3 * sizeof(float));
    }
    int vs = arr[A_VTX].stride ? arr[A_VTX].stride : arr[A_VTX].size * type_size(arr[A_VTX].type);
    int ns = nb ? (arr[A_NRM].stride ? arr[A_NRM].stride : 3 * type_size(arr[A_NRM].type)) : 0;
    int is = mi_arr.stride ? mi_arr.stride : mi_arr.size * type_size(mi_arr.type);
    int ws = wt_arr.stride ? wt_arr.stride : wt_arr.size * type_size(wt_arr.type);
    for (int i = 0; i < n; i++) {
        const uint8_t *v = vb + i * vs;
        float x = read_comp(v, arr[A_VTX].type, 0), y = read_comp(v, arr[A_VTX].type, 1),
              z = arr[A_VTX].size > 2 ? read_comp(v, arr[A_VTX].type, 2) : 0;
        float nx = 0, ny = 0, nz = 0;
        if (nb) {
            const uint8_t *q = nb + i * ns;
            nx = read_comp(q, arr[A_NRM].type, 0), ny = read_comp(q, arr[A_NRM].type, 1), nz = read_comp(q, arr[A_NRM].type, 2);
        }
        float ox = 0, oy = 0, oz = 0, onx = 0, ony = 0, onz = 0;
        for (int k = 0; k < mi_arr.size && k < wt_arr.size; k++) {
            int idx = (int)read_comp(ib + i * is, mi_arr.type, k);
            float w = read_comp(wb + i * ws, wt_arr.type, k);
            if (w == 0 || idx < 0 || idx >= MAX_PALETTE)
                continue;
            const float *m = palette[idx];
            ox += w * (m[0] * x + m[4] * y + m[8] * z + m[12]);
            oy += w * (m[1] * x + m[5] * y + m[9] * z + m[13]);
            oz += w * (m[2] * x + m[6] * y + m[10] * z + m[14]);
            onx += w * (m[0] * nx + m[4] * ny + m[8] * nz);
            ony += w * (m[1] * nx + m[5] * ny + m[9] * nz);
            onz += w * (m[2] * nx + m[6] * ny + m[10] * nz);
        }
        skin_v[i * 3] = ox, skin_v[i * 3 + 1] = oy, skin_v[i * 3 + 2] = oz;
        skin_n[i * 3] = onx, skin_n[i * 3 + 1] = ony, skin_n[i * 3 + 2] = onz;
    }
    if (bound_array_buffer)
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexPointer(3, GL_FLOAT, 0, skin_v);
    if (nb)
        glNormalPointer(GL_FLOAT, 0, skin_n);
    if (bound_array_buffer)
        glBindBuffer(GL_ARRAY_BUFFER, bound_array_buffer);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    return 1;
}
static void unskin(void) {
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(real_matrix_mode);
    set_pointer(A_VTX, 0);
    set_pointer(A_NRM, 0);
}

int palette_active(void) { return palette_on && mi_on && wt_on; }
static void g_glEnable(GLenum cap) {
    if (cap == GL_MATRIX_PALETTE_OES) {
        palette_on = 1;
        return;
    }
    glEnable(cap);
}
static void g_glDisable(GLenum cap) {
    if (cap == GL_MATRIX_PALETTE_OES) {
        palette_on = 0;
        return;
    }
    glDisable(cap);
}
static void g_glEnableClientState(GLenum a) {
    if (a == 0x8844 /* MATRIX_INDEX_ARRAY */ || a == 0x86AD /* WEIGHT_ARRAY */) {
        *(a == 0x8844 ? &mi_on : &wt_on) = 1;
        return;
    }
    glEnableClientState(a);
}
static void g_glDisableClientState(GLenum a) {
    if (a == 0x8844 || a == 0x86AD) {
        *(a == 0x8844 ? &mi_on : &wt_on) = 0;
        return;
    }
    glDisableClientState(a);
}

static const GLubyte *g_glGetString(GLenum name) {
    switch (name) {
    case GL_VENDOR:
        return (const GLubyte *)"Imagination Technologies";
    case GL_RENDERER:
        return (const GLubyte *)"PowerVR SGX 535";
    case GL_VERSION:
        return (const GLubyte *)"OpenGL ES-CM 1.1 IMGSGX535-63.14.2";
    case GL_EXTENSIONS:
        return (const GLubyte *)"GL_OES_blend_subtract GL_OES_compressed_paletted_texture GL_OES_depth24 "
                                "GL_OES_draw_texture GL_OES_framebuffer_object GL_OES_mapbuffer GL_OES_point_size_array "
                                "GL_OES_point_sprite GL_OES_read_format GL_OES_rgb8_rgba8 GL_OES_texture_mirrored_repeat "
                                "GL_EXT_texture_filter_anisotropic GL_EXT_texture_lod_bias GL_IMG_read_format "
                                "GL_IMG_texture_compression_pvrtc GL_IMG_texture_format_BGRA8888 "
                                "GL_APPLE_texture_2D_limited_npot";
    }
    return glGetString(name);
}

static void g_glGetIntegerv(GLenum pname, GLint *v) {
    if (pname == GL_MAX_TEXTURE_SIZE) {
        *v = 2048;
        return;
    }
    glGetIntegerv(pname, v);
}

// ---------------------------------------------------------------- framebuffers OES

#define MAX_NAMES 256
static struct {
    int is_drawable; // color del CAEAGLLayer
    GLuint vgl;      // renderbuffer de vitaGL (profundidad de FBOs propios)
    int w, h;
    GLenum fmt;
} rbs[MAX_NAMES];
static struct {
    int screen;  // tiene el drawable como color
    GLuint vgl;  // FBO real de vitaGL (render-to-texture)
    GLuint color_tex;
} fbs[MAX_NAMES];
static GLuint next_rb = 1, next_fb = 1, cur_rb, cur_fb;

static void g_glGenRenderbuffersOES(GLsizei n, GLuint *ids) {
    for (int i = 0; i < n; i++) {
        ids[i] = next_rb < MAX_NAMES ? next_rb++ : 0;
        memset(&rbs[ids[i]], 0, sizeof(rbs[0]));
    }
}
static void g_glGenFramebuffersOES(GLsizei n, GLuint *ids) {
    for (int i = 0; i < n; i++) {
        ids[i] = next_fb < MAX_NAMES ? next_fb++ : 0;
        memset(&fbs[ids[i]], 0, sizeof(fbs[0]));
    }
}
static void g_glBindRenderbufferOES(GLenum target, GLuint rb) { cur_rb = rb < MAX_NAMES ? rb : 0; }
static void g_glRenderbufferStorageOES(GLenum target, GLenum fmt, GLsizei w, GLsizei h) {
    rbs[cur_rb].w = w, rbs[cur_rb].h = h, rbs[cur_rb].fmt = fmt;
}
// EAGLContext renderbufferStorage:fromDrawable: (uikit.c)
void gles_drawable_storage(int w, int h) {
    rbs[cur_rb].is_drawable = 1;
    rbs[cur_rb].w = w, rbs[cur_rb].h = h;
    gles_set_size(w, h);
}
static void bind_fb(GLuint fb) {
    if (!fb || fbs[fb].screen || !fbs[fb].vgl)
        glBindFramebuffer(GL_FRAMEBUFFER, scr_fbo);
    else
        glBindFramebuffer(GL_FRAMEBUFFER, fbs[fb].vgl);
}
void gles_bind_screen(void) { // GLKView bindDrawable
    cur_fb = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, scr_fbo);
}
static void g_glBindFramebufferOES(GLenum target, GLuint fb) {
    cur_fb = fb < MAX_NAMES ? fb : 0;
    bind_fb(cur_fb);
}
static void g_glFramebufferRenderbufferOES(GLenum target, GLenum att, GLenum rbt, GLuint rb) {
    if (att == GL_COLOR_ATTACHMENT0_OES && rbs[rb].is_drawable) {
        fbs[cur_fb].screen = 1;
        bind_fb(cur_fb);
    }
    // profundidad del FBO de pantalla: ya la tiene el nuestro. FBOs propios con renderbuffer de
    // color que no es el drawable: no aparecieron en estos juegos (se registra).
    else if (att == GL_COLOR_ATTACHMENT0_OES)
        l_error("FBO %u con renderbuffer de color propio: sin soporte", cur_fb);
}
static void g_glFramebufferTexture2DOES(GLenum target, GLenum att, GLenum textarget, GLuint tex, GLint level) {
    if (!fbs[cur_fb].vgl) {
        glGenFramebuffers(1, &fbs[cur_fb].vgl);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbs[cur_fb].vgl);
    if (att == GL_COLOR_ATTACHMENT0_OES) {
        fbs[cur_fb].color_tex = tex;
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textarget, tex, level);
    }
}
static GLenum g_glCheckFramebufferStatusOES(GLenum target) { return GL_FRAMEBUFFER_COMPLETE_OES; }
static void g_glGetRenderbufferParameterivOES(GLenum target, GLenum pname, GLint *v) {
    *v = pname == GL_RENDERBUFFER_WIDTH_OES ? rbs[cur_rb].w : pname == GL_RENDERBUFFER_HEIGHT_OES ? rbs[cur_rb].h
         : pname == GL_RENDERBUFFER_INTERNAL_FORMAT_OES ? (GLint)GL_RGBA : 0;
}
static void g_glGetFramebufferAttachmentParameterivOES(GLenum t, GLenum a, GLenum p, GLint *v) { *v = 0; }
static void g_glDeleteFramebuffersOES(GLsizei n, const GLuint *ids) {
    for (int i = 0; i < n; i++)
        if (ids[i] < MAX_NAMES && fbs[ids[i]].vgl) {
            glDeleteFramebuffers(1, &fbs[ids[i]].vgl);
            fbs[ids[i]].vgl = 0;
        }
}
static void g_glNoop(void) {}
static GLboolean g_glIsFramebufferOES(GLuint fb) { return fb && fb < next_fb; }

// ---------------------------------------------------------------- init / present

void gles_set_size(int w, int h) {
    if (w <= 0 || h <= 0)
        return;
    if (scr_fbo && w == fb_w && h == fb_h)
        return;
    fb_w = w, fb_h = h;
    if (!scr_tex)
        glGenTextures(1, &scr_tex);
    glBindTexture(GL_TEXTURE_2D, scr_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fb_w, fb_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!scr_fbo)
        glGenFramebuffers(1, &scr_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, scr_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scr_tex, 0);
    if (!scr_depth)
        glGenRenderbuffers(1, &scr_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, scr_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fb_w, fb_h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, scr_depth);
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    l_info("vitaGL: drawable %dx%d -> pantalla %dx%d (rotación %d)", fb_w, fb_h, SCR_W, SCR_H, gles_rotation());
}

void gles_init(void) {
    vglUseTripleBuffering(GL_FALSE);
    vglInitExtended(0, SCR_W, SCR_H, GLES_POOL_MB * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    gles_set_size(fb_w, fb_h);
}

// Rotación del drawable a la pantalla: 0 = sin girar (drawable apaisado o juego vertical
// centrado), 1 = landscape-right (borde superior del drawable a la izquierda), 2 = landscape-left.
int port_rotation = PORT_ROTATION; // input.c puede cambiarla (SELECT+START) y la guarda en rotation.txt
int gles_rotation(void) { return fb_w > fb_h ? 0 : port_rotation; }

// Rectángulo destino en pantalla (para input.c: mismo mapeo inverso).
void gles_dest_rect(int *x, int *y, int *w, int *h) {
    int rw = gles_rotation() ? fb_h : fb_w, rh = gles_rotation() ? fb_w : fb_h; // tamaño visto apaisado
#if PORT_STRETCH
    (void)rw, (void)rh;
    *x = 0, *y = 0, *w = SCR_W, *h = SCR_H;
#else
    int dw = SCR_H * rw / rh;
    if (dw > SCR_W)
        dw = SCR_W;
    *w = dw, *h = SCR_H;
    *x = (SCR_W - dw) / 2, *y = 0;
#endif
}

void gles_present(void) {
    static const GLenum caps[] = {GL_BLEND, GL_ALPHA_TEST, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_CULL_FACE, GL_FOG,
                                  GL_LIGHTING, GL_TEXTURE_2D};
    enum { NCAPS = sizeof(caps) / sizeof(caps[0]) };
    GLboolean on[NCAPS], tca, va, ca, na;
    GLint tex, mode, active, vp[4];
    for (int i = 0; i < NCAPS; i++)
        on[i] = glIsEnabled(caps[i]);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    tca = glIsEnabled(GL_TEXTURE_COORD_ARRAY);
    va = glIsEnabled(GL_VERTEX_ARRAY);
    ca = glIsEnabled(GL_COLOR_ARRAY);
    na = glIsEnabled(GL_NORMAL_ARRAY);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    glGetIntegerv(GL_MATRIX_MODE, &mode);
    glGetIntegerv(GL_VIEWPORT, vp);
    if (bound_array_buffer)
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (bound_element_buffer)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SCR_W, SCR_H);
    for (int i = 0; i < NCAPS - 1; i++)
        glDisable(caps[i]);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glColor4f(1, 1, 1, 1);
    glBindTexture(GL_TEXTURE_2D, scr_tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrthof(0, SCR_W, SCR_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    int dx, dy, dw, dh;
    gles_dest_rect(&dx, &dy, &dw, &dh);
    // Esquinas de pantalla (sup-izq, sup-der, inf-izq, inf-der) -> UV del FBO (v = 0 abajo en GL).
    static const float uv_rot[3][8] = {
        {0, 1, 1, 1, 0, 0, 1, 0}, // sin girar
        {1, 1, 1, 0, 0, 1, 0, 0}, // landscape-right (probado en Inotia2)
        {0, 0, 0, 1, 1, 0, 1, 1}, // landscape-left
    };
    const float xy[] = {dx, dy, dx + dw, dy, dx, dy + dh, dx + dw, dy + dh};
    glVertexPointer(2, GL_FLOAT, 0, xy);
    glTexCoordPointer(2, GL_FLOAT, 0, uv_rot[gles_rotation()]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    uint64_t tp = T_NOW();
    vglSwapBuffers(ime_active());
    t_present += T_NOW() - tp;

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(mode);
    bind_fb(cur_fb);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, env_mode);
    for (int i = 0; i < NCAPS; i++)
        on[i] ? glEnable(caps[i]) : glDisable(caps[i]);
    if (!va)
        glDisableClientState(GL_VERTEX_ARRAY);
    if (!tca)
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (ca)
        glEnableClientState(GL_COLOR_ARRAY);
    if (na)
        glEnableClientState(GL_NORMAL_ARRAY);
    glColor4x(color[0], color[1], color[2], color[3]);
    if (bound_array_buffer)
        glBindBuffer(GL_ARRAY_BUFFER, bound_array_buffer);
    if (bound_element_buffer)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bound_element_buffer);
    for (int k = 0; k < A_N; k++)
        if (arr[k].type != GL_FIXED)
            set_pointer(k, 0);
    glClientActiveTexture(GL_TEXTURE0 + client_unit);
    glActiveTexture(active);
}


// ---------------------------------------------------------------- tabla

const import_entry gles_imports[] = {
    D(glActiveTexture), D(glAlphaFunc), D(glAlphaFuncx), D(glBindTexture), D(glBlendFunc), D(glClear),
    D(glClearColor), D(glClearColorx), D(glClearDepthf), D(glClearDepthx), D(glClearStencil), D(glColorMask),
    D(glCompressedTexImage2D), D(glCopyTexImage2D), D(glCopyTexSubImage2D),
    D(glCullFace), D(glDeleteTextures), D(glDepthFunc), D(glDepthMask), D(glDepthRangef), D(glDepthRangex),
    D(glFinish), D(glFlush), D(glFogf), D(glFogfv), D(glFogx), D(glFogxv), D(glFrontFace), D(glFrustumf),
    D(glFrustumx), D(glGenBuffers), D(glGenTextures), D(glGetError), D(glGetFloatv), D(glGetBooleanv),
    D(glHint), D(glIsEnabled), D(glIsTexture), D(glLightModelfv), D(glLightModelxv),
    F(glLightf, g_glLightf), D(glLightfv), D(glLightxv), D(glLineWidth), D(glLineWidthx), F(glLoadIdentity, g_glLoadIdentity), F(glLoadMatrixf, g_glLoadMatrixf),
    F(glLoadMatrixx, g_glLoadMatrixx), D(glMaterialf), D(glMaterialfv), D(glMaterialx), F(glMatrixMode, g_glMatrixMode), F(glMultMatrixf, g_glMultMatrixf),
    F(glMultMatrixx, g_glMultMatrixx), D(glNormal3f), D(glOrthof), D(glOrthox), D(glPixelStorei), D(glPointSize), D(glPolygonOffset),
    F(glPopMatrix, g_glPopMatrix), F(glPushMatrix, g_glPushMatrix), D(glReadPixels), F(glRotatef, g_glRotatef), F(glRotatex, g_glRotatex), F(glScalef, g_glScalef),
    F(glScalex, g_glScalex), D(glScissor), D(glShadeModel), D(glStencilFunc), D(glStencilMask), D(glStencilOp), D(glTexImage2D),
    D(glTexParameteri), D(glTexParameteriv), D(glTexSubImage2D), F(glTranslatef, g_glTranslatef),
    F(glTranslatex, g_glTranslatex), D(glViewport), D(glBlendEquation), D(glTexEnvfv), 
    D(glDeleteRenderbuffers), 
    F(glColor4x, g_glColor4x), F(glColor4f, g_glColor4f), F(glColor4ub, g_glColor4ub), F(glTexEnvi, g_glTexEnvi),
    F(glTexEnvf, g_glTexEnvf), F(glTexEnvx, g_glTexEnvx), F(glTexEnvxv, g_glTexEnvxv),
    F(glTexParameterx, g_glTexParameterx), F(glTexParameterf, g_glTexParameterf), F(glLightModelx, g_glLightModelx),
    F(glLightModelf, g_glLightModelf), F(glLightx, g_glLightx), F(glMaterialxv, g_glMaterialxv),
    F(glGetFixedv, g_glGetFixedv), F(glGetLightxv, g_glGetLightxv), F(glGetPointerv, g_glGetPointerv),
    F(glGetString, g_glGetString), F(glGetIntegerv, g_glGetIntegerv), F(glVertexPointer, g_glVertexPointer),
    F(glNormalPointer, g_glNormalPointer), F(glColorPointer, g_glColorPointer),
    F(glTexCoordPointer, g_glTexCoordPointer), F(glClientActiveTexture, g_glClientActiveTexture),
    F(glDrawArrays, g_glDrawArrays), F(glDrawElements, g_glDrawElements), F(glBindBuffer, g_glBindBuffer),
    F(glBufferData, g_glBufferData), F(glBufferSubData, g_glBufferSubData), F(glDeleteBuffers, g_glDeleteBuffers),
    F(glEnable, g_glEnable), F(glDisable, g_glDisable), F(glEnableClientState, g_glEnableClientState),
    F(glDisableClientState, g_glDisableClientState),
    F(glCurrentPaletteMatrixOES, g_glCurrentPaletteMatrixOES), F(glMatrixIndexPointerOES, g_glMatrixIndexPointerOES),
    F(glWeightPointerOES, g_glWeightPointerOES), F(glLoadPaletteFromModelViewMatrixOES, g_glLoadPaletteFromModelViewMatrixOES),
    F(glGenFramebuffersOES, g_glGenFramebuffersOES), F(glGenRenderbuffersOES, g_glGenRenderbuffersOES),
    F(glBindFramebufferOES, g_glBindFramebufferOES), F(glBindRenderbufferOES, g_glBindRenderbufferOES),
    F(glDeleteFramebuffersOES, g_glDeleteFramebuffersOES), F(glDeleteRenderbuffersOES, g_glNoop),
    F(glRenderbufferStorageOES, g_glRenderbufferStorageOES),
    F(glFramebufferRenderbufferOES, g_glFramebufferRenderbufferOES),
    F(glFramebufferTexture2DOES, g_glFramebufferTexture2DOES),
    F(glCheckFramebufferStatusOES, g_glCheckFramebufferStatusOES),
    F(glGetRenderbufferParameterivOES, g_glGetRenderbufferParameterivOES),
    F(glGetFramebufferAttachmentParameterivOES, g_glGetFramebufferAttachmentParameterivOES),
    F(glIsFramebufferOES, g_glIsFramebufferOES), F(glPointParameterf, g_glNoop), F(glPointParameterfv, g_glNoop),
    D(glClipPlanef), D(glClipPlanex), D(glVertexAttribPointer), D(glEnableVertexAttribArray),
    D(glDisableVertexAttribArray), D(glVertexAttrib4f), D(glDeleteProgram), D(glGenerateMipmap),
    F(glBlendEquationOES, glBlendEquation), F(glBlendFuncSeparateOES, glBlendFuncSeparate), F(glGenerateMipmapOES, glGenerateMipmap),
};
TABLE_END(gles)
