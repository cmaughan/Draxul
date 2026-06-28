#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
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

// A quaternion orbit camera. The quaternion is the sole orientation state;
// position and the camera frame are derived from it without a world-up rebuild.
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

    glm::quat orientation{ 1.0f, 0.0f, 0.0f, 0.0f }; // Camera local axes to world space

    glm::vec2 orbitDelta = glm::vec2(0.0f);
    glm::vec3 positionDelta = glm::vec3(0.0f);

    int64_t lastTime = 0;
    float minDistance = 0.0001f;
    float maxDistance = std::numeric_limits<float>::max();

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

    const glm::quat& GetOrientation() const
    {
        return orientation;
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

        const glm::vec3 view = glm::normalize(focalPoint - position);
        const glm::vec3 reference_up = std::abs(glm::dot(view, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.999f
            ? glm::vec3(0.0f, 0.0f, 1.0f)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 initial_right = glm::normalize(glm::cross(view, reference_up));
        const glm::vec3 initial_up = glm::normalize(glm::cross(initial_right, view));
        orientation = glm::normalize(glm::quat_cast(glm::mat3(initial_right, initial_up, -view)));

        RebuildFrame();
        ClampDistance();
    }

    void SetFocalPointAndDistance(const glm::vec3& point, float distance)
    {
        focalPoint = point;
        position = focalPoint - viewDirection * std::max(0.0001f, distance);
        ClampDistance();
    }

    void SetFocalPoint(const glm::vec3& point)
    {
        SetFocalPointAndDistance(point, GetDistance());
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

        const glm::vec3 yaw_axis = glm::normalize(orientation * glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat yaw = glm::angleAxis(glm::radians(-angle.x), yaw_axis);
        orientation = glm::normalize(yaw * orientation);

        const glm::vec3 pitch_axis = glm::normalize(orientation * glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::quat pitch = glm::angleAxis(glm::radians(-angle.y), pitch_axis);
        orientation = glm::normalize(pitch * orientation);

        RebuildFrame();
    }

private:
    void RebuildFrame()
    {
        const float distance = glm::length(focalPoint - position);
        right = glm::normalize(orientation * glm::vec3(1.0f, 0.0f, 0.0f));
        up = glm::normalize(orientation * glm::vec3(0.0f, 1.0f, 0.0f));
        viewDirection = glm::normalize(orientation * glm::vec3(0.0f, 0.0f, -1.0f));
        position = focalPoint - viewDirection * distance;
    }

    void ClampDistance()
    {
        const float signed_distance = glm::dot(focalPoint - position, viewDirection);
        const float clamped = std::clamp(signed_distance, minDistance, maxDistance);
        position = focalPoint - viewDirection * clamped;
    }
};

} // namespace draxul::satview
