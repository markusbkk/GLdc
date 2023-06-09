
#include "../containers/aligned_vector.h"
#include "private.h"

PolyList OP_LIST;
PolyList PT_LIST;
PolyList TR_LIST;

/**
 *  FAST_MODE will use invW for all Z coordinates sent to the
 *  GPU.
 *
 *  This will break orthographic mode so default is FALSE
 **/

#define FAST_MODE GL_FALSE

GLboolean AUTOSORT_ENABLED = GL_FALSE;

PolyList* _glOpaquePolyList() {
    return &OP_LIST;
}

PolyList* _glPunchThruPolyList() {
    return &PT_LIST;
}

PolyList *_glTransparentPolyList() {
    return &TR_LIST;
}

void APIENTRY glFlush() {

}

void APIENTRY glFinish() {

}


void APIENTRY glKosInitConfig(GLdcConfig* config) {
    config->autosort_enabled = GL_FALSE;
    config->fsaa_enabled = GL_FALSE;

    config->initial_op_capacity = 1024 * 3;
    config->initial_pt_capacity = 512 * 3;
    config->initial_tr_capacity = 1024 * 3;
    config->initial_immediate_capacity = 1024 * 3;
    config->internal_palette_format = GL_RGBA8;
}

void APIENTRY glKosInitEx(GLdcConfig* config) {
    TRACE();

    printf("\nWelcome to GLdc! Git revision: %s\n\n", GLDC_VERSION);

    InitGPU(config->autosort_enabled, config->fsaa_enabled);

    AUTOSORT_ENABLED = config->autosort_enabled;

    _glInitSubmissionTarget();
    _glInitMatrices();
    _glInitAttributePointers();
    _glInitContext();
    _glInitLights();
    _glInitImmediateMode(config->initial_immediate_capacity);
    _glInitFramebuffers();

    _glSetInternalPaletteFormat(config->internal_palette_format);

    _glInitTextures();

    OP_LIST.list_type = GPU_LIST_OP_POLY;
    PT_LIST.list_type = GPU_LIST_PT_POLY;
    TR_LIST.list_type = GPU_LIST_TR_POLY;

    aligned_vector_init(&OP_LIST.vector, sizeof(Vertex));
    aligned_vector_init(&PT_LIST.vector, sizeof(Vertex));
    aligned_vector_init(&TR_LIST.vector, sizeof(Vertex));

    aligned_vector_reserve(&OP_LIST.vector, config->initial_op_capacity);
    aligned_vector_reserve(&PT_LIST.vector, config->initial_pt_capacity);
    aligned_vector_reserve(&TR_LIST.vector, config->initial_tr_capacity);
}

void APIENTRY glKosInit() {
    GLdcConfig config;
    glKosInitConfig(&config);
    glKosInitEx(&config);
}

void APIENTRY glKosSwapBuffers() {
    TRACE();

    SceneBegin();
        if(OP_LIST.vector.size > 2) {
            SceneListBegin(GPU_LIST_OP_POLY);
            SceneListSubmit(OP_LIST.vector.data, OP_LIST.vector.size);
            SceneListFinish();
        }

        if(PT_LIST.vector.size > 2) {
            SceneListBegin(GPU_LIST_PT_POLY);
            SceneListSubmit(PT_LIST.vector.data, PT_LIST.vector.size);
            SceneListFinish();
        }

        if(TR_LIST.vector.size > 2) {
            SceneListBegin(GPU_LIST_TR_POLY);
            SceneListSubmit(TR_LIST.vector.data, TR_LIST.vector.size);
            SceneListFinish();
        }
    SceneFinish();

    aligned_vector_clear(&OP_LIST.vector);
    aligned_vector_clear(&PT_LIST.vector);
    aligned_vector_clear(&TR_LIST.vector);

    _glApplyScissor(true);
}