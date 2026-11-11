/* Compatibility shims shared by the game and the pattern editor.
 *
 * raylib 6.2 aborts start-up when GLFW reports a pending error right after the
 * window is created: it calls glfwGetError(NULL) and treats a
 * GLFW_PLATFORM_ERROR as "failed to initialize graphics device", then exits.
 * GLFW brings its joystick subsystem up lazily, and on a machine where
 * DirectInput8Create() fails (DirectInput broken/unavailable) that leaves
 * exactly such an error queued - so the program dies before any window
 * appears, even though the window and GL context were created fine.
 *
 * Neither the game nor the editor uses gamepads, so the joystick subsystem is
 * skipped altogether. The linker redirects GLFW's joystick entry points here
 * with -Wl,--wrap=... (see WIN_LDFLAGS in the Makefile). */

#if defined(_WIN32)
void *__wrap_glfwSetJoystickCallback(void *callback) { (void)callback; return 0; }
int   __wrap_glfwJoystickPresent(int jid) { (void)jid; return 0; }
#endif
