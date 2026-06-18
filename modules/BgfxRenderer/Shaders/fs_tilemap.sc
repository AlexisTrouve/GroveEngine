$input v_texcoord0

#include <bgfx_shader.sh>

// GPU tilemap fragment shader (Slice A2). Per pixel: read the tile id from the index texture and
// sample the atlas.
//
// WHAT  : cell = floor(tileCoord); id = texelFetch(index, cell); if empty -> discard; else sample
//         the atlas at the id's grid cell + the in-tile fraction.
// WHY   : the index texture is INTEGER (R16UI) and must be read with texelFetch (POINT, no filter,
//         no mip) — bilinear-sampling or wrapping tile ids is meaningless and corrupts the lookup.
// HOW   : s_index is a usampler2D (unsigned-integer 2D). id 0 = empty (matches the CPU convention),
//         real ids start at 1 so (id-1) indexes the atlas grid. A2 samples a MONO-texture atlas;
//         A3 swaps this for a texture2DArray to kill the cross-cell bleeding at zoom-out.
USAMPLER2D(s_index, 0);   // R16UI tile-index texture (1 texel = 1 tile id)
SAMPLER2D(s_atlas, 1);    // tile atlas (mono-texture for now)

uniform vec4 u_tilemapGrid;   // x=gridW, y=gridH, z=atlasCols, w=atlasRows

void main()
{
    vec2 grid     = u_tilemapGrid.xy;
    vec2 atlasDim = u_tilemapGrid.zw;

    vec2 tc = v_texcoord0;                                  // 0..grid (tile units)
    ivec2 cell = ivec2(floor(tc));
    cell = clamp(cell, ivec2(0, 0), ivec2(grid) - ivec2(1, 1));   // guard the quad's far edge

    uint id = texelFetch(s_index, cell, 0).r;
    if (id == 0u) {
        discard;                                            // empty tile
    }

    uint a = id - 1u;                                       // 1-based ids -> 0-based atlas index
    uint cols = uint(atlasDim.x);
    vec2 atlasCell = vec2(float(a % cols), float(a / cols));
    vec2 uv = (atlasCell + fract(tc)) / atlasDim;
    gl_FragColor = texture2D(s_atlas, uv);
}
