#include "test-util-3d.h"

#include "esUtil.h"
#include "esTransform.c"

static EGLint const config_attribute_list[] = {
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_DEPTH_SIZE, 8,
	EGL_NONE
};

static const EGLint context_attribute_list[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
};

static EGLDisplay display;
static EGLConfig config;
static EGLint num_config;
static EGLContext context;
static GLuint program;

const char *vertex_shader_src = ""
      "attribute vec2 pos;"
      "attribute vec2 uv;"
      "varying vec2 v_uv;"
      "varying vec2 v_uv1;"
      "varying vec2 v_uv2;"
      "varying vec2 v_uv3;"
      ""
      "void main() {"
      "  gl_Position = vec4 (pos, 0.0, 1.0);"
      "  v_uv = uv; \n"
      "  v_uv1 = uv.yx; \n"
      "  v_uv2 = uv; \n"
      "  v_uv3 = uv.yx; \n"
      "}";

const char *fragment_shader_src =
      "precision mediump float; \n"
      ""
      "uniform sampler2D u_tex; \n"
      "uniform sampler2D u_tex1; \n"
      "uniform sampler2D u_tex2; \n"
      "uniform sampler2D u_tex3; \n"
      "varying vec2 v_uv; \n"
      "varying vec2 v_uv1; \n"
      "varying vec2 v_uv2; \n"
      "varying vec2 v_uv3; \n"
      ""
      "void main() {"
      "  vec4 col = texture2D(u_tex, v_uv); \n"
      "  vec4 col1 = texture2D(u_tex1, v_uv1); \n"
      "  vec4 col2 = texture2D(u_tex2, v_uv2); \n"
      "  vec4 col3 = texture2D(u_tex3, v_uv3); \n"
      //"  gl_FragColor = vec4(col.xyz + col1.xyz +col2.xyz + col3.xyz, 1.0); \n"
      "  gl_FragColor = vec4(col.xyzw + col1.xyzw + col2.xyzw + col3.xyzw); \n"
      "}";


#define DEFAULT_WIDTH  1280
#define DEFAULT_HEIGHT  800

void test_sample_4(void)
{
	GLint width, height;
	EGLSurface surface;
	GLuint tex;
	GLuint tex_loc;

	display = get_display();
	RD_START("sample_4", "");

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));
	//surface = make_window(display, config, 400, 240);
	surface = make_window(display, config, DEFAULT_WIDTH, DEFAULT_HEIGHT);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	/* Create a program and attach our vertex and fragment shaders. */
	program = get_program(vertex_shader_src, fragment_shader_src);

	GCHK(glGenTextures (1, &tex));

	GCHK(glActiveTexture (GL_TEXTURE0));
	GCHK(glBindTexture (GL_TEXTURE_2D, tex));
	GCHK(glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GCHK(glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	
	/* Upload a 2x2 matrix of 4 different pixels to the texture. */
	const uint8_t tex_data[4][4] = {
		{255,   0,   0, 0}, {  0, 255, 0, 0},
		{  0,   0, 255, 0}, {255, 255, 0, 0},
	};
	GCHK(glTexImage2D (GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 2, 2,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 tex_data));

	GCHK(glBindAttribLocation (program, 0, "pos"));
	GCHK(glBindAttribLocation (program, 1, "uv"));

	link_program(program);

	glActiveTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, tex);

	GCHK(tex_loc = glGetUniformLocation (program, "u_tex"));
	GCHK(glUniform1i (tex_loc, 0));


	/* Start of Draw */
	GCHK(glViewport(0, 0, width, height));
	GCHK(glClearColor(1.0, 1.0, 1.0, 1.0));
	GCHK(glClear(GL_COLOR_BUFFER_BIT));

   /* Submit vertices to draw a quad. */
      static const float vertices[4][2] = {
         {-0.5,  0.5},
         { 0.5,  0.5},
         {-0.5, -0.5},
         { 0.5, -0.5},
      };

      static const float uv[4][2] = {
         { 0.0, 0.0},
         { 1.0, 0.0},
         { 0.0, 1.0},
         { 1.0, 1.0},
      };

	GCHK(glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, vertices));
	GCHK(glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 0, uv));

	GCHK(glEnableVertexAttribArray (0));
	GCHK(glEnableVertexAttribArray (1));

	//GCHK(glEnable(GL_CULL_FACE));
	GCHK(glEnable(GL_BLEND));

	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());
	/* End of Draw */

	sleep(1);

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_sample_4());
	TEST_END();
	return 0;
}
