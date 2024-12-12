#version 460
#extension GL_GOOGLE_include_directive : require

#include "hitcommon.glsl"

layout(location = 0) rayPayloadInEXT HitPayload hitPayload;

//----------ENTRY POINT----------//

void main()
{
    const HitInfo hitInfo = getHitInfo();

    //output
    hitPayload.worldPosition = hitInfo.worldPosition;
    hitPayload.normal = hitInfo.normal;
    hitPayload.origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    hitPayload.matIndex = gl_GeometryIndexEXT; //geometry in AS build corresponds to material slot
    hitPayload.customIndex = gl_InstanceCustomIndexEXT;
    hitPayload.isMiss = false;
}
