#pragma once

#include "Maths.h"

#include <math.h>

// Small math helpers used by the car controller and camera.
//
// The engine has equivalents on `class Maths`, but that class carries no
// POLYPHASE_API and none of its out-of-line members appear in Polyphase.lib,
// so an addon DLL cannot link them. Everything here is header-inline and leans
// on glm, which is header-only and therefore always available.

namespace CarAddon
{
    inline float Clamp01(float v)
    {
        return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
    }

    inline float Sign(float v)
    {
        return (v > 0.0f) ? 1.0f : ((v < 0.0f) ? -1.0f : 0.0f);
    }

    inline glm::vec3 SafeNormalize(glm::vec3 v)
    {
        const float len = glm::length(v);
        return (len > 0.0001f) ? (v / len) : glm::vec3(0.0f);
    }

    // Frame-rate independent exponential smoothing. `rate` is roughly "how many
    // e-foldings per second", so higher = snappier. Unlike a raw lerp(a, b, k*dt)
    // this behaves identically at 30fps and 240fps.
    inline float Damp(float current, float target, float rate, float deltaTime)
    {
        if (rate <= 0.0f)
        {
            return target;
        }
        const float t = 1.0f - expf(-rate * deltaTime);
        return current + (target - current) * t;
    }

    inline glm::vec3 Damp(glm::vec3 current, glm::vec3 target, float rate, float deltaTime)
    {
        if (rate <= 0.0f)
        {
            return target;
        }
        const float t = 1.0f - expf(-rate * deltaTime);
        return current + (target - current) * t;
    }

    // Constant-rate approach, in units per second. Used where a fixed slew rate
    // reads better than exponential easing -- steering, in particular, wants a
    // predictable lock-to-lock time rather than an asymptote.
    inline float Approach(float current, float target, float speed, float deltaTime)
    {
        const float maxStep = speed * deltaTime;
        const float delta = target - current;
        if (delta > maxStep)  { return current + maxStep; }
        if (delta < -maxStep) { return current - maxStep; }
        return target;
    }

    // Rotate `from` toward `to` about `axis` by at most `maxRadians`.
    //
    // This is the heart of the arcade handling model: the car's velocity
    // direction chases its heading at a rate equal to grip. A high rate means
    // the car goes exactly where it points; a low rate means the tail slides
    // out. Both vectors are expected to be normalized and roughly planar.
    inline glm::vec3 RotateTowards(glm::vec3 from, glm::vec3 to, glm::vec3 axis, float maxRadians)
    {
        if (maxRadians <= 0.0f)
        {
            return from;
        }

        const float dot = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
        const float angle = acosf(dot);

        if (angle <= maxRadians || angle < 0.00001f)
        {
            return to;
        }

        // Sign of the rotation: which side of `from` does `to` sit on.
        const float dir = Sign(glm::dot(glm::cross(from, to), axis));

        // A zero cross product means the vectors are exactly opposed (180 deg).
        // There's no defined shortest arc, so pick one arbitrarily rather than
        // returning a NaN-producing zero rotation.
        const float rot = (dir != 0.0f) ? (maxRadians * dir) : maxRadians;

        return SafeNormalize(glm::angleAxis(rot, axis) * from);
    }

    // Signed angle from `from` to `to` about `axis`, in radians.
    inline float SignedAngle(glm::vec3 from, glm::vec3 to, glm::vec3 axis)
    {
        const float dot = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
        const float angle = acosf(dot);
        return angle * Sign(glm::dot(glm::cross(from, to), axis));
    }

    // Remap `v` from [inMin, inMax] to [outMin, outMax], clamped at both ends.
    inline float MapClamped(float v, float inMin, float inMax, float outMin, float outMax)
    {
        if (inMax - inMin == 0.0f)
        {
            return outMin;
        }
        const float t = Clamp01((v - inMin) / (inMax - inMin));
        return outMin + (outMax - outMin) * t;
    }

    // Symmetric deadzone with rescale, so the usable range still reaches +/-1.
    // The engine applies no deadzone of its own to raw gamepad axes.
    inline float ApplyDeadzone(float v, float deadzone)
    {
        const float a = fabsf(v);
        if (a < deadzone)
        {
            return 0.0f;
        }
        if (deadzone >= 1.0f)
        {
            return 0.0f;
        }
        return Sign(v) * ((a - deadzone) / (1.0f - deadzone));
    }
}
