#pragma once
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "GLFW/glfw3.h"

#include "Descriptor.h"

#include <memory>
#include <variant>

namespace PaperRenderer
{
    //----------CAMERA TRANSFORMATION----------//

    struct EulerRotation
    {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
    };

    struct CameraTransformationParameters
    {
        std::variant<EulerRotation, glm::quat> rotation = {};
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
    };

    struct PerspectiveCamera //WILL update to uneven image screen size
    {
        float yFov = 75.0f;
    };

    struct OrthographicCamera //does NOT adjust to uneven image screen size
    {
        glm::vec2 xyScale;
    };

    //creation info
    struct CameraInfo
    {
        std::variant<PerspectiveCamera, OrthographicCamera, glm::mat4> projection = {};
        std::variant<glm::mat4, CameraTransformationParameters> transformation = {};
        float clipNear = 0.1f;
        float clipFar = 1000.0f;
        uint32_t descriptorIndex = 0; //all shaders that use the camera matrices will need the corresponding set set to this index
    };

    //----------CAMERA CLASS----------//

    struct CameraUBOData
    {
        glm::mat4 projection;
        glm::mat4 view;
    };

    class Camera
    {
    private:
        CameraInfo cameraInfo;
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 projection = glm::mat4(1.0f);
        
        Buffer ubo;
        DescriptorGroup descriptorGroup;

        class RenderEngine& renderer;
        
    public:
        Camera(class RenderEngine& renderer, const CameraInfo& cameraInfo);
        ~Camera();
        Camera(const Camera&) = delete;

        void updateClipSpace(float near, float far); //implicitly updates projection after
        void updateProjection(const std::variant<PerspectiveCamera, OrthographicCamera, glm::mat4>& newProjection); //updates projection to match window extent (doesn't apply to orthographic)
        void updateView(const std::variant<glm::mat4, CameraTransformationParameters>& newTransform);
        void updateUBO(); //MUST be called to update the camera UBO
        
        const glm::mat4& getViewMatrix() const { return view; }
        const glm::mat4& getProjection() const { return projection; }
        glm::vec3 getPosition() const;

        CameraInfo getCameraInfo() const { return cameraInfo; }
        const class Buffer& getCameraUBO() const { return ubo; }
        const DescriptorGroup& getDescriptorGroup() const { return descriptorGroup; }
    };
}