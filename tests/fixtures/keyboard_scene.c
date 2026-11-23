/* Self-written file-backed keyboard scene. Arrows move green quad; Escape quits.
 * Default four frames for CI. Define FRAMES=3600 for a bounded interactive run. */
#define ASSET_SCENE_FIXTURE 1
#define KEYBOARD_SCENE_FIXTURE 1
#ifndef FRAMES
#define FRAMES 4
#endif
#include "scene.c"
