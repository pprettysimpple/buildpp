#include <raylib.h>

// run build on this example and fix it one line at a time to see how recompilation using this tool works

int main(int argc, char** argv) {
    InitWindow(800, 450, "Build++ Example 03 - External Dependency");
    BeginDrawing();
    ClearBackground(RAYWHITE);
    DrawText("Hello, Build++!", 190, 200, 20, LIGHTGRAY);
    EndDrawing();
    CloseWindow();
}
