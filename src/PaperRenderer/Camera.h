#pragma once
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "GLFW/glfw3.h"

#include <memory>

namespace PaperRenderer
{
    struct CameraTranslation
    {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::quat qRotation = glm::quat(glm::mat4(1.0f));
        bool useQuaternion = false;
    };

    struct CameraCreateInfo
    {
        float fov = 75.0f;
        float clipNear = 0.1f;
        float clipFar = 1000.0f;
        CameraTranslation initTranslation = {};
    };

    class Camera
    {
    private:
        glm::vec3 up;
        glm::vec3 right;
        glm::vec3 forward;

        glm::mat4 view;
        glm::mat4 projection;

        CameraTranslation translation;

        float clipNear;
        float clipFar;
        float fov;

        int width;
        int height;
        GLFWwindow* windowPtr;

        struct UBOData
        {
            glm::mat4 projection;
            glm::mat4 view;
        };
        std::unique_ptr<class Buffer> ubo;

        class RenderEngine& renderer;
        
    public:
        Camera(class RenderEngine& renderer, const CameraCreateInfo& creationInfo);
        ~Camera();
        Camera(const Camera&) = delete;

        void setClipSpace(float near, float far);
        void updateCameraProjection(); //updates projection to match window extent
        void updateCameraView(const CameraTranslation& newTranslation);
        
        const glm::mat4& getViewMatrix() const { return view; }
        const glm::mat4& getProjection() const { return projection; }
        const float getClipNear() const { return clipNear; }
        const float getClipFar() const { return clipFar; }

        CameraTranslation getTranslation() const { return translation; }
        const class Buffer& getCameraUBO() const { return *ubo; }
    };
}