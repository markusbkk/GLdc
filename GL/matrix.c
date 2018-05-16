#include <dc/matrix.h>

#include "../include/gl.h"
#include "../containers/stack.h"
#include "../gl-sh4.h"

/* Viewport mapping */
static GLfloat gl_viewport_scale[3], gl_viewport_offset[3];

/* Depth range */
static GLclampf gl_depthrange_near, gl_depthrange_far;

/* Viewport size */
static GLint gl_viewport_x1, gl_viewport_y1, gl_viewport_width, gl_viewport_height;

static Stack MATRIX_STACKS[3]; // modelview, projection, texture
static matrix_t NORMAL_MATRIX __attribute__((aligned(32)));
static matrix_t SCREENVIEW_MATRIX __attribute__((aligned(32)));
static matrix_t RENDER_MATRIX __attribute__((aligned(32)));

static GLenum MATRIX_MODE = GL_MODELVIEW;
static GLubyte MATRIX_IDX = 0;

static const matrix_t IDENTITY = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f}
};

void APIENTRY glDepthRange(GLclampf n, GLclampf f);

void initMatrices() {
    init_stack(&MATRIX_STACKS[0], sizeof(matrix_t), 32);
    init_stack(&MATRIX_STACKS[1], sizeof(matrix_t), 32);
    init_stack(&MATRIX_STACKS[2], sizeof(matrix_t), 32);

    stack_push(&MATRIX_STACKS[0], IDENTITY);
    stack_push(&MATRIX_STACKS[1], IDENTITY);
    stack_push(&MATRIX_STACKS[2], IDENTITY);

    memcpy(NORMAL_MATRIX, IDENTITY, sizeof(matrix_t));
    memcpy(SCREENVIEW_MATRIX, IDENTITY, sizeof(matrix_t));

    glDepthRange(0.0f, 1.0f);
    glViewport(0, 0, vid_mode->width, vid_mode->height);
}

static void recalculateNormalMatrix() {

}

void APIENTRY glMatrixMode(GLenum mode) {
    MATRIX_MODE = mode;
    MATRIX_IDX = mode & 0xF;
}

void APIENTRY glPushMatrix() {
    stack_push(MATRIX_STACKS + MATRIX_IDX, stack_top(MATRIX_STACKS + MATRIX_IDX));
}

void APIENTRY glPopMatrix() {
    stack_pop(MATRIX_STACKS + MATRIX_IDX);
    if(MATRIX_MODE == GL_MODELVIEW) {
        recalculateNormalMatrix();
    }
}

void APIENTRY glLoadIdentity() {
    stack_replace(MATRIX_STACKS + MATRIX_IDX, IDENTITY);
}

void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    mat_load(stack_top(MATRIX_STACKS + MATRIX_IDX));
    mat_translate(x, y, z);
    mat_store(stack_top(MATRIX_STACKS + MATRIX_IDX));

    if(MATRIX_MODE == GL_MODELVIEW) {
        recalculateNormalMatrix();
    }
}


void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z) {
    mat_load(stack_top(MATRIX_STACKS + MATRIX_IDX));
    mat_scale(x, y, z);
    mat_store(stack_top(MATRIX_STACKS + MATRIX_IDX));

    if(MATRIX_MODE == GL_MODELVIEW) {
        recalculateNormalMatrix();
    }
}

void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat  y, GLfloat z) {
    float r = DEG2RAD * -angle;

    vec3f_normalize(x, y, z);

    mat_load(stack_top(MATRIX_STACKS + MATRIX_IDX));
    mat_rotate(r * x, r * y, r * z);
    mat_store(stack_top(MATRIX_STACKS + MATRIX_IDX));

    if(MATRIX_MODE == GL_MODELVIEW) {
        recalculateNormalMatrix();
    }
}

/* Load an arbitrary matrix */
void APIENTRY glLoadMatrixf(const GLfloat *m) {
    stack_replace(MATRIX_STACKS + MATRIX_IDX, m);

    if(MATRIX_MODE == GL_MODELVIEW) {
        recalculateNormalMatrix();
    }
}

/* Ortho */
void APIENTRY glOrtho(GLfloat left, GLfloat right,
             GLfloat bottom, GLfloat top,
             GLfloat znear, GLfloat zfar) {

    /* Ortho Matrix */
    static matrix_t OrthoMatrix __attribute__((aligned(32))) = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    OrthoMatrix[0][0] = 2.0f / (right - left);
    OrthoMatrix[1][1] = 2.0f / (top - bottom);
    OrthoMatrix[2][2] = -2.0f / (zfar - znear);
    OrthoMatrix[3][0] = -(right + left) / (right - left);;
    OrthoMatrix[3][1] = -(top + bottom) / (top - bottom);
    OrthoMatrix[3][2] = -(zfar + znear) / (zfar - znear);

    mat_load(stack_top(MATRIX_STACKS + MATRIX_MODE));
    mat_apply(&OrthoMatrix);
    mat_store(stack_top(MATRIX_STACKS + MATRIX_MODE));
}


/* Set the GL frustum */
void APIENTRY glFrustum(GLfloat left, GLfloat right,
               GLfloat bottom, GLfloat top,
               GLfloat znear, GLfloat zfar) {

    /* Frustum Matrix */
    static matrix_t FrustumMatrix __attribute__((aligned(32))) = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, -1.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f }
    };

    FrustumMatrix[0][0] = (2.0f * znear) / (right - left);
    FrustumMatrix[2][0] = (right + left) / (right - left);
    FrustumMatrix[1][1] = (2.0f * znear) / (top - bottom);
    FrustumMatrix[2][1] = (top + bottom) / (top - bottom);
    FrustumMatrix[2][2] = zfar / (zfar - znear);
    FrustumMatrix[3][2] = -(zfar * znear) / (zfar - znear);

    mat_load(stack_top(MATRIX_STACKS + MATRIX_IDX));
    mat_apply(&FrustumMatrix);
    mat_store(stack_top(MATRIX_STACKS + MATRIX_IDX));
}

/* Set the Perspective */
void gluPerspective(GLfloat angle, GLfloat aspect,
                    GLfloat znear, GLfloat zfar) {
    GLfloat xmin, xmax, ymin, ymax;

    ymax = znear * ftan(angle * F_PI / 360.0f);
    ymin = -ymax;
    xmin = ymin * aspect;
    xmax = ymax * aspect;

    glFrustum(xmin, xmax, ymin, ymax, znear, zfar);
}

/* Multiply the current matrix by an arbitrary matrix */
void glMultMatrixf(const GLfloat *m) {
    static matrix_t TEMP __attribute__((aligned(32))) = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    memcpy(TEMP, m, sizeof(matrix_t));

    mat_load(stack_top(MATRIX_STACKS + MATRIX_IDX));
    mat_apply(&TEMP);
    mat_store(stack_top(MATRIX_STACKS + MATRIX_IDX));

    if(MATRIX_MODE == GL_MODELVIEW) {
        recalculateNormalMatrix();
    }
}

/* Load an arbitrary transposed matrix */
void glLoadTransposeMatrixf(const GLfloat *m) {
    stack_replace(MATRIX_STACKS + MATRIX_IDX, m);
    transpose(stack_top(MATRIX_STACKS + MATRIX_IDX));

    if(MATRIX_MODE == GL_MODELVIEW) {
        recalculateNormalMatrix();
    }
}

/* Multiply the current matrix by an arbitrary transposed matrix */
void glMultTransposeMatrixf(const GLfloat *m) {
    static matrix_t ml;

    ml[0][0] = m[0];
    ml[0][1] = m[4];
    ml[0][2] = m[8];
    ml[0][3] = m[12];
    ml[1][0] = m[1];
    ml[1][1] = m[5];
    ml[1][2] = m[9];
    ml[1][3] = m[13];
    ml[2][0] = m[2];
    ml[2][1] = m[6];
    ml[2][2] = m[10];
    ml[2][3] = m[14];
    ml[3][0] = m[3];
    ml[3][1] = m[7];
    ml[3][2] = m[11];
    ml[3][3] = m[15];

    mat_load(stack_top(MATRIX_STACKS + MATRIX_IDX));
    mat_apply(&ml);
    mat_store(stack_top(MATRIX_STACKS + MATRIX_IDX));
}

/* Set the GL viewport */
void APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    gl_viewport_x1 = x;
    gl_viewport_y1 = y;
    gl_viewport_width = width;
    gl_viewport_height = height;

    /* Calculate the viewport scale and offset */
    gl_viewport_scale[0] = (GLfloat)width / 2.0f;
    gl_viewport_offset[0] = gl_viewport_scale[0] + (GLfloat)x;
    gl_viewport_scale[1] = (GLfloat)height / 2.0f;
    gl_viewport_offset[1] = gl_viewport_scale[1] + (GLfloat)y;
    gl_viewport_scale[2] = (gl_depthrange_far - gl_depthrange_near) / 2.0f;
    gl_viewport_offset[2] = (gl_depthrange_near + gl_depthrange_far) / 2.0f;

    gl_viewport_offset[2] += 0.0001f;

    /* Set the Screenview Matrix based on the viewport */
    SCREENVIEW_MATRIX[0][0] = gl_viewport_scale[0];
    SCREENVIEW_MATRIX[1][1] = -gl_viewport_scale[1];
    SCREENVIEW_MATRIX[2][2] = 1;
    SCREENVIEW_MATRIX[3][0] = gl_viewport_offset[0];
    SCREENVIEW_MATRIX[3][1] = vid_mode->height - gl_viewport_offset[1];
}

/* Set the depth range */
void APIENTRY glDepthRange(GLclampf n, GLclampf f) {
    /* clamp the values... */
    if(n < 0.0f) n = 0.0f;
    else if(n > 1.0f) n = 1.0f;

    if(f < 0.0f) f = 0.0f;
    else if(f > 1.0f) f = 1.0f;

    gl_depthrange_near = n;
    gl_depthrange_far = f;

    /* Adjust the viewport scale and offset for Z */
    gl_viewport_scale[2] = ((f - n) / 2.0f);
    gl_viewport_offset[2] = (n + f) / 2.0f;
}

/* Vector Cross Product - Used by glhLookAtf2 */
static inline void vec3f_cross(vector3f v1, vector3f v2, vector3f result) {
    result[0] = v1[1] * v2[2] - v1[2] * v2[1];
    result[1] = v1[2] * v2[0] - v1[0] * v2[2];
    result[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

/* glhLookAtf2 adapted from http://www.opengl.org/wiki/GluLookAt_code */
void glhLookAtf2(vector3f eyePosition3D,
                 vector3f center3D,
                 vector3f upVector3D) {

    /* Look-At Matrix */
    static matrix4f MatrixLookAt __attribute__((aligned(32))) = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    vector3f forward, side, up;

    vec3f_sub_normalize(center3D[0], center3D[1], center3D[2],
                        eyePosition3D[0], eyePosition3D[1], eyePosition3D[2],
                        forward[0], forward[1], forward[2]);

    //Side = forward x up
    vec3f_cross(forward, upVector3D, side);
    vec3f_normalize(side[0], side[1], side[2]);

    //Recompute up as: up = side x forward
    vec3f_cross(side, forward, up);

    MatrixLookAt[0][0] = side[0];
    MatrixLookAt[1][0] = side[1];
    MatrixLookAt[2][0] = side[2];
    MatrixLookAt[3][0] = 0;

    MatrixLookAt[0][1] = up[0];
    MatrixLookAt[1][1] = up[1];
    MatrixLookAt[2][1] = up[2];
    MatrixLookAt[3][1] = 0;

    MatrixLookAt[0][2] = -forward[0];
    MatrixLookAt[1][2] = -forward[1];
    MatrixLookAt[2][2] = -forward[2];
    MatrixLookAt[3][2] = 0;

    MatrixLookAt[0][3] =
        MatrixLookAt[1][3] =
            MatrixLookAt[2][3] = 0;
    MatrixLookAt[3][3] = 1;

    // Does not modify internal Modelview matrix
    mat_load(&MatrixLookAt);
    mat_translate(-eyePosition3D[0], -eyePosition3D[1], -eyePosition3D[2]);

    mat_apply(stack_top(MATRIX_STACKS + (GL_MODELVIEW & 0xF)));
    mat_store(stack_top(MATRIX_STACKS + (GL_MODELVIEW & 0xF)));
}

void gluLookAt(GLfloat eyex, GLfloat eyey, GLfloat eyez, GLfloat centerx,
               GLfloat centery, GLfloat centerz, GLfloat upx, GLfloat upy,
               GLfloat upz) {
    vector3f eye = { eyex, eyey, eyez };
    vector3f point = { centerx, centery, centerz };
    vector3f up = { upx, upy, upz };
    glhLookAtf2(eye, point, up);
}

void _applyRenderMatrix() {
    mat_load(&SCREENVIEW_MATRIX);
    mat_apply(stack_top(MATRIX_STACKS + (GL_PROJECTION & 0xF)));
    mat_apply(stack_top(MATRIX_STACKS + (GL_MODELVIEW & 0xF)));
    mat_store(&RENDER_MATRIX);
}

void _matrixLoadRender() {
    mat_load(&RENDER_MATRIX);
}

void _matrixLoadTexture() {
    mat_load(stack_top(MATRIX_STACKS + (GL_TEXTURE & 0xF)));
}

void _matrixLoadModelView() {
    mat_load(stack_top(MATRIX_STACKS + (GL_MODELVIEW & 0xF)));
}

void _matrixLoadNormal() {
    mat_load(&NORMAL_MATRIX);
}
