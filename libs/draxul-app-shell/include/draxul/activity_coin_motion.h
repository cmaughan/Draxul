#pragma once

// Renderer-free spin model for the Agents activity coin, ported from TokenFu's
// update_coin(). Activity is a normalized load in [0, 1]: zero lets the coin
// decelerate and settle face-on; higher load spins it faster.

namespace draxul
{

constexpr float kActivityCoinMinRotationsPerSecond = 0.10f;
constexpr float kActivityCoinMaxRotationsPerSecond = 3.0f;

struct ActivityCoinMotion
{
    float angle = 0.0f; // radians in [0, tau); 0 is face-on
    float angular_speed = 0.0f; // radians per second
};

// Target angular speed for a load: 0 when idle, otherwise linear between the
// minimum and maximum rotation rates.
float activity_coin_target_speed(float load);

// Ease toward the load's target speed and advance the angle. With zero load
// the coin also eases back to face-on and snaps exactly to rest once close.
void advance_activity_coin(ActivityCoinMotion& motion, float elapsed_seconds, float load);

// True until the coin is exactly at rest face-on; callers keep scheduling
// frames while any visible coin is in motion.
bool activity_coin_in_motion(const ActivityCoinMotion& motion);

} // namespace draxul
