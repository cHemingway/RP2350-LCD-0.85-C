#include "starfield.hpp"

#include "cube.hpp" // View

namespace {
uint16_t grey(uint8_t v) { return uint16_t((v >> 3) << 11 | (v >> 2) << 5 | (v >> 3)); }
} // namespace

Starfield::Starfield(float speed) : speed_(speed)
{
    for (Star &s : stars_)
        for (float &c : s.p)
            c = random(-EXTENT, EXTENT);
}

float Starfield::random(float lo, float hi)
{
    rng_ ^= rng_ << 13; // xorshift32
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return lo + (hi - lo) * float(rng_ >> 8) / float(1 << 24);
}

void Starfield::update(float dt)
{
    for (Star &s : stars_) {
        s.p[2] += speed_ * dt;
        if (s.p[2] > EXTENT) { // wrap round to the far side, in a new position
            s.p[0] = random(-EXTENT, EXTENT);
            s.p[1] = random(-EXTENT, EXTENT);
            s.p[2] -= 2 * EXTENT;
        }
    }
}

void Starfield::draw(uint16_t *fb, int width, int height, const View &view) const
{
    for (const Star &s : stars_) {
        float x, y, depth;
        if (!view.project(s.p, width, height, x, y, depth) || depth >= FADE)
            continue;
        const int ix = int(x), iy = int(y);
        if (unsigned(ix) < unsigned(width) && unsigned(iy) < unsigned(height))
            fb[iy * width + ix] = grey(uint8_t(255 * (1 - depth / FADE))); // brighter when closer
    }
}
