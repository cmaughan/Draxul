#pragma once

#include "camera.h"

#include <memory>

namespace draxul::satview
{

// Ported from EasyRender src/utils/camera_manipulator.h at 0871b7dd6eb24d7b611bd982a724289de437ed0e.
class Manipulator
{
private:
    glm::vec2 startPos;
    glm::vec2 currentPos;
    std::shared_ptr<Camera> spCamera;
    bool mouseDown;

public:
    Manipulator(std::shared_ptr<Camera> pCam)
        : spCamera(pCam)
        , mouseDown(false)
    {
    }

    void MouseDown(const glm::vec2& pos)
    {
        startPos = pos;
        mouseDown = true;
    }

    void MouseUp(const glm::vec2& pos)
    {
        currentPos = pos;
        mouseDown = false;
    }

    bool MouseMove(const glm::vec2& pos, bool dolly)
    {
        currentPos = pos;
        if (mouseDown)
        {
            if (dolly)
            {
                spCamera->Dolly((startPos.y - currentPos.y) / 4.0f);
            }
            else
            {
                const glm::vec2 delta = currentPos - startPos;
                spCamera->Orbit(delta / 2.0f);
            }
            startPos = pos;
            return true;
        }
        return false;
    }

    void Dolly(float distance)
    {
        spCamera->Dolly(distance);
    }

    void Cancel()
    {
        mouseDown = false;
    }
};

} // namespace draxul::satview
