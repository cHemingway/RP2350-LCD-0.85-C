// Software 3D rendering into an RGB565 framebuffer: a shared camera/view, lines and a wireframe cube
#pragma once

#include <cstdint>

// The board's view of a world that stays fixed as the board turns. The camera sits
// CAMERA_DISTANCE units from the world origin, looking at it, and orbits it as the board rotates.
struct View {
    // Perspective: screen offset = FOCAL * x / depth. Chosen so the cube's worst-case corner
    // (radius sqrt(3) at 4 units) stays on a 128px screen.
    static constexpr float FOCAL = 70.0f;
    static constexpr float CAMERA_DISTANCE = 4.0f;
    static constexpr float NEAR = 0.1f;

    // Board orientation in radians; rotation applied yaw, then pitch, then roll
    static View from_orientation(float roll, float pitch, float yaw);

    // World point → screen position and depth. Returns false if it's behind the camera.
    bool project(const float p[3], int width, int height, float &x, float &y, float &depth) const;

    float r[3][3]; // board orientation R = Rz(yaw) * Ry(pitch) * Rx(roll); world → camera uses R^T
};

// Bresenham line, clipped per pixel to the framebuffer
void draw_line(uint16_t *fb, int width, int height, int x0, int y0, int x1, int y1, uint16_t color);

// Draws a 2-unit cube at the world origin. Does not clear the framebuffer.
void draw_wireframe_cube(uint16_t *fb, int width, int height, const View &view, uint16_t color = 0xFFFF);
