#pragma once
#include "Core/Types.hpp"
#include <cstdint>

static constexpr float ENTITY_SIZE = 32.0f;

struct Entity {
    uint32_t id  = 0;
    int      fd  = -1;   // owning socket descriptor
    float    x   = 0.0f;
    float    y   = 0.0f;
    float    vx  = 0.0f;
    float    vy  = 0.0f;
    Color3   color{};
    bool     onGround = false;
    bool     dirty    = true;   // true when state changed since last broadcast
};
