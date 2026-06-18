$input v_texcoord0

#include <bgfx_shader.sh>

// GPU tilemap fragment shader (Slice A3). Per pixel: read the tile id from the index texture and
// sample the atlas — now a texture2DArray, one tile type per LAYER.
//
// WHAT  : cell = floor(tileCoord); id = texelFetch(index, cell); if empty -> discard; else sample
//         the atlas array at layer (id-1), sub-UV = fract(tileCoord).
// WHY   : a texture2DArray kills the cross-tile bleeding a packed atlas suffers under minification
//         — sub-UV stays in [0,1] WITHIN a layer, so there is no neighbour cell to bleed into, and
//         mips are per-tile-correct. The id maps straight to a layer; no grid-UV math at all.
// HOW   : s_index is usampler2D (POINT/CLAMP). id 0 = empty; real ids start at 1 so (id-1) is the
//         0-based layer. fract(tileCoord) is the position inside the tile.
USAMPLER2D(s_index, 0);        // R16UI tile-index texture (1 texel = 1 tile id)
SAMPLER2DARRAY(s_atlas, 1);    // tile atlas: one tile type per array layer

uniform vec4 u_tilemapGrid;    // x=gridW, y=gridH (z,w now unused — the atlas is an array)

void main()
{
    vec2 grid = u_tilemapGrid.xy;

    vec2 tc = v_texcoord0;                                  // 0..grid (tile units)
    ivec2 cell = ivec2(floor(tc));
    cell = clamp(cell, ivec2(0, 0), ivec2(grid) - ivec2(1, 1));   // guard the quad's far edge

    uint id = texelFetch(s_index, cell, 0).r;
    if (id == 0u) {
        discard;                                            // empty tile
    }

    float layer = float(id - 1u);                           // 1-based id -> 0-based array layer
    gl_FragColor = texture2DArray(s_atlas, vec3(fract(tc), layer));
}
