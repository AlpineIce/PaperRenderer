#version 460
#extension GL_GOOGLE_include_directive : require

#include "hitcommon.glsl"
#include "pbr.glsl"
#include "leaf.glsl"

layout(location = 0) rayPayloadInEXT HitPayload hitPayload;
layout(location = 1) rayPayloadEXT bool isShadowed;

//----------ENTRY POINT----------//

void main()
{
    //clear magic number to 0 if set
    if(hitPayload.depth == depthClearMagicNumber) hitPayload.depth = 0;

    //increment recursion depth value
    hitPayload.depth++;

    //hit info
    const HitInfo hitInfo = getHitInfo();

    const uint matIndex = gl_GeometryIndexEXT; //geometry in AS build corresponds to material slot
    const uint customIndex = gl_InstanceCustomIndexEXT;

    //get random seed
    uint seed = tea(gl_LaunchSizeEXT.x * gl_LaunchIDEXT.x * gl_LaunchIDEXT.y, uint(inputData.frameNumber));

    ////grab material definition
    Material materialInfo = materialDefinitions.materials[customIndex + matIndex];

    BRDFInput inputValues;
    inputValues.baseColor = vec4(materialInfo.albedo, 1.0) * getOcclusion(hitInfo.uv);
    inputValues.emissive = vec4(materialInfo.emissive, 1.0);
    inputValues.roughness = materialInfo.roughness;
    inputValues.metallic = materialInfo.metallic;

    //bruh moment
    if(matIndex == 1) materialInfo.albedo = vec3(1.0, 0.0, 0.0);;

    //get camera position from view matrix
    const vec3 camPos = inverse(cameraData.view)[3].xyz;
    
    //normal and camera direction vectors
    const vec3 N = normalize(hitInfo.normal);
    const vec3 V = normalize(camPos.xyz - hitInfo.worldPosition);

    //----------POINT LIGHTS----------//

    vec3 totalLight = vec3(0.0);
    for(uint i = 0; i < lightInfo.pointLightCount; i++)
    {
        const PointLight light = pointLights.lights[i];
        vec3 L = normalize(light.position.xyz - hitInfo.worldPosition);
        const vec3 H = normalize(V + L);

        //only do lighting calculations if light is "visible"
        if(dot(N, L) > 0.0)
        {
            //calculate shadows if enabled
            float visibility = 0.0;
            if(light.castShadow && inputData.shadowSamples > 0)
            {
                //perform n samples
                for(uint j = 0; j < inputData.shadowSamples; j++)
                {
                    //get new light location if radius is greater than 0.0
                    vec3 lightPosition;
                    if(light.radius > 0.0)
                    {
                        //initialize random numbers
                        float r1 = rnd(seed);
                        float r2 = rnd(seed);

                        //tangents and bitangents
                        vec3 Ltangent;
                        vec3 Lbitangent;
                        ComputeDefaultBasis(L, Ltangent, Lbitangent);

                        //TODO (maybe?) LARGE POINT LIGHTS ARE INNACURATE
                        //new position on a sphere
                        float sq = sqrt(1.0 - r2);
                        float phi = 2.0 * PI * r1;

                        lightPosition = vec3(cos(phi) * sq, sin(phi) * sq, sqrt(r2)) * light.radius;
                        lightPosition = (lightPosition.x * Ltangent + lightPosition.y * Lbitangent + lightPosition.z * L) + light.position;
                        L = normalize(lightPosition - hitInfo.worldPosition);
                    }

                    //cast ray
                    isShadowed = true;
                    traceRayEXT(
                        accelerationStructureEXT(inputData.tlasAddress),  // acceleration structure
                        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,       // rayFlags
                        0xFF,        // cullMask
                        0,           // sbtRecordOffset
                        0,           // sbtRecordStride
                        1,           // missIndex
                        hitInfo.worldPosition,      // ray origin
                        0.001,        // ray min range
                        L,      // ray direction
                        length(light.position.xyz - hitInfo.worldPosition),        // ray max range
                        1            // payload (location = 1)
                    );

                    //update visibility
                    if(!isShadowed)
                    {
                        visibility += 1.0;
                    }
                }

                visibility = visibility / float(inputData.shadowSamples);
            }
            else //no shadows, visiblity should be 1.0
            {
                visibility = 1.0;
            }

            if(visibility > 0.0)
            {
                totalLight += calculatePointLight(N, V, hitInfo.worldPosition, inputValues, light) * visibility;
            }
        }
    }

    //dont do reflections or translucency because its expensive

    //----------AMBIENT LIGHT----------//

    float ambientOcclusion = 1.0;
    if(inputData.aoRadius > 0.0 && inputData.aoSamples > 0) //if RTAO is enabled
    {
        vec3 tangent;
        vec3 bitangent;
        ComputeDefaultBasis(hitInfo.normal, tangent, bitangent);

        ambientOcclusion = 0.0;
        for(uint i = 0; i < inputData.aoSamples; i++)
        {
            const vec3 direction = cosineSample(hitInfo.normal, tangent, bitangent, 1.0, seed);

            const uint aoFlags = gl_RayFlagsNoneEXT; //gl_RayFlagsTerminateOnFirstHitEXT
            
            rayQueryEXT rayQuery;
            rayQueryInitializeEXT(
                rayQuery,
                accelerationStructureEXT(inputData.tlasAddress),
                aoFlags,
                0xFF,
                OffsetRay(hitInfo.worldPosition, N),
                0.001f,
                direction,
                inputData.aoRadius
            );

            // Start traversal: return false if traversal is complete
            while(rayQueryProceedEXT(rayQuery))
            {
            }

            // Returns type of committed (true) intersection
            if(rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT)
            {
                float length = 1 - (rayQueryGetIntersectionTEXT(rayQuery, true) / inputData.aoRadius);
                ambientOcclusion += length;  // * length;
            }
        }

        //factor in roughness and metallic
        const float ambientLightInfluence = mix(1.0, materialInfo.roughness, materialInfo.metallic);

        ambientOcclusion = 1.0 - (ambientOcclusion / float(inputData.aoSamples));
        ambientOcclusion = clamp(ambientOcclusion, 0.0, 1.0) * ambientLightInfluence;
    }

    //ambient
    totalLight += lightInfo.ambientLight.xyz * lightInfo.ambientLight.w * ambientOcclusion * materialInfo.albedo;

    //add emission
    totalLight += materialInfo.emissive.xyz;

    //set return value
    hitPayload.returnColor = totalLight;

    //decrement recursion depth value
    hitPayload.depth--;
}