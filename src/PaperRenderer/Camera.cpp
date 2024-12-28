#include "Camera.h"
#include "PaperRenderer.h"

#include <functional>

namespace PaperRenderer
{
    Camera::Camera(RenderEngine& renderer, const CameraInfo& cameraInfo)
        :cameraInfo(cameraInfo),
        ubo(renderer, {
            .size = sizeof(UBOData) * 2,
            .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR,
            .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        }),
        renderer(renderer)
    {
        updateProjection(cameraInfo.projection, cameraInfo.projectionType);
        updateView(cameraInfo.transformation, cameraInfo.transformationType);
    }

    Camera::~Camera()
    {
    }

    void Camera::updateClipSpace(float near, float far)
    {
        cameraInfo.clipFar = near;
        cameraInfo.clipFar = far;
        updateProjection(cameraInfo.projection, cameraInfo.projectionType);
    }

    void Camera::updateProjection(const CameraProjection& newProjection, CameraProjectionType projectionType)
    {
        //update camera info
        cameraInfo.projectionType = projectionType;
        cameraInfo.projection = newProjection;

        //get screen ratio
        const VkExtent2D extent = renderer.getSwapchain().getExtent();
        const float screenRatio = (float)extent.width / (float)extent.height;
        
        //set projection based on projectionType
        switch(projectionType)
        {
        case PERSPECTIVE:
            projection = glm::perspective(glm::radians(cameraInfo.projection.perspective.yFov), screenRatio, cameraInfo.clipNear, cameraInfo.clipFar);
            break;
        case ORTHOGRAPHIC:
            //TODO FIGURE OUT WHY BROKEN AND SAD
            const glm::vec2 xyScale = cameraInfo.projection.orthographic.xyScale;
            projection = glm::ortho(-xyScale.x / 2.0f, xyScale.x / 2.0f, -xyScale.y / 2.0f, xyScale.y / 2.0f);
            break;
        }
    }

    void Camera::updateView(const CameraTransformation& newTransform, CameraTransformationType transformType)
    {
        //update camera info
        cameraInfo.transformationType = transformType;
        cameraInfo.transformation = newTransform;

        if(transformType == MATRIX)
        {
            view = cameraInfo.transformation.viewMatrix;
        }
        else if(transformType == PARAMETERS)
        {
            //create copy of transformation parameters
            CameraTransformationParameters parameters = cameraInfo.transformation.translationParameters; //not a reference
            
            //calculate quaternion from euler angles if needed
            if(parameters.rotationType == EULER)
            {
                //TODO VERIFY THIS STUFF IS RIGHT

                //calculate qRotation
                glm::quat yawRot = glm::angleAxis(glm::radians(parameters.rotation.eRotation.yaw), glm::vec3(0.0f, 0.0f, -1.0f));
                glm::quat pitchRot = glm::angleAxis(glm::radians(parameters.rotation.eRotation.pitch - 90.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
                glm::quat zUpPitchRot = glm::angleAxis(glm::radians(parameters.rotation.eRotation.pitch), glm::vec3(-1.0f, 0.0f, 0.0f));

                //set qRotation
                parameters.rotation.qRotation = zUpPitchRot * yawRot;
                parameters.rotationType = QUATERNION;
            }

            //normalize
            parameters.rotation.qRotation = glm::normalize(parameters.rotation.qRotation);

            glm::mat4 mRotation = glm::mat4_cast(parameters.rotation.qRotation);
            glm::mat4 mTranslation = glm::mat4(1.0f);

            //add position
            mTranslation = glm::translate(mTranslation, glm::vec3(-parameters.position.x, -parameters.position.y, -parameters.position.z));
            view = mRotation * mTranslation;
        }
    }

    void Camera::updateUBO()
    {
        UBOData uboData = {
            .projection = projection,
            .view = view
        };

        BufferWrite write = {
            .offset = 0,
            .size = sizeof(UBOData),
            .readData = &uboData
        };
        ubo.writeToBuffer({ write });
    }

    glm::vec3 Camera::getPosition() const
    {
        glm::mat4 viewInverse = glm::inverse(view);

        return glm::vec3(viewInverse[3][0], viewInverse[3][1], viewInverse[3][2]);
    }
}