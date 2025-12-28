#pragma once
// GLIncludes.hpp - Manage OpenGL header inclusion order
// GLEW must be included before any Qt OpenGL headers

#ifndef VIBECHAD_GL_INCLUDES
#define VIBECHAD_GL_INCLUDES

// GLEW first, always
#include <GL/glew.h>

// Then OpenGL
#include <GL/gl.h>

#endif // VIBECHAD_GL_INCLUDES
