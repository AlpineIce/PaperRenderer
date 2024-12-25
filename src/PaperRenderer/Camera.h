#pragma once
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "GLFW/glfw3.h"

#include "VulkanResources.h"

#include <memory>

namespace PaperRenderer
{
    //----------CAMERA TRANSFORMATION----------//

    //rotation types
    enum CameraRotationType
    {
        EULER,
        QUATERNION
    };

    struct EulerRotation
    {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
    };

    union CameraRotation
    {
        EulerRotation eRotation;
        glm::quat qRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); //default initialize to quaternion
    };
    
    //transformation types
    enum CameraTransformationType
    {
        MATRIX,
        PARAMETERS
    };

    struct CameraTransformationParameters
    {
        CameraRotationType rotationType = QUATERNION; //default rotation is quaternion, also default initializer
        CameraRotation rotation = {};
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
    };

    union CameraTransformation
    {
        glm::mat4 viewMatrix; //raw matrix input
        CameraTransformationParameters translationParameters = {}; //default initialize to use parameters for view construction
    };

    //camera projection
    enum CameraProjectionType
    {
        PERSPECTIVE,
        ORTHOGRAPHIC
    };

    struct PerspectiveCamera //WILL update to uneven image screen size
    {
        float yFov = 75.0f;
    };

    struct OrthographicCamera //does NOT adjust to uneven image screen size
    {
        glm::vec2 xyScale;
    };

    union CameraProjection
    {
        PerspectiveCamera perspective = {}; //default initialize to use perspective
        OrthographicCamera orthographic;
    };

    //creation info
    struct CameraInfo
    {
        CameraProjectionType projectionType = PERSPECTIVE;
        CameraProjection projection = {};
        CameraTransformationType transformationType = PARAMETERS;
        CameraTransformation transformation = {};
        float clipNear = 0.1f;
        float clipFar = 1000.0f;
    };

    //----------CAMERA CLASS----------//

    class Camera
    {
    private:
        CameraInfo cameraInfo;
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 projection = glm::mat4(1.0f);

        struct UBOData
        {
            glm::mat4 projection;
            glm::mat4 view;
        };
        Buffer ubo;

        class RenderEngine& renderer;
        
    public:
        Camera(class RenderEngine& renderer, const CameraInfo& cameraInfo);
        ~Camera();
        Camera(const Camera&) = delete;

        void updateClipSpace(float near, float far); //implicitly updates projection after
        void updateProjection(const CameraProjection& newProjection, CameraProjectionType projectionType); //updates projection to match window extent (doesn't apply to orthographic)
        void updateView(const CameraTransformation& newTransform, CameraTransformationType transformType);
        void updateUBO(); //MUST be called to update the camera UBO
        
        const glm::mat4& getViewMatrix() const { return view; }
        const glm::mat4& getProjection() const { return projection; }
        glm::vec3 getPosition() const;

        CameraInfo getCameraInfo() const { return cameraInfo; }
        const class Buffer& getCameraUBO() const { return ubo; }
    };
}