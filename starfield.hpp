// "Flying through space" starfield: single-pixel stars streaming through the world along +Z
// (towards the camera when the board is level), so turning the board turns the view of them.
#pragma once

#include <cstdint>

struct View;

class Starfield {
public:
    // speed in units per second
    explicit Starfield(float speed = 8.0f);

    void update(float dt);
    void draw(uint16_t *fb, int width, int height, const View &view) const; // does not clear

private:
    static constexpr int COUNT = 400;
    static constexpr float EXTENT = 20.0f; // stars fill a cube of ±EXTENT around the origin
    static constexpr float FADE = 30.0f;   // depth at which stars fade to black

    struct Star {
        float p[3];
    };

    float random(float lo, float hi);

    Star stars_[COUNT];
    float speed_;
    uint32_t rng_ = 0x2545F491;
};
