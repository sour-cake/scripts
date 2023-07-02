#!/bin/bash
#if 0
DIR=$(dirname "$0")
SRC=$DIR/$(basename "$0")
EXE=/tmp/exe-$(basename "$0" .c)
if [ \! -f "$EXE" -o "$SRC" -nt "$EXE" ]; then
	LIBS="sdl2 glesv2"
	tail -n +2 $SRC | gcc -Werror -Wall -std=c99 -O3 -march=native $(pkgconf --cflags $LIBS) -x c - \
		-o $EXE $(pkgconf --libs $LIBS) || exit 1
fi
exec $EXE $@
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <GLES3/gl3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION 1
#include <stb/stb_image_write.h>

#define		STRING(...)		#__VA_ARGS__
#define		COUNTOF(x)		(sizeof(x)/sizeof((x)[0]))
#define		LOG_NEUTRAL		"\x1b[0m"
#define		LOG_BAD			"\x1b[31m"
#define		log_error(...)		log_printf(__func__, LOG_BAD, __VA_ARGS__)
#define		log_info(...)		log_printf(__func__, LOG_NEUTRAL, __VA_ARGS__)

void		log_printf(char const *, char const *type, char const *format, ...)
{
	static char log_buf[4096];
	va_list args;
	va_start(args, format);
	vsnprintf(log_buf, sizeof log_buf, format, args);
	va_end(args);
	if (isatty(STDERR_FILENO))
		fprintf(stderr, "%s%s\x1b[0m\n", type, log_buf);
	else
		fprintf(stderr, "%s\n", log_buf);
}

static int exit_status = EXIT_FAILURE;
static SDL_Rect display_rect;
static SDL_Window *window = NULL;
static SDL_GLContext gl_context = NULL;
static int drawable_w, drawable_h;
static GLuint program, vertex_shader, fragment_shader;
static GLint u_window_size, u_time;
static uint64_t time_freq;
static bool keep_going = true;
static char const *fragment_filename = NULL;
static char const *screencap_dir = ".";
static char screencap_prefix[4096] = "glslview";

char const *	vertex_source = STRING(
void main()
{
	highp vec2 v = 4.0 * vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1) - 1.0;
	gl_Position = vec4(v, 0.0, 1.0);
}
);

static GLchar	fragment_source[1 << 16];

static void	save_screencap()
{
	char fullname[4096];
	bool fullname_taken;
	int number = 0;
	uint8_t *pixels = 0;

	pixels = malloc(4 * drawable_w * drawable_h);
	if (!pixels) {
		log_error("Can't take screencap: Out of memory.");
		goto on_fail;
	}

	glReadPixels(0, 0,
		drawable_w, drawable_h,
		GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	// Could be done a lot better by using readdir().
	do {
		if (snprintf(fullname, sizeof fullname, "%s/%s-%04x.png",
			screencap_dir, screencap_prefix, number) < 0)
		{
			log_error("Can't save screencap: snprintf error.");
			goto on_fail;
		}
		FILE *test = fopen(fullname, "rb");
		if (test) {
			fullname_taken = true;
			number += 1;
			fclose(test);
		} else
			fullname_taken = false;
	} while (fullname_taken);

	if (!stbi_write_png(fullname, drawable_w, drawable_h, 4, pixels, 0)) {
		log_error("Can't save screencap: stb_image error.");
		goto on_fail;
	}

	log_info("Saved screencap \"%s\".", fullname);
on_fail:
	free(pixels);
}

int		main(int argc, char const **argv)
{
	if (argc < 2) {
		if (isatty(STDIN_FILENO)) {
			log_info("Usage:\t%s shader.glsl\n\tcat shader.glsl | %s", argv[0], argv[0]);
			goto on_fail;
		}
		(void)fread(fragment_source, 1, sizeof fragment_source, stdin);
		if (ferror(stdin)) {
			log_error("STDIN reading error.");
			goto on_fail;
		}
	} else if (argc == 2) {
		char const *name = argv[1];
		fragment_filename = argv[1];

		char const *base = strrchr(name, '/');
		if (base)
			base += 1;
		else
			base = name;
		strncpy(screencap_prefix, base, sizeof screencap_prefix - 1);
		char *dot = strrchr(screencap_prefix, '.');
		if (dot) *dot = 0;

		FILE *fp = fopen(name, "rb");
		if (!fp) {
			log_error("\"%s\": %s", name, strerror(errno));
			goto on_fail;
		}
		(void)fread(fragment_source, 1, sizeof fragment_source, fp);
		if (ferror(fp)) {
			log_error("\"%s\": Can't fread.", name);
			fclose(fp);
			goto on_fail;
		}
		fclose(fp);
	} else {
		log_info("Usage:\t%s shader.glsl\n\tcat shader.glsl | %s", argv[0], argv[0]);
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		log_error("SDL_Init(): %s", SDL_GetError());
		goto on_fail;
	}

	if (SDL_GetDisplayBounds(0, &display_rect)) {
		log_error("SDL_GetDisplayBounds(): %s", SDL_GetError());
		display_rect = (SDL_Rect){0, 0, 1920, 1080};
	}

	if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3)
	|| SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0)
	|| SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES)
	|| SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1)
	|| SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0)) {
		log_error("SDL_GL_SetAttribute(): %s", SDL_GetError());
		goto on_fail;
	}

	char window_title[1024];
	if (fragment_filename)
		snprintf(window_title, sizeof window_title, "glslview - \"%s\"", argv[1]);
	else
		snprintf(window_title, sizeof window_title, "glslview");

	window = SDL_CreateWindow(window_title,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		display_rect.w / 2, display_rect.h / 2,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!window) {
		log_error("SDL_CreateWindow(): %s", SDL_GetError());
		goto on_fail;
	}

	gl_context = SDL_GL_CreateContext(window);
	if (!gl_context) {
		log_error("SDL_GL_CreateContext(): %s", SDL_GetError());
		goto on_fail;
	}

	GLchar const *vertex_strings[] = {
		"#version 300 es\n",
		"precision highp float;\n",
		vertex_source,
	};

	GLchar const *fragment_strings[] = {
		"#version 300 es\n",
		"precision highp float;\n",
		fragment_source,
	};

	program = glCreateProgram();
	vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	if (!program || !vertex_shader || !fragment_shader) {
		log_error("Can't create shader program or shader objects.");
		goto on_fail;
	}

	glShaderSource(vertex_shader, COUNTOF(vertex_strings), vertex_strings, NULL);
	glShaderSource(fragment_shader, COUNTOF(fragment_strings), fragment_strings, NULL);
	glCompileShader(vertex_shader);
	glCompileShader(fragment_shader);
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);

	GLint status;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[4096];
		glGetProgramInfoLog(program, sizeof log, NULL, log);
		log_error("Shader linking error:\n%s", log);

		glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &status);
		if (!status) {
			glGetShaderInfoLog(vertex_shader, sizeof log, NULL, log);
			log_error("Vertex shader compilation error:\n%s", log);
		}
		glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &status);
		if (!status) {
			glGetShaderInfoLog(fragment_shader, sizeof log, NULL, log);
			log_error("Fragment shader compilation error:\n%s", log);
		}
		goto on_fail;
	}

	time_freq = SDL_GetPerformanceFrequency();
	u_window_size = glGetUniformLocation(program, "u_window_size");
	u_time = glGetUniformLocation(program, "u_time");
	glUseProgram(program);
	glClearColor(0.f, 0.f, 0.f, 1.f);
	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);

	while (keep_going) {
		SDL_Event event;
		bool do_screencap = false;

		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_KEYDOWN: {
				switch (event.key.keysym.sym) {
				case SDLK_p: {
					do_screencap = true;
				} break;
				case SDLK_ESCAPE: {
					keep_going = false;
				} break;
				}
			} break;
			case SDL_WINDOWEVENT: {
				switch (event.window.event) {
				case SDL_WINDOWEVENT_SIZE_CHANGED: {
					SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
					glViewport(0, 0, drawable_w, drawable_h);
				} break;
				}
			} break;
			case SDL_QUIT: {
				keep_going = false;
			} break;
			}
		}

		glClear(GL_COLOR_BUFFER_BIT);
		if (u_window_size >= 0) glUniform2f(u_window_size, drawable_w, drawable_h);
		if (u_time >= 0) glUniform1f(u_time, (double)SDL_GetPerformanceCounter() / time_freq);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		if (do_screencap) save_screencap();
		SDL_GL_SwapWindow(window);
	}

	exit_status = EXIT_SUCCESS;
on_fail:
	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return exit_status;
}

