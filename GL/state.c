#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "private.h"

static PolyContext GL_CONTEXT;

PolyContext *_glGetPVRContext() {
    return &GL_CONTEXT;
}


static struct {
    GLboolean is_dirty;

/* We can't just use the GL_CONTEXT for this state as the two
 * GL states are combined, so we store them separately and then
 * calculate the appropriate PVR state from them. */
    GLenum depth_func;
    GLboolean depth_test_enabled;
    GLenum cull_face;
    GLenum front_face;
    GLboolean culling_enabled;
    GLboolean color_material_enabled;
    GLboolean znear_clipping_enabled;
    GLboolean lighting_enabled;
    GLboolean shared_palette_enabled;
    GLboolean alpha_test_enabled;
    GLboolean polygon_offset_enabled;
    GLboolean normalize_enabled;;

    struct {
        GLint x;
        GLint y;
        GLsizei width;
        GLsizei height;
        GLboolean applied;
    } scissor_rect;

    GLenum blend_sfactor;
    GLenum blend_dfactor;
    GLboolean blend_enabled;
    GLfloat offset_factor;
    GLfloat offset_units;

    GLfloat scene_ambient[4];
    GLboolean viewer_in_eye_coords;
    GLenum color_control;
    GLenum color_material_mode;
    GLenum color_material_mask;

    LightSource lights[MAX_GLDC_LIGHTS];
    GLuint enabled_light_count;
    Material material;
} GPUState = {
    GL_TRUE,
    GL_LESS,
    GL_FALSE,
    GL_BACK,
    GL_CCW,
    GL_FALSE,
    GL_FALSE,
    GL_TRUE,
    GL_FALSE,
    GL_FALSE,
    GL_FALSE,
    GL_FALSE,
    GL_FALSE,
    {0, 0, 640, 480, false},
    GL_ONE,
    GL_ZERO,
    GL_FALSE,
    0.0f,
    0.0f,
    {0.2f, 0.2f, 0.2f, 1.0f},
    GL_TRUE,
    GL_SINGLE_COLOR,
    GL_AMBIENT_AND_DIFFUSE,
    AMBIENT_MASK | DIFFUSE_MASK,
    {0},
    0,
    {0}
};

Material* _glActiveMaterial() {
    return &GPUState.material;
}

LightSource* _glLightAt(GLuint i) {
    assert(i < MAX_GLDC_LIGHTS);
    return &GPUState.lights[i];
}

void _glEnableLight(GLubyte light, GLboolean value) {
    GPUState.lights[light].isEnabled = value;
}

GLuint _glEnabledLightCount() {
    return GPUState.enabled_light_count;
}

GLfloat* _glLightModelSceneAmbient() {
    return GPUState.scene_ambient;
}

GLboolean _glIsBlendingEnabled() {
    return GPUState.blend_enabled;
}

GLboolean _glIsAlphaTestEnabled() {
    return GPUState.alpha_test_enabled;
}

void _glRecalcEnabledLights() {
    GLubyte i;

    GPUState.enabled_light_count = 0;
    for(int i = 0; i < MAX_GLDC_LIGHTS; ++i) {
        if(_glLightAt(i)->isEnabled) {
            GPUState.enabled_light_count++;
        }
    }
}

void _glSetLightModelViewerInEyeCoordinates(GLboolean v) {
    GPUState.viewer_in_eye_coords = v;
}

void _glSetLightModelSceneAmbient(const GLfloat* v) {
    vec4cpy(GPUState.scene_ambient, v);
}

void _glSetLightModelColorControl(GLint v) {
    GPUState.color_control = v;
}

GLenum _glColorMaterialMask() {
    return GPUState.color_material_mask;
}

void _glSetColorMaterialMask(GLenum mask) {
    GPUState.color_material_mask = mask;
}

void _glSetColorMaterialMode(GLenum mode) {
    GPUState.color_material_mode = mode;
}

GLenum _glColorMaterialMode() {
    return GPUState.color_material_mode;
}

GLboolean _glIsSharedTexturePaletteEnabled() {
    return GPUState.shared_palette_enabled;
}

GLboolean _glNearZClippingEnabled() {
    return GPUState.znear_clipping_enabled;
}

void _glApplyScissor(bool force);

static int _calc_pvr_face_culling() {
    if(!GPUState.culling_enabled) {
        return GPU_CULLING_SMALL;
    } else {
        if(GPUState.cull_face == GL_BACK) {
            return (GPUState.front_face == GL_CW) ? GPU_CULLING_CCW : GPU_CULLING_CW;
        } else {
            return (GPUState.front_face == GL_CCW) ? GPU_CULLING_CCW : GPU_CULLING_CW;
        }
    }
}


static int _calc_pvr_depth_test() {
    if(!GPUState.depth_test_enabled) {
        return GPU_DEPTHCMP_ALWAYS;
    }

    switch(GPUState.depth_func) {
        case GL_NEVER:
            return GPU_DEPTHCMP_NEVER;
        case GL_LESS:
            return GPU_DEPTHCMP_GREATER;
        case GL_EQUAL:
            return GPU_DEPTHCMP_EQUAL;
        case GL_LEQUAL:
            return GPU_DEPTHCMP_GEQUAL;
        case GL_GREATER:
            return GPU_DEPTHCMP_LESS;
        case GL_NOTEQUAL:
            return GPU_DEPTHCMP_NOTEQUAL;
        case GL_GEQUAL:
            return GPU_DEPTHCMP_LEQUAL;
        break;
        case GL_ALWAYS:
        default:
            return GPU_DEPTHCMP_ALWAYS;
    }
}


GLboolean _glIsNormalizeEnabled() {
    return GPUState.normalize_enabled;
}

static int _calcPVRBlendFactor(GLenum factor) {
    switch(factor) {
    case GL_ZERO:
        return GPU_BLEND_ZERO;
    case GL_SRC_ALPHA:
        return GPU_BLEND_SRCALPHA;
    case GL_DST_COLOR:
        return GPU_BLEND_DESTCOLOR;
    case GL_DST_ALPHA:
        return GPU_BLEND_DESTALPHA;
    case GL_ONE_MINUS_DST_COLOR:
        return GPU_BLEND_INVDESTCOLOR;
    case GL_ONE_MINUS_SRC_ALPHA:
        return GPU_BLEND_INVSRCALPHA;
    case GL_ONE_MINUS_DST_ALPHA:
        return GPU_BLEND_INVDESTALPHA;
    case GL_ONE:
        return GPU_BLEND_ONE;
    default:
        fprintf(stderr, "Invalid blend mode: %u\n", (unsigned int) factor);
        return GPU_BLEND_ONE;
    }
}

static void _updatePVRBlend(PolyContext* context) {
    if(GPUState.blend_enabled || GPUState.alpha_test_enabled) {
        context->gen.alpha = GPU_ALPHA_ENABLE;
    } else {
        context->gen.alpha = GPU_ALPHA_DISABLE;
    }

    context->blend.src = _calcPVRBlendFactor(GPUState.blend_sfactor);
    context->blend.dst = _calcPVRBlendFactor(GPUState.blend_dfactor);
}

GLboolean _glCheckValidEnum(GLint param, GLint* values, const char* func) {
    GLubyte found = 0;
    while(*values != 0) {
        if(*values == param) {
            found++;
            break;
        }
        values++;
    }

    if(!found) {
        _glKosThrowError(GL_INVALID_ENUM, func);
        return GL_TRUE;
    }

    return GL_FALSE;
}

GLboolean TEXTURES_ENABLED [] = {GL_FALSE, GL_FALSE};

void _glUpdatePVRTextureContext(PolyContext *context, GLshort textureUnit) {
    const TextureObject *tx1 = (textureUnit == 0) ? _glGetTexture0() : _glGetTexture1();

    /* Disable all texturing to start with */
    context->txr.enable = GPU_TEXTURE_DISABLE;
    context->txr2.enable = GPU_TEXTURE_DISABLE;
    context->txr2.alpha = GPU_TXRALPHA_DISABLE;

    if(!TEXTURES_ENABLED[textureUnit] || !tx1) {
        return;
    }

    context->txr.alpha = (GPUState.blend_enabled || GPUState.alpha_test_enabled) ? GPU_TXRALPHA_ENABLE : GPU_TXRALPHA_DISABLE;

    GLuint filter = GPU_FILTER_NEAREST;
    GLboolean enableMipmaps = GL_FALSE;

    switch(tx1->minFilter) {
        case GL_NEAREST_MIPMAP_LINEAR:
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_LINEAR:
        case GL_LINEAR_MIPMAP_NEAREST:
            enableMipmaps = GL_TRUE;
        break;
    default:
        enableMipmaps = GL_FALSE;
        break;
    }

    /* FIXME: If you disable mipmaps on a compressed mipmapped texture
     * you get corruption and I don't know why, so we force mipmapping for now */
    if(tx1->isCompressed && _glIsMipmapComplete(tx1)) {
        enableMipmaps = GL_TRUE;
    }

    if(tx1->height != tx1->width){
        enableMipmaps = GL_FALSE;
    }

    if(enableMipmaps) {
        // FIXME: This is probably the wrong filter for this
        // GL mode, but I'm not sure what's correct...
        if(tx1->minFilter == GL_NEAREST_MIPMAP_LINEAR) {
            filter = GPU_FILTER_TRILINEAR1;
        } else if(tx1->minFilter == GL_LINEAR_MIPMAP_LINEAR) {
            filter = GPU_FILTER_TRILINEAR2;
        } else if(tx1->minFilter == GL_LINEAR_MIPMAP_NEAREST) {
            filter = GPU_FILTER_BILINEAR;
        } else {
            filter = GPU_FILTER_NEAREST;
        }
    } else {
        if(tx1->minFilter == GL_LINEAR && tx1->magFilter == GL_LINEAR) {
            filter = GPU_FILTER_BILINEAR;
        }
    }

    /* If we don't have complete mipmaps, and yet mipmapping was enabled, we disable texturing.
     * This is effectively what standard GL does (it renders a white texture)
     */
    if(!_glIsMipmapComplete(tx1) && enableMipmaps) {
        return;
    }

    if(tx1->data) {
        context->txr.enable = GPU_TEXTURE_ENABLE;
        context->txr.filter = filter;
        context->txr.width = tx1->width;
        context->txr.height = tx1->height;
        context->txr.mipmap = enableMipmaps;
        context->txr.mipmap_bias = tx1->mipmap_bias;

        if(enableMipmaps) {
            context->txr.base = tx1->data;
        } else {
            context->txr.base = tx1->data + tx1->baseDataOffset;
        }

        context->txr.format = tx1->color;

        if(tx1->isPaletted) {
            if(_glIsSharedTexturePaletteEnabled()) {
                TexturePalette* palette = _glGetSharedPalette(tx1->shared_bank);
                if (palette->size  != 16){
                    context->txr.format |= GPUPaletteSelect8BPP(palette->bank);
                }
                else{
                    context->txr.format |= GPUPaletteSelect4BPP(palette->bank);
                }
            }
            else {
                if (tx1->palette->size != 16){
                    context->txr.format |= GPUPaletteSelect8BPP((tx1->palette) ? tx1->palette->bank : 0);
                }
                else{
                    context->txr.format |= GPUPaletteSelect4BPP((tx1->palette) ? tx1->palette->bank : 0);
                }
            }
        }

        context->txr.env = tx1->env;
        context->txr.uv_flip = GPU_UVFLIP_NONE;
        context->txr.uv_clamp = tx1->uv_clamp;
    }
}

GLboolean _glIsLightingEnabled() {
    return GPUState.lighting_enabled;
}

GLboolean _glIsColorMaterialEnabled() {
    return GPUState.color_material_enabled;
}

static GLfloat CLEAR_COLOUR[3];

void _glInitContext() {
    memset(&GL_CONTEXT, 0, sizeof(PolyContext));

    GL_CONTEXT.list_type = GPU_LIST_OP_POLY;
    GL_CONTEXT.fmt.color = GPU_CLRFMT_ARGBPACKED;
    GL_CONTEXT.fmt.uv = GPU_UVFMT_32BIT;
    GL_CONTEXT.gen.color_clamp = GPU_CLRCLAMP_DISABLE;

    const VideoMode* mode = GetVideoMode();

    GPUState.scissor_rect.x = 0;
    GPUState.scissor_rect.y = 0;
    GPUState.scissor_rect.width = mode->width;
    GPUState.scissor_rect.height = mode->height;

    glClearDepth(1.0f);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glShadeModel(GL_SMOOTH);
    glClearColor(0, 0, 0, 0);

    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);

    GLubyte i;
    for(i = 0; i < MAX_GLDC_LIGHTS; ++i) {
        glDisable(GL_LIGHT0 + i);
    }
}

GLAPI void APIENTRY glEnable(GLenum cap) {
    GLboolean was_dirty = GPUState.is_dirty;

    GPUState.is_dirty = GL_TRUE;

    switch(cap) {
        case GL_TEXTURE_2D:
            TEXTURES_ENABLED[_glGetActiveTexture()] = GL_TRUE;
        break;
        case GL_CULL_FACE: {
            GPUState.cull_face = GL_TRUE;
            GL_CONTEXT.gen.culling = _calc_pvr_face_culling();
        } break;
        case GL_DEPTH_TEST: {
            GPUState.depth_test_enabled = GL_TRUE;
            GL_CONTEXT.depth.comparison = _calc_pvr_depth_test();
        } break;
        case GL_BLEND: {
            GPUState.blend_enabled = GL_TRUE;
            _updatePVRBlend(&GL_CONTEXT);
        } break;
        case GL_SCISSOR_TEST: {
            GL_CONTEXT.gen.clip_mode = GPU_USERCLIP_INSIDE;
            _glApplyScissor(false);
        } break;
        case GL_LIGHTING: {
            GPUState.lighting_enabled = GL_TRUE;
        } break;
        case GL_FOG:
            GL_CONTEXT.gen.fog_type = GPU_FOG_TABLE;
        break;
        case GL_COLOR_MATERIAL:
            GPUState.color_material_enabled = GL_TRUE;
        break;
        case GL_SHARED_TEXTURE_PALETTE_EXT: {
            GPUState.shared_palette_enabled = GL_TRUE;
        }
        break;
        case GL_ALPHA_TEST: {
            GPUState.alpha_test_enabled = GL_TRUE;
            _updatePVRBlend(&GL_CONTEXT);
        } break;
        case GL_LIGHT0:
        case GL_LIGHT1:
        case GL_LIGHT2:
        case GL_LIGHT3:
        case GL_LIGHT4:
        case GL_LIGHT5:
        case GL_LIGHT6:
        case GL_LIGHT7:
            _glLightAt(cap & 0xF)->isEnabled = GL_TRUE;
            _glRecalcEnabledLights();
        break;
        case GL_NEARZ_CLIPPING_KOS:
            GPUState.znear_clipping_enabled = GL_TRUE;
        break;
        case GL_POLYGON_OFFSET_POINT:
        case GL_POLYGON_OFFSET_LINE:
        case GL_POLYGON_OFFSET_FILL:
            GPUState.polygon_offset_enabled = GL_TRUE;
        break;
        case GL_NORMALIZE:
            GPUState.normalize_enabled = GL_TRUE;
        break;
    default:
        GPUState.is_dirty = was_dirty;
        break;
    }
}

GLAPI void APIENTRY glDisable(GLenum cap) {
    GLboolean was_dirty = GPUState.is_dirty;
    GPUState.is_dirty = GL_TRUE;

    switch(cap) {
        case GL_TEXTURE_2D: {
            TEXTURES_ENABLED[_glGetActiveTexture()] = GL_FALSE;
        } break;
        case GL_CULL_FACE: {
            GPUState.culling_enabled = GL_FALSE;
            GL_CONTEXT.gen.culling = _calc_pvr_face_culling();
        } break;
        case GL_DEPTH_TEST: {
            GPUState.depth_test_enabled = GL_FALSE;
            GL_CONTEXT.depth.comparison = _calc_pvr_depth_test();
        } break;
        case GL_BLEND:
            GPUState.blend_enabled = GL_FALSE;
            _updatePVRBlend(&GL_CONTEXT);
        break;
        case GL_SCISSOR_TEST: {
            GL_CONTEXT.gen.clip_mode = GPU_USERCLIP_DISABLE;
        } break;
        case GL_LIGHTING: {
            GPUState.lighting_enabled = GL_FALSE;
        } break;
        case GL_FOG:
            GL_CONTEXT.gen.fog_type = GPU_FOG_DISABLE;
        break;
        case GL_COLOR_MATERIAL:
            GPUState.color_material_enabled = GL_FALSE;
        break;
        case GL_SHARED_TEXTURE_PALETTE_EXT: {
            GPUState.shared_palette_enabled = GL_FALSE;
        }
        break;
        case GL_ALPHA_TEST: {
            GPUState.alpha_test_enabled = GL_FALSE;
        } break;
        case GL_LIGHT0:
        case GL_LIGHT1:
        case GL_LIGHT2:
        case GL_LIGHT3:
        case GL_LIGHT4:
        case GL_LIGHT5:
        case GL_LIGHT6:
        case GL_LIGHT7:
            _glEnableLight(cap & 0xF, GL_FALSE);
        break;
        case GL_NEARZ_CLIPPING_KOS:
            GPUState.znear_clipping_enabled = GL_FALSE;
        break;
        case GL_POLYGON_OFFSET_POINT:
        case GL_POLYGON_OFFSET_LINE:
        case GL_POLYGON_OFFSET_FILL:
            GPUState.polygon_offset_enabled = GL_FALSE;
        break;
        case GL_NORMALIZE:
            GPUState.normalize_enabled = GL_FALSE;
        break;
    default:
        GPUState.is_dirty = was_dirty;
        break;
    }
}

/* Clear Caps */
GLAPI void APIENTRY glClear(GLuint mode) {
    if(mode & GL_COLOR_BUFFER_BIT) {
        GPUSetBackgroundColour(CLEAR_COLOUR[0], CLEAR_COLOUR[1], CLEAR_COLOUR[2]);
    }
}

GLAPI void APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    if(r > 1) r = 1;
    if(g > 1) g = 1;
    if(b > 1) b = 1;
    if(a > 1) a = 1;

    /* FIXME: The background-poly doesn't take an alpha value */
    _GL_UNUSED(a);

    CLEAR_COLOUR[0] = r;
    CLEAR_COLOUR[1] = g;
    CLEAR_COLOUR[2] = b;
}

/* Depth Testing */
GLAPI void APIENTRY glClearDepthf(GLfloat depth) {
    glClearDepth(depth);
}

GLAPI void APIENTRY glClearDepth(GLfloat depth) {
    /* We reverse because using invW means that farther Z == lower number */
    GPUSetClearDepth(MIN(1.0f - depth, PVR_MIN_Z));
}

GLAPI void APIENTRY glDrawBuffer(GLenum mode) {
    _GL_UNUSED(mode);

}

GLAPI void APIENTRY glReadBuffer(GLenum mode) {
    _GL_UNUSED(mode);

}

GLAPI void APIENTRY glDepthMask(GLboolean flag) {
    GL_CONTEXT.depth.write = (flag == GL_TRUE) ? GPU_DEPTHWRITE_ENABLE : GPU_DEPTHWRITE_DISABLE;
}

GLAPI void APIENTRY glDepthFunc(GLenum func) {
    GPUState.depth_func = func;
    GPUState.is_dirty = GL_TRUE;
    GL_CONTEXT.depth.comparison = _calc_pvr_depth_test();
}

/* Hints */
/* Currently Supported Capabilities:
      GL_PERSPECTIVE_CORRECTION_HINT - This will Enable  on the PVR */
GLAPI void APIENTRY glHint(GLenum target, GLenum mode) {
    if(target == GL_PERSPECTIVE_CORRECTION_HINT && mode == GL_NICEST) {
        // FIXME: enable supersampling
    }
}

/* Polygon Rasterization Mode */
GLAPI void APIENTRY glPolygonMode(GLenum face, GLenum mode) {
    _GL_UNUSED(face);
    _GL_UNUSED(mode);
}

/* Culling */
GLAPI void APIENTRY glFrontFace(GLenum mode) {
    GPUState.front_face = mode;
    GPUState.is_dirty = GL_TRUE;
    GL_CONTEXT.gen.culling = _calc_pvr_face_culling();
}

GLAPI void APIENTRY glCullFace(GLenum mode) {
    GPUState.cull_face = mode;
    GPUState.is_dirty = GL_TRUE;
    GL_CONTEXT.gen.culling = _calc_pvr_face_culling();
}

GLenum _glGetShadeModel() {
    return (GL_CONTEXT.gen.shading == GPU_SHADE_FLAT) ? GL_FLAT : GL_SMOOTH;
}

/* Shading - Flat or Goraud */
GLAPI void APIENTRY glShadeModel(GLenum mode) {
    GL_CONTEXT.gen.shading = (mode == GL_SMOOTH) ? GPU_SHADE_GOURAUD : GPU_SHADE_FLAT;
}

/* Blending */
GLAPI void APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor) {
    GPUState.blend_sfactor = sfactor;
    GPUState.blend_dfactor = dfactor;
    GPUState.is_dirty = GL_TRUE;
    _updatePVRBlend(&GL_CONTEXT);
}


GLAPI void APIENTRY glAlphaFunc(GLenum func, GLclampf ref) {
    GLint validFuncs[] = {
        GL_GREATER,
        0
    };

    if(_glCheckValidEnum(func, validFuncs, __func__) != 0) {
        return;
    }

    GLubyte val = (GLubyte)(ref * 255.0f);
    GPUSetAlphaCutOff(val);
}

void glLineWidth(GLfloat width) {
    _GL_UNUSED(width);
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
    GPUState.offset_factor = factor;
    GPUState.offset_units = units;
    GPUState.is_dirty = GL_TRUE;
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
    _GL_UNUSED(target);
    _GL_UNUSED(pname);
    _GL_UNUSED(params);
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
    _GL_UNUSED(target);
    _GL_UNUSED(pname);
    _GL_UNUSED(params);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
    _GL_UNUSED(red);
    _GL_UNUSED(green);
    _GL_UNUSED(blue);
    _GL_UNUSED(alpha);
}

void glPixelStorei(GLenum pname, GLint param) {
    _GL_UNUSED(pname);
    _GL_UNUSED(param);
}


void APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {

    if(GPUState.scissor_rect.x == x &&
        GPUState.scissor_rect.y == y &&
        GPUState.scissor_rect.width == width &&
        GPUState.scissor_rect.height == height) {
        return;
    }

    GPUState.scissor_rect.x = x;
    GPUState.scissor_rect.y = y;
    GPUState.scissor_rect.width = width;
    GPUState.scissor_rect.height = height;
    GPUState.scissor_rect.applied = false;
    GPUState.is_dirty = GL_TRUE; // FIXME: do we need this?

    _glApplyScissor(false);
}

/* Setup the hardware user clip rectangle.

   The minimum clip rectangle is a 32x32 area which is dependent on the tile
   size use by the tile accelerator. The PVR swithes off rendering to tiles
   outside or inside the defined rectangle dependant upon the 'clipmode'
   bits in the polygon header.

   Clip rectangles therefore must have a size that is some multiple of 32.

    glScissor(0, 0, 32, 32) allows only the 'tile' in the lower left
    hand corner of the screen to be modified and glScissor(0, 0, 0, 0)
    disallows modification to all 'tiles' on the screen.

    We call this in the following situations:

     - glEnable(GL_SCISSOR_TEST) is called
     - glScissor() is called
     - After glKosSwapBuffers()

    This ensures that a clip command is added to every vertex list
    at the right place, either when enabling the scissor test, or
    when the scissor test changes.
*/
void _glApplyScissor(bool force) {
    /* Don't do anyting if clipping is disabled */
    if(GL_CONTEXT.gen.clip_mode == GPU_USERCLIP_DISABLE) {
        return;
    }

    /* Don't apply if we already applied - nothing changed */
    if(GPUState.scissor_rect.applied && !force) {
        return;
    }

    PVRTileClipCommand c;

    GLint miny, maxx, maxy;

    const VideoMode* vid_mode = GetVideoMode();

    GLsizei scissor_width = MAX(MIN(GPUState.scissor_rect.width, vid_mode->width), 0);
    GLsizei scissor_height = MAX(MIN(GPUState.scissor_rect.height, vid_mode->height), 0);

    /* force the origin to the lower left-hand corner of the screen */
    miny = (vid_mode->height - scissor_height) - GPUState.scissor_rect.y;
    maxx = (scissor_width + GPUState.scissor_rect.x);
    maxy = (scissor_height + miny);

    /* load command structure while mapping screen coords to TA tiles */
    c.flags = GPU_CMD_USERCLIP;
    c.d1 = c.d2 = c.d3 = 0;

    uint16_t vw = vid_mode->width >> 5;
    uint16_t vh = vid_mode->height >> 5;

    c.sx = CLAMP(GPUState.scissor_rect.x >> 5, 0, vw);
    c.sy = CLAMP(miny >> 5, 0, vh);
    c.ex = CLAMP((maxx >> 5) - 1, 0, vw);
    c.ey = CLAMP((maxy >> 5) - 1, 0, vh);

    aligned_vector_push_back(&_glOpaquePolyList()->vector, &c, 1);
    aligned_vector_push_back(&_glPunchThruPolyList()->vector, &c, 1);
    aligned_vector_push_back(&_glTransparentPolyList()->vector, &c, 1);

    GPUState.scissor_rect.applied = true;
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    _GL_UNUSED(func);
    _GL_UNUSED(ref);
    _GL_UNUSED(mask);
}

void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
    _GL_UNUSED(sfail);
    _GL_UNUSED(dpfail);
    _GL_UNUSED(dppass);
}

GLboolean APIENTRY glIsEnabled(GLenum cap) {
    switch(cap) {
    case GL_DEPTH_TEST:
        return GPUState.depth_test_enabled;
    case GL_SCISSOR_TEST:
        return GL_CONTEXT.gen.clip_mode == GPU_USERCLIP_INSIDE;
    case GL_CULL_FACE:
        return GPUState.culling_enabled;
    case GL_LIGHTING:
        return GPUState.lighting_enabled;
    case GL_BLEND:
        return GPUState.blend_enabled;
    case GL_POLYGON_OFFSET_POINT:
    case GL_POLYGON_OFFSET_LINE:
    case GL_POLYGON_OFFSET_FILL:
        return GPUState.polygon_offset_enabled;
    }

    return GL_FALSE;
}

static GLenum COMPRESSED_FORMATS [] = {
    GL_COMPRESSED_ARGB_1555_VQ_KOS,
    GL_COMPRESSED_ARGB_1555_VQ_TWID_KOS,
    GL_COMPRESSED_ARGB_4444_VQ_KOS,
    GL_COMPRESSED_ARGB_4444_VQ_TWID_KOS,
    GL_COMPRESSED_RGB_565_VQ_KOS,
    GL_COMPRESSED_RGB_565_VQ_TWID_KOS
};

static GLuint NUM_COMPRESSED_FORMATS = sizeof(COMPRESSED_FORMATS) / sizeof(GLenum);

void APIENTRY glGetBooleanv(GLenum pname, GLboolean* params) {
    GLuint enabledAttrs = *_glGetEnabledAttributes();
    GLuint activeClientTexture = _glGetActiveClientTexture();

    switch(pname) {
    case GL_TEXTURE_2D:
        *params = TEXTURES_ENABLED[_glGetActiveTexture()];
    break;
    case GL_VERTEX_ARRAY:
        *params = (enabledAttrs & VERTEX_ENABLED_FLAG) == VERTEX_ENABLED_FLAG;
    break;
    case GL_COLOR_ARRAY:
        *params = (enabledAttrs & DIFFUSE_ENABLED_FLAG) == DIFFUSE_ENABLED_FLAG;
    break;
    case GL_NORMAL_ARRAY:
        *params = (enabledAttrs & NORMAL_ENABLED_FLAG) == NORMAL_ENABLED_FLAG;
    break;
    case GL_TEXTURE_COORD_ARRAY: {
        if(activeClientTexture == 0) {
            *params = (enabledAttrs & UV_ENABLED_FLAG) == UV_ENABLED_FLAG;
        } else {
            *params = (enabledAttrs & ST_ENABLED_FLAG) == ST_ENABLED_FLAG;
        }
    } break;
    default:
        _glKosThrowError(GL_INVALID_ENUM, __func__);
    }
}

void APIENTRY glGetFloatv(GLenum pname, GLfloat* params) {
    switch(pname) {
        case GL_PROJECTION_MATRIX:
            MEMCPY4(params, _glGetProjectionMatrix(), sizeof(float) * 16);
        break;
        case GL_MODELVIEW_MATRIX:
            MEMCPY4(params, _glGetModelViewMatrix(), sizeof(float) * 16);
        break;
        case GL_POLYGON_OFFSET_FACTOR:
            *params = GPUState.offset_factor;
        break;
        case GL_POLYGON_OFFSET_UNITS:
            *params = GPUState.offset_units;
        break;
        default:
            _glKosThrowError(GL_INVALID_ENUM, __func__);
            break;
    }
}

void APIENTRY glGetIntegerv(GLenum pname, GLint *params) {
    switch(pname) {
        case GL_MAX_LIGHTS:
            *params = MAX_GLDC_LIGHTS;
        break;
        case GL_TEXTURE_BINDING_2D:
            *params = (_glGetBoundTexture()) ? _glGetBoundTexture()->index : 0;
        break;
        case GL_DEPTH_FUNC:
            *params = GPUState.depth_func;
        break;
        case GL_BLEND_SRC:
            *params = GPUState.blend_sfactor;
        break;
        case GL_BLEND_DST:
            *params = GPUState.blend_dfactor;
        break;
        case GL_MAX_TEXTURE_SIZE:
            *params = MAX_TEXTURE_SIZE;
        break;
        case GL_NUM_COMPRESSED_TEXTURE_FORMATS_ARB:
            *params = NUM_COMPRESSED_FORMATS;
        break;
        case GL_ACTIVE_TEXTURE:
            *params = GL_TEXTURE0 + _glGetActiveTexture();
        break;
        case GL_CLIENT_ACTIVE_TEXTURE:
            *params = GL_TEXTURE0 + _glGetActiveClientTexture();
        break;
        case GL_COMPRESSED_TEXTURE_FORMATS_ARB: {
            GLuint i = 0;
            for(; i < NUM_COMPRESSED_FORMATS; ++i) {
                params[i] = COMPRESSED_FORMATS[i];
            }
        } break;
        case GL_TEXTURE_FREE_MEMORY_ATI:
        case GL_FREE_TEXTURE_MEMORY_KOS:
            *params = _glFreeTextureMemory();
        break;
        case GL_USED_TEXTURE_MEMORY_KOS:
            *params = _glUsedTextureMemory();
        break;
        case GL_FREE_CONTIGUOUS_TEXTURE_MEMORY_KOS:
            *params = _glFreeContiguousTextureMemory();
        break;
    default:
        _glKosThrowError(GL_INVALID_ENUM, __func__);
        break;
    }
}

const GLubyte *glGetString(GLenum name) {
    switch(name) {
        case GL_VENDOR:
            return (const GLubyte*) "KallistiOS / Kazade";

        case GL_RENDERER:
            return (const GLubyte*) "PowerVR2 CLX2 100mHz";

        case GL_VERSION:
            return (const GLubyte*) "1.2 (partial) - GLdc 1.1";

        case GL_EXTENSIONS:
            return (const GLubyte*)"GL_ARB_framebuffer_object, GL_ARB_multitexture, GL_ARB_texture_rg, GL_OES_compressed_paletted_texture, GL_EXT_paletted_texture, GL_EXT_shared_texture_palette, GL_KOS_multiple_shared_palette, GL_ARB_vertex_array_bgra, GL_ARB_vertex_type_2_10_10_10_rev, GL_KOS_texture_memory_management, GL_ATI_meminfo";
    }

    return (const GLubyte*) "GL_KOS_ERROR: ENUM Unsupported\n";
}
