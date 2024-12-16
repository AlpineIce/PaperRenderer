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

        //queue data transfer
        std::vector<char> uboData(sizeof(glm::mat4));
        memcpy(uboData.data(), &projection, sizeof(glm::mat4));
        
        renderer.getStagingBuffer().queueDataTransfers(*ubo, 0, uboData);
    }

    void Camera::updateCameraView(const CameraTranslation& newTranslation)
    {
        this->translation = newTranslation;
        this->translation.qRotation = glm::normalize(this->translation.qRotation);

        //calculate rotation from angle axis if not using quaternion
        if(!this->translation.useQuaternion)
        {
            glm::quat yawRot = glm::angleAxis(glm::radians(newTranslation.yaw), glm::vec3(0.0f, 0.0f, -1.0f));
            glm::quat pitchRot = glm::angleAxis(glm::radians(newTranslation.pitch - 90.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
            glm::quat qRotation =  pitchRot * yawRot;

            glm::quat zUpPitchRot = glm::angleAxis(glm::radians(newTranslation.pitch), glm::vec3(-1.0f, 0.0f, 0.0f));
            this->translation.qRotation = zUpPitchRot * yawRot;
        }

        glm::mat4 mRotation = glm::mat4_cast(this->translation.qRotation);
        glm::mat4 mTranslation = glm::mat4(1.0f);

        mTranslation = glm::translate(mTranslation, glm::vec3(-newTranslation.position.x, -newTranslation.position.y, -newTranslation.position.z));
        view = mRotation * mTranslation;

        //queue data transfer
        std::vector<char> uboData(sizeof(glm::mat4));
        memcpy(uboData.data(), &view, sizeof(glm::mat4));
        
        renderer.getStagingBuffer().queueDataTransfers(*ubo, sizeof(glm::mat4), uboData);
    }
}