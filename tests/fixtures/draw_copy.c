/* Self-written draw/copy/present fixture. Reuses the scene's authored shaders and geometry.
 * Frame 1 is the original scene. After present, CopyResource replaces the sampled texture
 * with yellow and the live backbuffer with cyan. Frame 2 draws without a color clear.
 * LESS_EQUAL allows the same geometry to draw again without another depth clear. */
#define DRAW_COPY_FIXTURE 1
#include "scene.c"
