/*
 * Copyright (c) 2012 Rob Clark <robdclark@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <GLES3/gl31.h>
#include "test-util-3d.h"

int openfile(const char *fmt, int i)
{
	static char path[256];
	sprintf(path, fmt, i);
	return open(path, 0);
}

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define NVERT 3

static const char *attrnames[] = {
		"aFoo", "aPosition", "aPosition0", "aPosition1", "aPosition2",
		"aTexCoord", "aTexCoord0", "in_position", "a_coords", "a_position",
		"a_in0", "a_in1",
};

static const char *ubonames[] = {
		"ubo0", "ubo1", "ubo2",
};

static EGLDisplay display;
static EGLSurface surface;

struct ubodata {
	float a[512][4];
	float b[512][4];
	float c[512][4];
} ubodata;

static void compile_shader(GLint program, int fd, GLenum type)
{
	GLuint shader;
	static char text[64 * 1024];
	const GLchar *const t = text;
	int ret = read(fd, text, sizeof(text));
	if (ret < 0) {
		ERROR_MSG("error reading shader: %d", ret);
		return;
	}
	text[ret] = '\0';

	GCHK(shader = glCreateShader(type));
	GCHK(glShaderSource(shader, 1, &t, NULL));
	GCHK(glCompileShader(shader));
	GCHK(glGetShaderiv(shader, GL_COMPILE_STATUS, &ret));
	if (!ret) {
		char *log;

		ERROR_MSG("%d shader compilation failed!:", type);
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetShaderInfoLog(shader, ret, NULL, log);
			printf("%s", log);
		}
		exit(-1);
	}

	GCHK(glAttachShader(program, shader));
}

static void *getpix(unsigned npix)
{
	uint32_t *pix = malloc(npix * 4);
	for (unsigned i = 0; i < npix; i++)
		pix[i] = i;
	return pix;
}

static void setup_textures(GLint program)
{
	int handle, tex, unit = 0;

	handle = glGetUniformLocation(program, "uTexture2D");
	if (handle >= 0) {
		DEBUG_MSG("setup uTexture2D");

		glGenTextures(1, &tex);

		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, getpix(32 * 32));

		glUniform1i(handle, unit);

		unit++;
	}

	handle = glGetUniformLocation(program, "uTexture3D");
	if (handle >= 0) {
		DEBUG_MSG("setup uTexture3D");

		glGenTextures(1, &tex);

		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_3D, tex);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 32, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, getpix(32 * 32 * 32));

		glUniform1i(handle, unit);

		unit++;
	}

	// TODO other texture types..
}

int test_compiler(int n)
{
	static char vert_shader[64 * 1024], frag_shader[64 * 1024];
	static GLfloat v[ARRAY_SIZE(attrnames)][NVERT * 4];
	static int nattr = 0;
	GLuint program;
	int vert_fd, frag_fd, gs_fd, tcs_fd, tes_fd;
	int i, ret;

	vert_fd = openfile("shaders/%04d.vs", n);
	tcs_fd = openfile("shaders/%04d.tcs", n);
	tes_fd = openfile("shaders/%04d.tes", n);
	gs_fd = openfile("shaders/%04d.gs", n);
	frag_fd = openfile("shaders/%04d.fs", n);
	if ((vert_fd < 0) || (frag_fd < 0))
		return -1;

	ret = read(vert_fd, vert_shader, sizeof(vert_shader));
	if (ret < 0)
		return ret;
	vert_shader[ret] = '\0';

	ret = read(frag_fd, frag_shader, sizeof(frag_shader));
	if (ret < 0)
		return ret;
	frag_shader[ret] = '\0';

	RD_START("compiler", "%d", n);

	program = get_program(vert_shader, frag_shader);

	if (tcs_fd >= 0)
		compile_shader(program, tcs_fd, 0x8E88/*GL_TESS_CONTROL_SHADER*/);
	if (tes_fd >= 0)
		compile_shader(program, tes_fd, 0x8E87/*GL_TESS_EVALUATION_SHADER*/);
	if (gs_fd >= 0)
		compile_shader(program, gs_fd, 0x8DD9/*GL_GEOMETRY_SHADER*/);

	for (i = 0; i < ARRAY_SIZE(attrnames); i++) {
		glBindAttribLocation(program, i, attrnames[i]);
		if (glGetError() == GL_NO_ERROR) {
			printf("use attribute: %s\n", attrnames[i]);
			nattr++;
		}
		/* clear any errors, just in case: */
		while (glGetError() != GL_NO_ERROR) {}
	}

	link_program(program);

	GCHK(glFlush());

	for (i = 0; i < nattr; i++) {
		GCHK(glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 0, v[i]));
		GCHK(glEnableVertexAttribArray(i));
	}

	for (i = 0; i < ARRAY_SIZE(ubonames); i++) {
		GLuint idx = glGetUniformBlockIndex(program, ubonames[i]);
		GLuint ubo;

		if (idx == GL_INVALID_INDEX)
			continue;

		GCHK(glGenBuffers(1, &ubo));
		GCHK(glBindBuffer(GL_UNIFORM_BUFFER, ubo));
		GCHK(glBufferData(GL_UNIFORM_BUFFER, sizeof(ubodata), &ubodata, GL_DYNAMIC_DRAW));
		GCHK(glBindBuffer(GL_UNIFORM_BUFFER, 0));

		GCHK(glBindBufferBase(GL_UNIFORM_BUFFER, i, ubo));
		GCHK(glUniformBlockBinding(program, idx, i));
	}

	setup_textures(program);

	/* clear any errors, just in case: */
	while (glGetError() != GL_NO_ERROR) {}

	if (tes_fd >= 0)
		GCHK(glDrawArrays(0x000E/*GL_PATCHES*/, 0, NVERT));
	else
		GCHK(glDrawArrays(GL_TRIANGLES, 0, NVERT));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	RD_END();

	return 0;
}

int main(int argc, char *argv[])
{
	GLint width, height;
	EGLint pbuffer_attribute_list[] = {
		EGL_WIDTH, 256,
		EGL_HEIGHT, 256,
		EGL_LARGEST_PBUFFER, EGL_TRUE,
		EGL_NONE
	};
	const EGLint config_attribute_list[] = {
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_DEPTH_SIZE, 8,
		EGL_NONE
	};
	const EGLint context_attribute_list[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};
	EGLConfig config;
	EGLint num_config;
	EGLContext context;
	TEST_START();

	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));
	ECHK(surface = eglCreatePbufferSurface(display, config, pbuffer_attribute_list));

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("PBuffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));
	GCHK(glFlush());

	if (__test == -1) {
		int i;
		for (i = 0; ; i++) {
			int ret = 0;
			if (test_compiler(i)) {
				break;
			}
		}
	} else {
		if (test_compiler(__test)) {
			exit(42);
		}
	}

	ECHK(eglDestroySurface(display, surface));
	ECHK(eglTerminate(display));

	return 0;
}

