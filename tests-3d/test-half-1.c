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

#define DEFAULT_WIDTH  1280
#define DEFAULT_HEIGHT  800

const char *vertex_src = ""
		"attribute vec4 vertex;\n"
		""
		"void main() {\n"
    	"	gl_Position = vertex;\n"
		"}";

const char *fragment_src =
      "precision mediump float; \n"
      ""
		"void main() {\n"
		"gl_FragColor = vec4(0.1, 0.2, 0.3, 1.0);\n"
		"}";

void
test_half_1(void)
{
	GLint width, height;
	EGLSurface surface;
	GLuint tex;
	GLuint tex_loc;

	display = get_display();
	RD_START("half_1", "");

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));
	surface = make_window(display, config, DEFAULT_WIDTH, DEFAULT_HEIGHT);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	/* Create a program and attach our vertex and fragment shaders. */
	program = get_program(vertex_src, fragment_src);


   GCHK(glBindAttribLocation (program, 0, "vertex"));

	link_program(program);

	/* Start of Draw */
	GCHK(glViewport(0, 0, width, height));
	GCHK(glClearColor(1.0, 1.0, 1.0, 1.0));
	GCHK(glClear(GL_COLOR_BUFFER_BIT));

    static const float vertices[4][2] = {
         {-1.0,  -1.0},
         { 1.0,  -1.0},
         { 1.0,   1.0},
         {-1.0,   1.0},
      };

	GCHK(glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, vertices));
	GCHK(glEnableVertexAttribArray (0));

	GCHK(glEnable(GL_BLEND));
	GCHK(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());
	/* End of Draw */

	sleep(1);

	ECHK(eglDestroySurface(display, surface));
	ECHK(eglTerminate(display));

	RD_END();
}

int32_t
main (int32_t argc, char *argv[])
{
   TEST_START();
   TEST(test_half_1());
   TEST_END();

   return 0;
}
