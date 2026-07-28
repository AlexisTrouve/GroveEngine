#include "TextPass.h"
#include "../RHI/RHIDevice.h"
#include "../Frame/FramePacket.h"
#include "../Text/Utf8.h"
#include "../Text/TextFit.h"   // pure ellipsis truncation (unit-tested headlessly)
#include <cstring>   // std::memcpy into the transient glyph buffer

namespace grove {

TextPass::TextPass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
    m_glyphInstances.reserve(MAX_GLYPHS_PER_BATCH);
}

void TextPass::setup(rhi::IRHIDevice& device) {
    // Initialize default bitmap font
    if (!m_font.initDefault(device)) {
        // Font init failed - text rendering will be disabled
        return;
    }

    // Create quad vertex buffer (unit quad, instanced) - same as SpritePass
    float quadVertices[] = {
        // pos.x, pos.y, pos.z,    r,    g,    b,    a
        0.0f, 0.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // bottom-left
        1.0f, 0.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // bottom-right
        1.0f, 1.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // top-right
        0.0f, 1.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,  // top-left
    };

    rhi::BufferDesc vbDesc;
    vbDesc.type = rhi::BufferDesc::Vertex;
    vbDesc.size = sizeof(quadVertices);
    vbDesc.data = quadVertices;
    vbDesc.dynamic = false;
    vbDesc.layout = rhi::BufferDesc::PosColor;
    m_quadVB = device.createBuffer(vbDesc);

    // Create index buffer
    uint16_t quadIndices[] = {
        0, 1, 2,
        0, 2, 3
    };

    rhi::BufferDesc ibDesc;
    ibDesc.type = rhi::BufferDesc::Index;
    ibDesc.size = sizeof(quadIndices);
    ibDesc.data = quadIndices;
    ibDesc.dynamic = false;
    m_quadIB = device.createBuffer(ibDesc);

    // Create dynamic instance buffer for glyphs
    rhi::BufferDesc instDesc;
    instDesc.type = rhi::BufferDesc::Instance;
    instDesc.size = MAX_GLYPHS_PER_BATCH * sizeof(SpriteInstance);
    instDesc.data = nullptr;
    instDesc.dynamic = true;
    m_instanceBuffer = device.createBuffer(instDesc);

    // Create texture sampler uniform
    m_textureSampler = device.createUniform("s_texColor", 1);
}

void TextPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_instanceBuffer);
    device.destroy(m_textureSampler);
    m_font.shutdown(device);
    m_fontBold.shutdown(device);
}

void TextPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    if (!m_font.isValid()) {
        return;
    }

    // World text on view 0 (world camera), HUD text on view 1 (fixed screen-space overlay).
    renderTextSet(device, cmd, frame.texts, frame.textCount, 0);
    renderTextSet(device, cmd, frame.hudTexts, frame.hudTextCount, 1);
}

void TextPass::renderTextSet(rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd,
                             const TextCommand* texts, size_t count, rhi::ViewId viewId) {
    if (count == 0) {
        return;
    }

    // Set render state for text (alpha blending, no depth)
    rhi::RenderState state;
    state.blend = rhi::BlendMode::Alpha;
    state.cull = rhi::CullMode::None;
    state.depthTest = false;
    state.depthWrite = false;

    m_glyphInstances.clear();

    // Clip currently accumulated in m_glyphInstances. Glyphs from text commands that share a clip
    // batch together; a clip change flushes first so each draw gets its own bgfx scissor.
    float curClip[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Font currently accumulated. Regular and bold are DIFFERENT ATLASES, so a face change must break
    // the batch exactly like a clip change does — one draw binds one texture.
    const BitmapFont* curFont = &m_font;

    // Flush the accumulated glyphs as one scissored draw. Transient buffer so multiple flushes in a
    // frame (a clipped scroll list, or a >MAX-glyph run) don't clobber one shared dynamic buffer.
    auto flush = [&]() {
        const uint32_t n = static_cast<uint32_t>(m_glyphInstances.size());
        if (n == 0) return;
        rhi::TransientInstanceBuffer tib = device.allocTransientInstanceBuffer(n);
        cmd.setState(state);   // state is consumed per submit -> set it before EACH flush
        if (curClip[2] > 0.0f) {
            cmd.setScissor(static_cast<uint16_t>(curClip[0]), static_cast<uint16_t>(curClip[1]),
                           static_cast<uint16_t>(curClip[2]), static_cast<uint16_t>(curClip[3]));
        }
        cmd.setVertexBuffer(m_quadVB);
        cmd.setIndexBuffer(m_quadIB);
        if (tib.isValid()) {
            std::memcpy(tib.data, m_glyphInstances.data(), n * sizeof(SpriteInstance));
            cmd.setTransientInstanceBuffer(tib, 0, n);
        } else {
            device.updateBuffer(m_instanceBuffer, m_glyphInstances.data(), n * sizeof(SpriteInstance));
            cmd.setInstanceBuffer(m_instanceBuffer, 0, n);
        }
        cmd.setTexture(0, curFont->getTexture(), m_textureSampler);
        cmd.drawInstanced(6, n);
        cmd.submit(viewId, m_shader, 0);
        m_glyphInstances.clear();
    };

    // Convert each TextCommand into glyph instances
    for (size_t i = 0; i < count; ++i) {
        const TextCommand& textCmd = texts[i];

        if (!textCmd.text) continue;

        // A clip change between text commands ends the current batch (its glyphs flush under the
        // previous clip); the new clip then applies to what follows.
        const float clip[4] = {textCmd.clipX, textCmd.clipY, textCmd.clipW, textCmd.clipH};
        if (!m_glyphInstances.empty() &&
            (clip[0] != curClip[0] || clip[1] != curClip[1] ||
             clip[2] != curClip[2] || clip[3] != curClip[3])) {
            flush();
        }
        curClip[0] = clip[0]; curClip[1] = clip[1]; curClip[2] = clip[2]; curClip[3] = clip[3];

        // Pick the face: a real bold atlas when one is loaded AND this command asks for bold, else the
        // regular one. Same rule as the clip above — a face change flushes first, since the two atlases
        // are different textures and one draw binds one texture.
        const bool wantBold = (textCmd.bold != 0);
        const bool realBold = wantBold && m_fontBold.isValid();
        const BitmapFont* face = realBold ? &m_fontBold : &m_font;
        if (!m_glyphInstances.empty() && face != curFont) {
            flush();
        }
        curFont = face;
        const BitmapFont& font = *face;

        // Calculate scale factor based on font size
        float scale = static_cast<float>(textCmd.fontSize) / font.getBaseSize();

        // Extract color components
        uint32_t color = textCmd.color;
        float r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
        float g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
        float b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
        float a = static_cast<float>(color & 0xFF) / 255.0f;

        // Horizontal alignment: measure each line's pixel width and shift its start so `x` means left edge
        // (factor 0), centre (0.5) or right edge (1). Measured per line so multi-line text aligns line by line.
        const float alignFactor = (textCmd.align == 1) ? 0.5f : (textCmd.align == 2) ? 1.0f : 0.0f;
        // Synthetic bold: a second glyph copy shifted by ~1px in x thickens a single-weight font. Only
        // used as a FALLBACK now — with a real bold face loaded it would double-draw an already-bold
        // glyph and read as a smear.
        const float boldOffset = (wantBold && !realBold) ? (scale > 1.0f ? scale : 1.0f) : 0.0f;

        // On-screen advance of one codepoint. Handed to the PURE fitter, so the truncation logic stays
        // font-agnostic and unit-testable headlessly (Text/TextFit.h + TextFitUnit).
        auto advOf = [&](uint32_t c) { return font.getGlyph(c).advance * scale; };
        const float ellipsisW = 3.0f * advOf('.');

        float cursorX = 0.0f;
        float cursorY = textCmd.y;

        // Emit ONE glyph at the cursor and advance it. Factored out so the ellipsis dots take exactly
        // the same path as real text — same bold, colour, layer and batching, no second code path.
        auto emitGlyph = [&](uint32_t cp) {
            const GlyphInfo& glyph = font.getGlyph(cp);

            // Create sprite instance for this glyph
            SpriteInstance inst;

            // Position (top-left of glyph)
            inst.x = cursorX + glyph.offsetX * scale;
            inst.y = cursorY + glyph.offsetY * scale;

            // Scale to glyph size
            inst.scaleX = glyph.width * scale;
            inst.scaleY = glyph.height * scale;

            // No rotation
            inst.rotation = 0.0f;

            // UVs from font atlas
            inst.u0 = glyph.u0;
            inst.v0 = glyph.v0;
            inst.u1 = glyph.u1;
            inst.v1 = glyph.v1;

            // Texture ID (font atlas = 0)
            inst.textureId = 0.0f;

            // Layer
            inst.layer = static_cast<float>(textCmd.layer);

            // Padding/reserved
            inst.padding0 = 0.0f;
            inst.reserved[0] = 0.0f;
            inst.reserved[1] = 0.0f;
            inst.reserved[2] = 0.0f;
            inst.reserved[3] = 0.0f;

            // Color
            inst.r = r;
            inst.g = g;
            inst.b = b;
            inst.a = a;

            m_glyphInstances.push_back(inst);

            // Synthetic bold: a shifted duplicate of the same glyph (same UV/color) thickens the stroke.
            if (boldOffset > 0.0f) {
                SpriteInstance b = inst;
                b.x += boldOffset;
                m_glyphInstances.push_back(b);
            }

            // Advance cursor
            cursorX += glyph.advance * scale;

            // Flush when the glyph batch is full (keeps within MAX per draw). Bold pushes 2/glyph, so the
            // batch can be one over MAX here — check leaves headroom (MAX is a soft cap, buffer sized to it).
            if (m_glyphInstances.size() >= MAX_GLYPHS_PER_BATCH - 1) {
                flush();
            }
        };

        // OVERFLOW: `maxWidth` (0 = unlimited — the default, so every existing caller is byte-identical)
        // caps each line. A line past it is cut on a WHOLE codepoint and finished with "...": clipping
        // mid-glyph reads as a rendering bug, an ellipsis reads as "there is more text", which is true.
        //
        // Walk line by line: fit, align on the FITTED width (a truncated line must align on what is
        // actually drawn, not on the text it would have had), emit, then the dots if it was cut.
        const char* ptr = textCmd.text;
        while (*ptr) {
            const grove::text::FitResult fit =
                grove::text::fitLine(ptr, textCmd.maxWidth, advOf, ellipsisW);
            cursorX = textCmd.x - alignFactor * fit.width;

            const char* lineEnd = ptr + fit.bytes;
            while (ptr < lineEnd) {
                // Decode a full UTF-8 codepoint so accents (é, ç…) map to one glyph, instead of
                // reading each byte of a multi-byte char as a separate glyph.
                emitGlyph(decodeUtf8(ptr));
            }
            if (fit.ellipsis) {
                for (int d = 0; d < 3; ++d) emitGlyph('.');
                while (*ptr && *ptr != '\n') ++ptr;   // drop what was cut
            }

            if (*ptr == '\n') {                       // next line
                ++ptr;
                cursorY += font.getLineHeight() * scale;
            }
        }
    }

    flush();  // remaining glyphs
}

} // namespace grove
