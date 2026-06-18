$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

// GPU tilemap vertex shader (Slice A2). ONE quad per chunk — no per-tile instancing.
//
// WHAT  : scale the unit quad (0..1) to the chunk's pixel rectangle and place it at the chunk's
//         world origin; hand the fragment shader the per-pixel TILE coordinate (0..gridW, 0..gridH)
//         so it can texelFetch the index texture.
// WHY   : the whole point of the index-texture path is that the draw cost is independent of tile
//         count — 1 quad covers a whole chunk; the fragment shader resolves each pixel's tile.
// HOW   : worldPos = origin + quad * (grid * tilePix). v_texcoord0 = quad * grid spans the grid in
//         TILE units, so floor() in the fragment shader yields the cell and fract() the in-tile UV.
uniform vec4 u_tilemapParams;   // x=originX, y=originY (world), z=tilePixW, w=tilePixH
uniform vec4 u_tilemapGrid;     // x=gridW(tiles), y=gridH(tiles), z=atlasCols, w=atlasRows

void main()
{
    vec2 origin  = u_tilemapParams.xy;
    vec2 tilePix = u_tilemapParams.zw;
    vec2 grid    = u_tilemapGrid.xy;

    vec2 worldPos = origin + a_position.xy * grid * tilePix;
    gl_Position = mul(u_viewProj, vec4(worldPos, 0.0, 1.0));

    v_texcoord0 = a_position.xy * grid;   // tile-space coordinate (0..grid)
}
