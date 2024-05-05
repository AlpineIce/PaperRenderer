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
        up = glm::vec3(0.0f, 1.0f, 0.0f);
        right = glm::vec3(1.0f, 0.0f, 0.0f);
        forward = glm::vec3(0.0f, 0.0f, 1.0f);

        //kinda from stack overflow ngl
        glm::quat qPitch = glm::angleAxis(glm::radians(translation.pitch), -right);
        glm::quat qYaw = glm::angleAxis(glm::radians(translation.yaw), up);
        //glm::quat qRoll = glm::angleAxis(glm::radians(0.0f), forward);

        this->translation.qRotation = qPitch * qYaw;// * qRoll; 

        glm::mat4 mRotation = glm::mat4_cast(glm::normalize(this->translation.qRotation));
        glm::mat4 mTranslation = glm::mat4(1.0f);

        mTranslation = glm::translate(mTranslation, -translation.position);

        view = mRotation * mTranslation;
    }
}