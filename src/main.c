#include "core/pty.h"
#include <GLFW/glfw3.h>

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

int main() 
{
  // spawn_pty();
  // 1. Initialize GLFW
  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    return -1;
  }

  // 2. Create a windowed mode window and OpenGL context
  GLFWwindow* window = glfwCreateWindow(800, 600, "GLFW Rectangle", NULL, NULL);
  if (!window) {
    fprintf(stderr, "Failed to create GLFW window\n");
    glfwTerminate();
    return -1;
  }

  glfwMakeContextCurrent(window);

  // 3. Set key callback
  glfwSetKeyCallback(window, key_callback);

  // 4. Main loop
  while (!glfwWindowShouldClose(window))
  {
    // Set the viewport size
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    // Clear the screen
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f); // background color
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw a plain rectangle
    glBegin(GL_QUADS);
      glColor3f(1.0f, 0.0f, 0.0f); // red rectangle
      glVertex2f(-0.5f, -0.5f);
      glVertex2f( 0.5f, -0.5f);
      glVertex2f( 0.5f,  0.5f);
      glVertex2f(-0.5f,  0.5f);
    glEnd();

    // Swap buffers and poll events
    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
