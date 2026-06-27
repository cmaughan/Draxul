#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vector_relational.hpp>
#include <limits>

namespace draxul::satview
{

struct Ray
{
    glm::vec3 position;
    glm::vec3 direction;
};

// A turntable orbit camera.
//
// The orbit state is two scalars: azimuth (yaw about world +Y) and elevation
// (pitch above/below the equator). The position and the orthonormal frame are
// reconstructed from those scalars whenever they change, with the up vector
// always derived from world up. This keeps the horizon level (no roll) and,
// together with the elevation clamp, avoids the pole singularity / image
// inversion that a free-quaternion camera suffers from when you orbit over a
// pole. (Replaces an earlier EasyRender-derived shortest-arc quaternion camera
// that baked a yaw-dependent roll into the initial frame and had no pole clamp.)
class Camera
{
private:
    glm::vec3 position = glm::vec3(0.0f); // Position of the camera in world space
    glm::vec3 focalPoint = glm::vec3(0.0f); // Look at point

    float filmWidth = 1.0f; // Width/height of the film
    float filmHeight = 1.0f;

    glm::vec3 viewDirection = glm::vec3(0.0f, 0.0f, -1.0f); // The direction the camera is looking in
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f); // The vector to the right
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f); // The vector up

    float fieldOfView = 60.0f; // Field of view
    float halfAngle = 30.0f; // Half angle of the view frustum
    float aspectRatio = 1.0f; // Ratio of x to y of the viewport

    float azimuth = 0.0f; // Yaw about world +Y (radians)
    float elevation = 0.0f; // Pitch above/below the equator (radians)

    glm::vec2 orbitDelta = glm::vec2(0.0f);
    glm::vec3 positionDelta = glm::vec3(0.0f);

    int64_t lastTime = 0;
    float minDistance = 0.0001f;
    float maxDistance = std::numeric_limits<float>::max();

    // Keep the camera off the exact pole, where yaw degenerates into roll and
    // the look-at frame becomes singular.
    static float MaxElevation()
    {
        return glm::radians(89.0f);
    }

public:
    Camera()
    {
    }

    virtual ~Camera()
    {
    }

    const glm::vec3& GetPosition() const
    {
        return position;
    }

    const glm::vec3& GetFocalPoint() const
    {
        return focalPoint;
    }

    const glm::vec3& GetViewDirection() const
    {
        return viewDirection;
    }

    const glm::vec3& GetRight() const
    {
        return right;
    }

    const glm::vec3& GetUp() const
    {
        return up;
    }

    float GetFieldOfView() const
    {
        return fieldOfView;
    }

    float GetAspectRatio() const
    {
        return aspectRatio;
    }

    float GetDistance() const
    {
        return glm::length(focalPoint - position);
    }

    float GetMaxDistance() const
    {
        return maxDistance;
    }

    void SetPositionAndFocalPoint(const glm::vec3& pos, const glm::vec3& point)
    {
        // From
        position = pos;

        // Focal
        focalPoint = point;

        // Derive azimuth/elevation from the incoming direction (focal -> camera).
        // The azimuth sign matches RebuildFrame's -sin(azimuth) convention so a
        // round-trip reproduces the requested position exactly.
        const glm::vec3 dir = glm::normalize(position - focalPoint);
        elevation = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
        elevation = std::clamp(elevation, -MaxElevation(), MaxElevation());
        azimuth = std::atan2(-dir.x, dir.z);

        RebuildFrame();
        ClampDistance();
    }

    void SetFilmSize(float width, float height)
    {
        filmWidth = width;
        filmHeight = height;
        aspectRatio = width / height;
    }

    void SetDistanceLimits(float min_distance, float max_distance)
    {
        minDistance = std::max(0.0001f, min_distance);
        maxDistance = std::max(minDistance, max_distance);
        ClampDistance();
    }

    void ClearMotion()
    {
        orbitDelta = glm::vec2(0.0f);
        positionDelta = glm::vec3(0.0f);
    }

    bool PreRender()
    {
        // The half-width of the viewport, in world space
        halfAngle = float(tan(glm::radians(fieldOfView) / 2.0));

        auto time = GetTime();

        int64_t delta;
        if (lastTime == 0)
        {
            lastTime = time;
            delta = 0;
        }
        else
        {
            delta = time - lastTime;
            lastTime = time;
        }

        bool changed = false;
        if (orbitDelta != glm::vec2(0.0f))
        {
            UpdateOrbit(delta);
            changed = true;
        }

        if (positionDelta != glm::vec3(0.0f))
        {
            UpdatePosition(delta);
            changed = true;
        }
        return changed;
    }

    int64_t GetTime()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    }

    // Given a screen coordinate, return a ray leaving the camera and entering the world at that 'pixel'
    Ray GetWorldRay(const glm::vec2& imageSample)
    {
        // Could move some of this maths out of here for speed, but this isn't time critical
        auto dir = viewDirection;
        float x = ((imageSample.x * 2.0f) / filmWidth) - 1.0f;
        float y = ((imageSample.y * 2.0f) / filmHeight) - 1.0f;

        // Take the view direction and adjust it to point at the given sample, based on the
        // the frustum
        dir += (right * (halfAngle * aspectRatio * x));
        dir -= (up * (halfAngle * y));
        float ft = (glm::length(focalPoint - position) - 1.0f) / glm::length(dir);
        glm::vec3 focasPoint = position + dir * ft;

        dir = glm::normalize(focasPoint - position);

        return Ray{ position, dir };
    }

    void Dolly(float distance)
    {
        positionDelta += viewDirection * distance;
    }

    // Orbit around the focal point, keeping y 'Up'
    void Orbit(const glm::vec2& angle)
    {
        orbitDelta += angle;
    }

    float SmoothStep(float val)
    {
        return val * val * (3.0f - 2.0f * val);
    }

    void UpdatePosition(int64_t timeDelta)
    {
        const float settlingTimeMs = 50;
        float frac = std::min(timeDelta / settlingTimeMs, 1.0f);
        frac = SmoothStep(frac);
        glm::vec3 distance = frac * positionDelta;
        positionDelta *= (1.0f - frac);

        position += distance;
        ClampDistance();
    }

    void UpdateOrbit(int64_t timeDelta)
    {
        const float settlingTimeMs = 50;
        float frac = std::min(timeDelta / settlingTimeMs, 1.0f);
        frac = SmoothStep(frac);

        // Get a proportion of the remaining turn angle, based on the time delta
        glm::vec2 angle = frac * orbitDelta;

        // Reduce the orbit delta remaining for next time
        orbitDelta *= (1.0f - frac);
        if (glm::all(glm::lessThan(glm::abs(orbitDelta), glm::vec2(.1f))))
        {
            orbitDelta = glm::vec2(0.0f);
        }

        // Apply the turn as a yaw about world up and a pitch in elevation, clamped
        // off the poles. Reconstructing the frame from these scalars keeps the
        // horizon level and prevents the over-the-pole inversion.
        azimuth += glm::radians(angle.x);
        elevation += glm::radians(angle.y);
        elevation = std::clamp(elevation, -MaxElevation(), MaxElevation());

        RebuildFrame();
    }

private:
    void RebuildFrame()
    {
        const float cp = std::cos(elevation);
        // Direction from the focal point to the camera. The -sin(azimuth) sign
        // makes a positive yaw orbit the camera the same way the previous
        // quaternion camera did (Orbit(90,0) from +Z lands on -X).
        const glm::vec3 dir(
            -std::sin(azimuth) * cp,
            std::sin(elevation),
            std::cos(azimuth) * cp);

        const float distance = glm::length(focalPoint - position);
        position = focalPoint + dir * distance;
        viewDirection = glm::normalize(focalPoint - position);
        UpdateRightUp();
    }

    void UpdateRightUp()
    {
        // Derive the frame from world up so the horizon never rolls. The
        // elevation clamp guarantees viewDirection is never parallel to world up,
        // so this cross product never degenerates.
        right = glm::normalize(glm::cross(viewDirection, glm::vec3(0.0f, 1.0f, 0.0f)));
        up = glm::normalize(glm::cross(right, viewDirection));
    }

    void ClampDistance()
    {
        const float signed_distance = glm::dot(focalPoint - position, viewDirection);
        const float clamped = std::clamp(signed_distance, minDistance, maxDistance);
        position = focalPoint - viewDirection * clamped;
    }
};

} // namespace draxul::satview
