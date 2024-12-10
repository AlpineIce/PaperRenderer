#include "Camera.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    Camera::Camera(RenderEngine& renderer, const CameraCreateInfo& creationInfo)
        :windowPtr(renderer.getSwapchain().getGLFWwindow()),
        clipNear(creationInfo.clipNear),
        clipFar(creationInfo.clipFar),
        fov(creationInfo.fov),
        translation(creationInfo.initTranslation),
        renderer(renderer)
    {
        BufferInfo uboInfo = {};
        uboInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        uboInfo.size = sizeof(glm::mat4) * 2;
        uboInfo.usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR;
        ubo = std::make_unique<Buffer>(renderer, uboInfo);

        updateCameraProjection();
        updateCameraView(translation);
    }

    Camera::~Camera()
    {

    }

    void Camera::setClipSpace(float near, float far)
    {
        clipNear = near;
        clipFar = far;
        updateCameraProjection();
    }

    void Camera::updateCameraProjection()
    {
        glfwGetFramebufferSize(windowPtr, &width, &height);
        projection = glm::perspective(glm::radians(fov), (float)width / (float)height, clipNear, clipFar);

        BufferWrite write = {};
        write.data = &projection;
        write.offset = 0;
        write.size = sizeof(glm::mat4);
        ubo->writeToBuffer({ write });
    }

    void Camera::updateCameraView(const CameraTranslation& newTranslation)
    {
        this->translation = newTranslation;

        glm::quat qRotation = this->translation.qRotation;

        //calculate rotation from angle axis if not using quaternion
        if(!this->translation.useQuaternion)
        {
            glm::quat yawRot = glm::angleAxis(glm::radians(newTranslation.yaw), glm::vec3(0.0f, 0.0f, -1.0f));
            glm::quat pitchRot = glm::angleAxis(glm::radians(newTranslation.pitch - 90.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
            glm::quat qRotation =  pitchRot * yawRot;

            glm::quat zUpPitchRot = glm::angleAxis(glm::radians(newTranslation.pitch), glm::vec3(-1.0f, 0.0f, 0.0f));
            this->translation.qRotation = zUpPitchRot * yawRot;
        }

        glm::mat4 mRotation = glm::mat4_cast(qRotation);
        glm::mat4 mTranslation = glm::mat4(1.0f);

        mTranslation = glm::translate(mTranslation, -newTranslation.position);
        view = mRotation * mTranslation;

        BufferWrite write = {};
        write.data = &view;
        write.offset = sizeof(glm::mat4);
        write.size = sizeof(glm::mat4);
        ubo->writeToBuffer({ write });
    }
}