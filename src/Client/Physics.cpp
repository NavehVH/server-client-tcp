#include "Client/Physics.hpp"
#include <algorithm>
#include <cmath>

void Physics::update(float dt, const std::vector<Foothold>& footholds) {
    // Horizontal input.
    vx = 0.0f;
    if (moveLeft)  vx = -MOVE_SPEED;
    if (moveRight) vx =  MOVE_SPEED;

    // Gravity.
    if (!onGround) vy += GRAVITY * dt;

    // Jump (only when grounded).
    if (jumpPressed && onGround) {
        vy       = JUMP_VY;
        onGround = false;
    }

    // Integrate.
    float newX = x + vx * dt;
    float newY = y + vy * dt;

    // World boundary (horizontal).
    newX = std::clamp(newX, 0.0f, WORLD_W - ENTITY_SIZE);

    // Foothold collision — bottom edge of the square.
    float oldBottom = y + ENTITY_SIZE;
    float newBottom = newY + ENTITY_SIZE;
    onGround = false;

    for (const auto& fh : footholds) {
        float cx = newX + ENTITY_SIZE * 0.5f;  // centre X for lookup
        if (!inFootholdXRange(fh, cx)) continue;
        float fhY = footholdYAt(fh, cx);

        if (oldBottom <= fhY + 1.0f && newBottom >= fhY) {
            newY     = fhY - ENTITY_SIZE;
            vy       = 0.0f;
            onGround = true;
            break;
        }
    }

    x = newX;
    y = newY;
}

void Physics::snapTo(float nx, float ny) {
    x = nx;
    y = ny;
    vx = vy = 0.0f;
    onGround = false;
}
