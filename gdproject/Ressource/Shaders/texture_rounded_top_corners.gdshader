shader_type canvas_item;

// UV radii: corner_px / panel_size (see TextureRect minimum size in scene)
uniform vec2 radius_uv_tl = vec2(0.077419, 0.08);

uniform vec2 radius_uv_tr = vec2(0.077419, 0.08);

uniform float aa : hint_range(0.0, 0.02) = 0.004;

void fragment() {
	vec4 col = texture(TEXTURE, UV);
	vec2 uv = UV;
	float a = col.a;
	vec2 r;
	vec2 c;

	r = radius_uv_tl;
	if (r.x > 0.001 && r.y > 0.001 && uv.x < r.x && uv.y < r.y) {
		c = r;
		float f = length((uv - c) / r);
		a *= 1.0 - smoothstep(1.0 - aa, 1.0 + aa, f);
	}

	r = radius_uv_tr;
	if (r.x > 0.001 && r.y > 0.001 && uv.x > 1.0 - r.x && uv.y < r.y) {
		c = vec2(1.0 - r.x, r.y);
		float f = length((uv - c) / r);
		a *= 1.0 - smoothstep(1.0 - aa, 1.0 + aa, f);
	}

	col.a = a;
	COLOR = col;
}
