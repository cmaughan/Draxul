#include <draxul/activity_coin_motion.h>

#include <algorithm>
#include <cmath>

namespace draxul
{
namespace
{
constexpr float kTau = 6.28318530718f;
constexpr float kSpeedEaseRate = 3.0f;
constexpr float kSettleEaseRate = 5.0f;
constexpr float kRestEpsilon = 0.001f;
} // namespace

float activity_coin_target_speed(float load)
{
    if (!(load > 0.0f))
        return 0.0f;
    const float clamped = std::min(load, 1.0f);
    const float rotations_per_second = kActivityCoinMinRotationsPerSecond
        + (kActivityCoinMaxRotationsPerSecond - kActivityCoinMinRotationsPerSecond) * clamped;
    return rotations_per_second * kTau;
}

void advance_activity_coin(ActivityCoinMotion& motion, float elapsed_seconds, float load)
{
    if (!(elapsed_seconds > 0.0f))
        return;
    const float speed_blend = 1.0f - std::exp(-elapsed_seconds * kSpeedEaseRate);
    motion.angular_speed
        += (activity_coin_target_speed(load) - motion.angular_speed) * speed_blend;
    motion.angle = std::fmod(motion.angle + motion.angular_speed * elapsed_seconds, kTau);

    if (!(load > 0.0f))
    {
        const float face_offset = std::remainder(motion.angle, kTau);
        const float settle_blend = 1.0f - std::exp(-elapsed_seconds * kSettleEaseRate);
        motion.angle -= face_offset * settle_blend;
        if (std::abs(face_offset) < kRestEpsilon && std::abs(motion.angular_speed) < kRestEpsilon)
        {
            motion.angle = 0.0f;
            motion.angular_speed = 0.0f;
        }
    }
    if (motion.angle < 0.0f)
        motion.angle += kTau;
}

bool activity_coin_in_motion(const ActivityCoinMotion& motion)
{
    return motion.angle != 0.0f || motion.angular_speed != 0.0f;
}

} // namespace draxul
