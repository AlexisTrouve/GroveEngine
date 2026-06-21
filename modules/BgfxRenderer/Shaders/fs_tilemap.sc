$input v_texcoord0

#include <bgfx_shader.sh>

// GPU tilemap fragment shader (Slice B). Per pixel, blend two representations by how many tiles
// fall under one pixel:
//   - DETAIL band (zoomed in): tile id from the index texture -> atlas array layer (crisp).
//   - LOD band (zoomed out):   the mipped color texture, trilinear-sampled (smooth, alias-free).
//
// WHY  : an index texture can't be mipped (averaging ids is meaningless), so zoom-out would alias
//        on tile borders. The separate mipped COLOR texture (Slice B1) is the mippable stand-in;
//        crossfading to it as tiles/pixel grows gives a seamless zoom across the whole range.
// HOW  : tiles-per-pixel = length(fwidth(tileCoord)); k = smoothstep(1,4, tpp) blends detail->LOD.
//        The LOD sample uses the NORMALIZED uv (tileCoord/grid), whose screen derivatives drive the
//        hardware mip selection automatically — no textureGrad needed (the atlas array has no mips).
USAMPLER2D(s_index, 0);        // R16UI tile-index texture
SAMPLER2DARRAY(s_atlas, 1);    // detail atlas: one tile type per layer
SAMPLER2D(s_lod, 2);           // mipped LOD color texture (1 texel/tile + box-filter mips)
SAMPLER2D(s_fog, 3);           // mipped R8 per-tile visibility (1 = visible, 0 = hidden)
SAMPLER2D(s_fognoise, 4);      // tiled fog texture (wrap=Repeat); hidden tiles show this, not black

uniform vec4 u_tilemapGrid;    // x=gridW, y=gridH (z,w unused)
uniform vec4 u_tilemapParams;  // x=originX, y=originY, z=tilePixW, w=tilePixH (world-space fog uv)
uniform vec4 u_tileAnim[4];    // animated tiles: per entry x=tileId, y=frameCount, z=fps (w unused)
uniform vec4 u_tileAnimMeta;   // x=animCount, y=time(seconds) (z,w unused)

void main()
{
    vec2 grid = u_tilemapGrid.xy;
    vec2 tc   = v_texcoord0;                                 // tile-space coordinate (0..grid)

    // Tiles per pixel (screen-space derivative of the tile coordinate).
    float tpp = length(fwidth(tc));

    // Detail band: index -> atlas array layer.
    ivec2 cell = ivec2(floor(tc));
    cell = clamp(cell, ivec2(0, 0), ivec2(grid) - ivec2(1, 1));
    uint id = texelFetch(s_index, cell, 0).r;
    // Animated tiles: the index stores the tile's BASE id (uploaded once); cycle the atlas LAYER over
    // time for declared ids. frames are CONSECUTIVE layers from the base (id-1); frame = floor(time*fps)
    // mod frameCount (mirror of grove::tilemap::animFrame). Constant 16 loop = shaderc-safe; unused
    // entries are zeroed (won't match a real id). Guarded so the no-animation case stays free.
    float layer = float(id - 1u);
    if (id != 0u && u_tileAnimMeta.x > 0.5) {
        for (int ai = 0; ai < 4; ai++) {
            vec4 e = u_tileAnim[ai];
            if (uint(e.x) == id) {
                layer = float(id - 1u) + mod(floor(u_tileAnimMeta.y * e.z), e.y);
            }
        }
    }
    vec4 detail = (id == 0u) ? vec4(0.0, 0.0, 0.0, 0.0)
                             : texture2DArray(s_atlas, vec3(fract(tc), layer));

    // LOD band: mipped color, hardware-selected mip from the normalized-uv derivatives.
    vec4 lod = texture2D(s_lod, tc / grid);

    // Crossfade detail -> LOD as the view zooms out. The detail band is POINT-sampled (no atlas
    // mips), so it starts to alias/moiré once a pixel covers ~half a tile (Nyquist). The mipped LOD
    // is smooth from mip 1 up, so we hand over to it EARLY — fully by ~1 tile/pixel — to kill the
    // shimmer in the transition zone. (Crossfade window is a tuning knob, per the design.)
    float k = smoothstep(0.5, 1.0, tpp);
    vec4 col = mix(detail, lod, k);

    // Fog of war (Slice fog): scalar visibility blends the tile with a TILED fog texture. vis=1 ->
    // tile, vis=0 -> fog. Mipped R8 mask -> dims correctly at every zoom. The fog texture samples at
    // a WORLD-space uv (so the fog is anchored to the terrain) and wraps (Repeat) -> tiles seamlessly.
    // With the default 1x1 black fog texture this reduces to the old "hidden -> black".
    float vis = texture2D(s_fog, tc / grid).r;
    vec2 worldPos = u_tilemapParams.xy + tc * u_tilemapParams.zw;
    vec3 fogColor = texture2D(s_fognoise, worldPos / 64.0).rgb;
    col.rgb = mix(fogColor, col.rgb, vis);

    if (col.a < 0.01) {
        discard;                                             // fully empty -> nothing to draw
    }
    gl_FragColor = col;
}
