/*
 * Copyright (C) 2009 Maciej Cencora <m.cencora@gmail.com>
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "radeon_common.h"
#include "r300_context.h"

#include "main/image.h"
#include "main/teximage.h"
#include "main/texstate.h"
#include "drivers/common/meta.h"

#include "radeon_mipmap_tree.h"
#include "r300_blit.h"
#include <main/debug.h>

static GLboolean
do_copy_texsubimage(GLcontext *ctx,
                    GLenum target, GLint level,
                    struct radeon_tex_obj *tobj,
                    radeon_texture_image *timg,
                    GLint dstx, GLint dsty,
                    GLint x, GLint y,
                    GLsizei width, GLsizei height)
{
    struct r300_context *r300 = R300_CONTEXT(ctx);
    struct radeon_renderbuffer *rrb;

    if (_mesa_get_format_bits(timg->base.TexFormat, GL_DEPTH_BITS) > 0) {
        rrb = radeon_get_depthbuffer(&r300->radeon);
    } else {
        rrb = radeon_get_colorbuffer(&r300->radeon);
    }

    if (!timg->mt) {
        radeon_validate_texture_miptree(ctx, &tobj->base);
    }

    assert(rrb && rrb->bo);
    assert(timg->mt->bo);
    assert(timg->base.Width >= dstx + width);
    assert(timg->base.Height >= dsty + height);

    intptr_t src_offset = rrb->draw_offset + x * rrb->cpp + y * rrb->pitch;
    intptr_t dst_offset = radeon_miptree_image_offset(timg->mt, _mesa_tex_target_to_face(target), level);
    dst_offset += dstx * _mesa_get_format_bytes(timg->base.TexFormat) +
                  dsty * _mesa_format_row_stride(timg->base.TexFormat, timg->base.Width);

    if (src_offset % 32 || dst_offset % 32) {
        return GL_FALSE;
    }

    if (0) {
        fprintf(stderr, "%s: copying to face %d, level %d\n",
                __FUNCTION__, _mesa_tex_target_to_face(target), level);
        fprintf(stderr, "to: x %d, y %d, offset %d\n", dstx, dsty, (uint32_t) dst_offset);
        fprintf(stderr, "from (%dx%d) width %d, height %d, offset %d, pitch %d\n",
                x, y, rrb->base.Width, rrb->base.Height, (uint32_t) src_offset, rrb->pitch/rrb->cpp);
        fprintf(stderr, "src size %d, dst size %d\n", rrb->bo->size, timg->mt->bo->size);

    }

    /* blit from src buffer to texture */
    return r300_blit(r300, rrb->bo, src_offset, rrb->base.Format, rrb->pitch/rrb->cpp,
                     rrb->base.Width, rrb->base.Height, timg->mt->bo ? timg->mt->bo : timg->bo, dst_offset,
                     timg->base.TexFormat, timg->base.Width, width, height);
}

static void
r300CopyTexImage2D(GLcontext *ctx, GLenum target, GLint level,
                   GLenum internalFormat,
                   GLint x, GLint y, GLsizei width, GLsizei height,
                   GLint border)
{
    struct gl_texture_unit *texUnit = _mesa_get_current_tex_unit(ctx);
    struct gl_texture_object *texObj =
        _mesa_select_tex_object(ctx, texUnit, target);
    struct gl_texture_image *texImage =
        _mesa_select_tex_image(ctx, texObj, target, level);
    int srcx, srcy, dstx, dsty;

    if (border)
        goto fail;

    /* Setup or redefine the texture object, mipmap tree and texture
     * image.  Don't populate yet.
     */
    ctx->Driver.TexImage2D(ctx, target, level, internalFormat,
                           width, height, border,
                           GL_RGBA, GL_UNSIGNED_BYTE, NULL,
                           &ctx->DefaultPacking, texObj, texImage);

    srcx = x;
    srcy = y;
    dstx = 0;
    dsty = 0;
    if (!_mesa_clip_copytexsubimage(ctx,
                                    &dstx, &dsty,
                                    &srcx, &srcy,
                                    &width, &height)) {
        return;
    }

    if (!do_copy_texsubimage(ctx, target, level,
                             radeon_tex_obj(texObj), (radeon_texture_image *)texImage,
                             0, 0, x, y, width, height)) {
        goto fail;
    }

    return;

fail:
    _mesa_meta_CopyTexImage2D(ctx, target, level, internalFormat, x, y,
                              width, height, border);
}

static void
r300CopyTexSubImage2D(GLcontext *ctx, GLenum target, GLint level,
                      GLint xoffset, GLint yoffset,
                      GLint x, GLint y,
                      GLsizei width, GLsizei height)
{
    struct gl_texture_unit *texUnit = _mesa_get_current_tex_unit(ctx);
    struct gl_texture_object *texObj = _mesa_select_tex_object(ctx, texUnit, target);
    struct gl_texture_image *texImage = _mesa_select_tex_image(ctx, texObj, target, level);

    if (!do_copy_texsubimage(ctx, target, level,
                             radeon_tex_obj(texObj), (radeon_texture_image *)texImage,
                             xoffset, yoffset, x, y, width, height)) {

       //DEBUG_FALLBACKS

        _mesa_meta_CopyTexSubImage2D(ctx, target, level,
                                     xoffset, yoffset, x, y, width, height);
    }
}


void r300_init_texcopy_functions(struct dd_function_table *table)
{
    table->CopyTexImage2D = r300CopyTexImage2D;
    table->CopyTexSubImage2D = r300CopyTexSubImage2D;
}