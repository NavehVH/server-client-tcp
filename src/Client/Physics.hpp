#pragma once
#include "Core/Types.hpp"
#include <vector>

// Fixed-timestep local physics for the player-controlled square.
// Runs in the render thread; the server re-validates and may issue corrections.
class Physics {
public:
    static constexpr float ENTITY_SIZE = 32.0f;
    static constexpr float GRAVITY     = 980.0f;
    static constexpr float MOVE_SPEED  = 220.0f;
    static constexpr float JUMP_VY     = -520.0f;
    static constexpr float WORLD_W     = 800.0f;
    static constexpr float WORLD_H     = 600.0f;

    // Input flags — set by the render loop each frame before calling update().
    bool moveLeft  = false;
    bool moveRight = false;
    bool jumpPressed = false;   // true only on the frame the key was first pressed

    // State
    float x  = 400.0f;
    float y  = 50.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    bool  onGround = false;

    // Integrate physics one fixed step.
    void update(float dt, const std::vector<Foothold>& footholds);

    // Teleport (from server correction).
    void snapTo(float nx, float ny);
};
