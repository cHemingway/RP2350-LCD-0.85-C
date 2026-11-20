#include "cube.hpp"

#include <cmath>
#include <cstdlib>

View View::from_orientation(float roll, float pitch, float yaw)
{
    const float cr = cosf(roll), sr = sinf(roll);
    const float cp = cosf(pitch), sp = sinf(pitch);
    const float cy = cosf(yaw), sy = sinf(yaw);
    return View{{
        {cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr},
        {sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr},
        {-sp,     cp * sr,                cp * cr},
    }};
}

bool View::project(const float p[3], int width, int height, float &x, float &y, float &depth) const
{
    // Rotate by R^T (the inverse of the board's rotation) so the world appears fixed
    float v[3];
    for (int i = 0; i < 3; i++)
        v[i] = r[0][i] * p[0] + r[1][i] * p[1] + r[2][i] * p[2];
    depth = CAMERA_DISTANCE - v[2]; // camera on +Z looking towards -Z
    if (depth < NEAR)
        return false;
    x = width / 2 + v[0] * FOCAL / depth;
    y = height / 2 - v[1] * FOCAL / depth; // screen Y points down
    return true;
}

void draw_line(uint16_t *fb, int width, int height, int x0, int y0, int x1, int y1, uint16_t color)
{
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        if (unsigned(x0) < unsigned(width) && unsigned(y0) < unsigned(height))
            fb[y0 * width + x0] = color;
        if (x0 == x1 && y0 == y1)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_wireframe_cube(uint16_t *fb, int width, int height, const View &view, uint16_t color)
{
    // Vertex i has coordinates (±1, ±1, ±1) from bits 0, 1, 2 of i
    struct Edge { uint8_t a, b; };
    static constexpr Edge EDGES[] = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7}, // along X
        {0, 2}, {1, 3}, {4, 6}, {5, 7}, // along Y
        {0, 4}, {1, 5}, {2, 6}, {3, 7}, // along Z
    };

    int sx[8], sy[8];
    for (int i = 0; i < 8; i++) {
        const float v[3] = {i & 1 ? 1.0f : -1.0f, i & 2 ? 1.0f : -1.0f, i & 4 ? 1.0f : -1.0f};
        float x, y, depth;
        view.project(v, width, height, x, y, depth); // always in front: the cube is within the orbit
        sx[i] = int(lroundf(x));
        sy[i] = int(lroundf(y));
    }

    for (const Edge &e : EDGES)
        draw_line(fb, width, height, sx[e.a], sy[e.a], sx[e.b], sy[e.b], color);
}
