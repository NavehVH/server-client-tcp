#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>

struct Vector2D {
    float x = 0.0f;
    float y = 0.0f;
};

struct LineSegment {
    Vector2D p1;
    Vector2D p2;
};

using Foothold = LineSegment;

struct Color3 {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
};

// Returns the Y coordinate on a foothold at a given X.
inline float footholdYAt(const Foothold& fh, float x) {
    float dx = fh.p2.x - fh.p1.x;
    if (std::abs(dx) < 0.001f) return fh.p1.y;
    return fh.p1.y + ((fh.p2.y - fh.p1.y) / dx) * (x - fh.p1.x);
}

inline bool inFootholdXRange(const Foothold& fh, float x) {
    float lo = std::min(fh.p1.x, fh.p2.x);
    float hi = std::max(fh.p1.x, fh.p2.x);
    return x >= lo && x <= hi;
}
