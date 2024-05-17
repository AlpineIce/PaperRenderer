#include "Camera.h"

namespace PaperRenderer
{
    Camera::Camera(const CameraCreateInfo& creationInfo)
        :windowPtr(creationInfo.window),
        clipNear(creationInfo.clipNear),
        clipFar(creationInfo.clipFar),
        fov(creationInfo.fov),
        translation(creationInfo.initTranslation)
    {
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
        this->projection = glm::perspective(glm::radians(fov), (float)width / (float)height, clipNear, clipFar);
    }

    void Camera::updateCameraView(const CameraTranslation& newTranslation)
    {
        this->translation = newTranslation;

        glm::quat yawRot = glm::angleAxis(glm::radians(newTranslation.yaw), glm::vec3(0.0f, 0.0f, -1.0f));
        glm::quat pitchRot = glm::angleAxis(glm::radians(newTranslation.pitch - 90.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
        glm::quat qRotation =  pitchRot * yawRot;

        glm::quat zUpPitchRot = glm::angleAxis(glm::radians(newTranslation.pitch), glm::vec3(-1.0f, 0.0f, 0.0f));
        this->translation.qRotation = zUpPitchRot * yawRot;

        glm::mat4 mRotation = glm::mat4_cast(qRotation);
        glm::mat4 mTranslation = glm::mat4(1.0f);

        mTranslation = glm::translate(mTranslation, -newTranslation.position);
        view = mRotation * mTranslation;
    }
}