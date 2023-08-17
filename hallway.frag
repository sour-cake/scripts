uniform vec2	u_window_size;
uniform float	u_time;
uniform vec2	u_mouse;
out vec3	o_color;

const float	tau = 6.283185307179586;

float		rand1(in vec2 v)
{
	return fract(tan(mod(dot(v, vec2(1.164062, 6.460938)), tau)) * 332.367188);
}

float		noise(in vec2 v)
{
	vec2 i = floor(v);
	vec2 f = fract(v);
	float a = rand1(i + vec2(0.0, 0.0));
	float b = rand1(i + vec2(1.0, 0.0));
	float c = rand1(i + vec2(0.0, 1.0));
	float d = rand1(i + vec2(1.0, 1.0));
	vec2 t = f*f*(3.0 - 2.0*f);
	return mix(mix(a, b, t.x), mix(c, d, t.x), t.y);
}

float		fbm(in vec2 v)
{
	float y = 0.0;
	float amp = 0.5;
	mat2 rot = mat2(cos(0.5), sin(0.5), -cos(0.5), sin(0.5));
	v = rot * v;
	for (int i = 0; i < 8; ++i) {
		y += amp * noise(v);
		v = rot * v * 2.0 + 100.0;
		amp *= 0.5;
	}
	return y;
}

bool		wall(
	inout float hit_dist, inout vec3 hit_normal, inout vec3 hit_color,
	in vec3 ray_pos, in vec3 ray_dir,
	in vec3 plane_pos, mat3 plane_tbn)
{
	float d = dot(ray_pos - plane_pos, plane_tbn[2]) / -dot(ray_dir, plane_tbn[2]);
	bool hit = 0.0 <= d && d < hit_dist;
	if (hit) {
		hit_dist = d;
		hit_normal = plane_tbn[2];

		vec3 hit_pos = ray_pos + ray_dir*d;
		vec2 uv = vec2(dot(hit_pos, plane_tbn[0]), dot(hit_pos, plane_tbn[1]));

		hit_color = vec3(0.7 + fbm(10.0*uv)*0.2);
	}
	return hit;
}

const vec3 plane_pos[] = vec3[](
	vec3(0, -2, 0),
	vec3(0, 2, 0),
	vec3(-1, 0, 0),
	vec3(1, 0, 0)
);
const mat3 plane_tbn[] = mat3[](
	mat3(1, 0, 0, 0, 0, 1, 0, 1, 0),
	mat3(-1, 0, 0, 0, 0, 1, 0, -1, 0),
	mat3(0, 0, 1, 0, 1, 0, 1, 0, 0),
	mat3(0, 0, -1, 0, 1, 0, -1, 0, 0)
);

void		main()
{


	vec3 camera_pos = vec3(1.8*(u_mouse / u_window_size - 0.5), 10.0*u_time);
	vec3 ray_dir = normalize(vec3((gl_FragCoord.xy - u_window_size/2.0) * 2.0/u_window_size.xx, 1.0));
	float max_dist = 40.0;
	float hit_dist = max_dist;
	vec3 hit_normal = vec3(0);
	vec3 hit_color = vec3(0);

	for (int i = 0; i < 4; ++i) {
		wall(hit_dist, hit_normal, hit_color, camera_pos, ray_dir, plane_pos[i], plane_tbn[i]);
	}

	vec3 hit_pos = camera_pos + hit_dist*ray_dir;

	float light_power = 0.2;

	o_color = vec3(0);

	float camera_z = floor(camera_pos.z/5.0)*5.0;
	for (float i = -2.0; i < 10.0; ++i) {
		vec3 to_light = vec3(0, 0, camera_z + 5.0*i) - hit_pos;
		float to_light_d = length(to_light);
		to_light /= to_light_d;
		o_color += (light_power / to_light_d) * max(0.0, dot(to_light, hit_normal));
	}
	o_color *= hit_color;
	o_color = mix(vec3(0), o_color, clamp((max_dist - hit_dist)/10.0, 0., 1.));

	o_color /= o_color + 1.0;
	o_color = pow(o_color, vec3(1.0/2.2));
}
