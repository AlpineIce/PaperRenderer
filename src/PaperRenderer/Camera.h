#pragma once
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "GLFW/glfw3.h"

namespace PaperRenderer
{
    struct CameraTranslation
    {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::quat qRotation = glm::quat(glm::mat4(1.0f));
    };

    struct CameraCreateInfo
    {
        float fov = 75.0f;
        float clipNear = 0.1f;
        float clipFar = 1000.0f;

        CameraTranslation initTranslation;
        GLFWwindow* window;
    };

    struct CameraFrustum
    {
        glm::vec4 frustum; //(left, right, top, bottom)
        glm::vec2 zPlanes; //(near, far)
        glm::vec2 padding;
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
        CameraFrustum frustum; 

        float clipNear;
        float clipFar;
        float fov;

        int width;
        int height;
        GLFWwindow* windowPtr;

        void createFrustum();
        
    public:
        Camera(const CameraCreateInfo& creationInfo);
        ~Camera();

        
        static glm::vec4 normalizePlane(glm::vec4 plane);

        void setClipSpace(float near, float far);
        void updateCameraProjection(); //updates projection to match window extent
        void updateCameraView(const CameraTranslation& newTranslation);
        
        const glm::mat4& getViewMatrix() const { return view; }
        const glm::mat4& getProjection() const { return projection; }
        const float getClipNear() const { return clipNear; }
        const float getClipFar() const { return clipFar; }

        CameraTranslation getTranslation() const { return translation; }
        CameraFrustum getFrustum() const { return frustum; }
    };
}