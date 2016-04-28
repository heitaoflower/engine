in vec3 v_pos;
in vec3 v_color;
in vec3 v_lightpos;
in float v_fogrange;
in vec3 v_fogcolor;
in float v_viewdistance;
in float v_ambientocclusion;

out vec4 o_color;

void main(void) {
	vec3 fdx = dFdx(v_pos.xyz);
	vec3 fdy = dFdy(v_pos.xyz);
	vec3 normal = normalize(cross(fdx, fdy));
	vec3 lightdir = normalize(v_lightpos - v_pos);

	float diffuse = clamp(dot(normal, lightdir), 0.0, 1.0) * 0.7;
	float ambient = 0.3;
	float lightvalue = diffuse + ambient;

	float fogstart = max(v_viewdistance - v_fogrange, 0.0);
	float fogdistance = gl_FragCoord.z / gl_FragCoord.w;
	float fogval = 1.0 - clamp((v_viewdistance - fogdistance) / (v_viewdistance - fogstart), 0.0, 1.0);
	o_color = vec4(mix(v_color * v_ambientocclusion * lightvalue, v_fogcolor, fogval), 1.0);
}
