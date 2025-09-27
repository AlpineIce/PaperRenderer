#include "Camera.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    Camera::Camera(RenderEngine& renderer, const CameraInfo& cameraInfo)
        :cameraInfo(cameraInfo),
        ubo(renderer, {
            .size = sizeof(CameraUBOData) * 2,
            .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR,
            .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        }),
        uboDescriptor(renderer, renderer.getDefaultDescriptorSetLayout(CAMERA_MATRICES)),
        renderer(&renderer)
    {
        //update matrices to initial values
        updateProjection(cameraInfo.projection);
        updateView(cameraInfo.transformation);

        //update descriptors
        uboDescriptor.updateDescriptorSet({
            .bufferWrites = { {
                .infos = { {
                    .buffer = ubo.getBuffer(),
                    .offset = 0,
                    .range = sizeof(CameraUBOData)
                } },
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .binding = 0,
            } }
        });
    }

    Camera::~Camera()
    {
    }

    Camera::Camera(Camera&& other) noexcept
        :cameraInfo(other.cameraInfo),
        view(other.view),
        projection(other.projection),
        ubo(std::move(other.ubo)),
        uboDescriptor(std::move(other.uboDescriptor)),
        renderer(other.renderer)
    {
        other.cameraInfo = {};
        other.view = glm::mat4(1.0f);
        other.projection = glm::mat4(1.0f);
        other.renderer = NULL;
    }

    Camera& Camera::operator=(Camera&& other) noexcept
    {
        if(this != &other)
        {
            cameraInfo = other.cameraInfo;
            view = other.view;
            projection = other.projection;
            ubo = std::move(other.ubo);
            uboDescriptor = std::move(other.uboDescriptor);
            renderer = other.renderer;
            

            other.cameraInfo = {};
            other.view = glm::mat4(1.0f);
            other.projection = glm::mat4(1.0f);
            other.renderer = NULL;
        }

        return *this;
    }

    void Camera::updateClipSpace(float near, float far)
    {
        cameraInfo.clipFar = near;
        cameraInfo.clipFar = far;
        updateProjection(cameraInfo.projection);
    }

    void Camera::updateProjection(const std::variant<PerspectiveCamera, OrthographicCamera, glm::mat4>& newProjection)
    {
        //update camera info
        cameraInfo.projection = newProjection;

        //early return if custom matrix type
        if(std::holds_alternative<glm::mat4>(newProjection))
        {
            projection = std::get<glm::mat4>(newProjection);
            return;
        }

        //get screen ratio
        const VkExtent2D extent = renderer->getSwapchain().getExtent();
        const float screenRatio = (float)extent.width / (float)extent.height;
        
        //set projection based on projectionType
        switch(cameraInfo.projection.index())
        {
        case 0: //perspective
            projection = glm::perspective(glm::radians(std::get<PerspectiveCamera>(cameraInfo.projection).yFov), screenRatio, cameraInfo.clipNear, cameraInfo.clipFar);
            break;
        case 1: //orthographic
            const glm::vec2 xyScale = std::get<OrthographicCamera>(cameraInfo.projection).xyScale;
            projection = glm::ortho(-xyScale.x, xyScale.x, -xyScale.y, xyScale.y, cameraInfo.clipNear, cameraInfo.clipFar);
            break;
        }
    }

    void Camera::updateView(const std::variant<glm::mat4, CameraTransformationParameters>& newTransform)
    {
        //update camera info
        cameraInfo.transformation = newTransform;

        if(std::holds_alternative<glm::mat4>(cameraInfo.transformation))
        {
            view = std::get<glm::mat4>(cameraInfo.transformation);
        }
        else if(std::holds_alternative<CameraTransformationParameters>(cameraInfo.transformation))
        {
            //create copy of transformation parameters
            CameraTransformationParameters parameters = std::get<CameraTransformationParameters>(cameraInfo.transformation); //not a reference
            
            //calculate quaternion from euler angles if needed
            if(std::holds_alternative<EulerRotation>(parameters.rotation))
            {
                //TODO VERIFY THIS STUFF IS RIGHT

                //calculate qRotation
                glm::quat yawRot = glm::angleAxis(glm::radians(std::get<EulerRotation>(parameters.rotation).yaw), glm::vec3(0.0f, 0.0f, -1.0f));
                glm::quat pitchRot = glm::angleAxis(glm::radians(std::get<EulerRotation>(parameters.rotation).pitch - 90.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
                glm::quat zUpPitchRot = glm::angleAxis(glm::radians(std::get<EulerRotation>(parameters.rotation).pitch), glm::vec3(-1.0f, 0.0f, 0.0f));

                //set qRotation
                parameters.rotation = zUpPitchRot * yawRot;
            }

            //normalize
            parameters.rotation = glm::normalize(std::get<glm::quat>(parameters.rotation));

            glm::mat4 mRotation = glm::mat4_cast(std::get<glm::quat>(parameters.rotation));
            glm::mat4 mTranslation = glm::mat4(1.0f);

            //add position
            mTranslation = glm::translate(mTranslation, glm::vec3(-parameters.position.x, -parameters.position.y, -parameters.position.z));
            view = mRotation * mTranslation;
        }
    }

    void Camera::updateUBO()
    {
        CameraUBOData uboData = {
            .projection = projection,
            .view = view
        };

        BufferWrite write = {
            .offset = sizeof(CameraUBOData) * renderer->getBufferIndex(),
            .size = sizeof(CameraUBOData),
            .readData = &uboData
        };
        ubo.writeToBuffer({ write });
    }

    glm::vec3 Camera::getPosition() const
    {
        glm::mat4 viewInverse = glm::inverse(view);

        return glm::vec3(viewInverse[3][0], viewInverse[3][1], viewInverse[3][2]);
    }
    
    const uint32_t Camera::getUBODynamicOffset() const
    {
        return sizeof(CameraUBOData) * renderer->getBufferIndex(); 
    }
}