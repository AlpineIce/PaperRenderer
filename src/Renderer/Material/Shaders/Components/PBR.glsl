struct AmbientLight
{
    vec4 color;
};

struct DirectLight
{
    vec4 direction;
    vec4 color;
};

struct PointLight
{
    vec4 position;
    vec4 color;
};

layout(location = 0) in vec3 worldPosition;

layout(std140, set = 0, binding = 1) readonly buffer PointLights
{
    PointLight lights[];
} pointLights;

layout(std140, set = 0, binding = 2) uniform LightInformation
{
    AmbientLight ambientLight;
    DirectLight directLight;
    vec3 camPos; //padded by pointLightCount
    uint pointLightCount;
} lightInfo;

//----------CALCULATION FUNCTIONS----------//

float distribution(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
	
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265359 * denom * denom;
	
    return num / (denom + 0.00001);
}

vec3 schlickFresnel(vec3 F0, float cosTheta)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float GeometrySchlickGGX(float AdotB, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num = AdotB;
    float denom = AdotB * (1.0 - k) + k;
	
    return num / (denom + 0.00001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

vec3 calculateLight(vec3 N, vec3 V, vec3 L, vec3 H, vec3 radiance, vec3 albedo, float roughness, float metallic)
{
    //fresnel
    float cosTheta = max(dot(N, L), 0.0);
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);
    vec3 F = schlickFresnel(F0, cosTheta);

    //cook torrance
    vec3 DFG = distribution(N, H, roughness) * F * GeometrySmith(N, V, L, roughness);
    float denom = 4.0 * max(dot(V, N), 0.0) * max(dot(V, H), 0.0);

    vec3 specular = DFG / (denom + 0.00001);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - vec3(metallic);
 
    return (kD * albedo / 3.14159265359 + specular) * radiance * cosTheta;
}

float attenuate(float distance)
{
    float linearness = 2.0;
    return (1.0 / ((distance + 1.0) * (distance + 1.0) + linearness)) * 5 * linearness;
}

//----------PBR FUNCTION DEFINITION & INPUT----------//

struct PBRinput
{
    vec4 albedo;
    float metallic;
    float roughness;
    vec3 normal;
};

//take inputs and output a vec4 color to be directly drawn on screen before post-processing
vec4 calculatePBR(PBRinput inputValues)
{
    //init important variables and calculate directional light first
    vec3 N = normalize(inputValues.normal);
    vec3 V = normalize(lightInfo.camPos - worldPosition);
    vec3 L = normalize(lightInfo.directLight.direction.xyz);
    vec3 H = normalize(V + L);
    vec3 radiance = lightInfo.directLight.color.xyz * lightInfo.directLight.color.w; //w is light power
    
    //add up total light from future calculations
    vec3 totalLight = lightInfo.ambientLight.color.xyz * lightInfo.ambientLight.color.w; //ambient light

    //directional light
    totalLight += calculateLight(N, V, L, H, radiance, inputValues.albedo.xyz, inputValues.roughness, inputValues.metallic);

    //point lights
    for(uint i = 0; i < lightInfo.pointLightCount; i++)
    {
        PointLight light = pointLights.lights[i];
        L = normalize(light.position.xyz - worldPosition);
        H = normalize(V + L);

        radiance = attenuate(length(light.position.xyz - worldPosition)) * light.color.xyz * light.color.w;

        totalLight += calculateLight(N, V, L, H, radiance, inputValues.albedo.xyz, inputValues.roughness, inputValues.metallic);
    }

    return vec4(totalLight, 1.0f);
}