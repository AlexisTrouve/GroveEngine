$input a_position, a_color0
$output v_texcoord0

#include <bgfx_shader.sh>

// Full-screen composite — vertex stage.
//
// The quad arrives ALREADY IN CLIP SPACE (CompositePass builds it at -1..1), so there is no
// u_modelViewProj here on purpose: the composite must cover the screen whatever the world camera is
// doing. Multiplying by the view/projection would drag it around with the zoom and pan.
//
// The texture coordinate is derived from the position rather than shipped as an attribute, because
// the vertex layout in use (PosColor) has no texcoord slot and the mapping is a fixed -1..1 -> 0..1.
void main()
{
	gl_Position = vec4(a_position, 1.0);

	vec2 uv = a_position.xy * 0.5 + 0.5;

	// Render-target origin differs by API: OpenGL samples from the bottom-left, D3D/Metal from the
	// top-left. Without this flip the composited frame comes out upside down on one family of
	// backends — and only on that family, which is exactly the kind of defect that survives a test
	// run on one machine.
#if BGFX_SHADER_LANGUAGE_GLSL
	v_texcoord0 = uv;
#else
	v_texcoord0 = vec2(uv.x, 1.0 - uv.y);
#endif

	// a_color0 is unused (the layout carries it, the composite has no per-vertex colour).
	vec4 unused = a_color0;
}
