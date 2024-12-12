#version 460
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 color;

layout(std430, set = 0, binding = 0) uniform InputData
{
    vec4 colorFilter;
    float exposure;
    float WBtemp;
    float WBtint;
    float contrast;
    float brightness;
    float saturation;
    float gammaCorrection;
} inputData;

layout(set = 0, binding = 1) uniform sampler2D InputBaseColor;

//https://docs.unity3d.com/Packages/com.unity.shadergraph@6.9/manual/White-Balance-Node.html (literally ripped straight from the website with very little modification)
vec3 whiteBalance(vec3 pixel, float temperature, float tint)
{
    // Range ~[-1.67;1.67] works best
    float t1 = temperature * 10 / 6;
    float t2 = tint * 10 / 6;

    // Get the CIE xy chromaticity of the reference white point.
    // Note: 0.31271 = x value on the D65 white point
    float x = 0.31271 - t1 * (t1 < 0 ? 0.1 : 0.05);
    float standardIlluminantY = 2.87 * x - 3 * x * x - 0.27509507;
    float y = standardIlluminantY + t2 * 0.05;

    // Calculate the coefficients in the LMS space.
    vec3 w1 = vec3(0.949237, 1.03542, 1.08728); // D65 white point

    // CIExyToLMS
    float Y = 1;
    float X = Y * x / y;
    float Z = Y * (1 - x - y) / y;
    float L = 0.7328 * X + 0.4296 * Y - 0.1624 * Z;
    float M = -0.7036 * X + 1.6975 * Y + 0.0061 * Z;
    float S = 0.0030 * X + 0.0136 * Y + 0.9834 * Z;
    vec3 w2 = vec3(L, M, S);

    vec3 balance = vec3(w1.x / w2.x, w1.y / w2.y, w1.z / w2.z);

    const mat3 LIN_2_LMS_MAT = mat3(
        3.90405e-1, 5.49941e-1, 8.92632e-3,
        7.08416e-2, 9.63172e-1, 1.35775e-3,
        2.31082e-2, 1.28021e-1, 9.36245e-1
    );

    const mat3 LMS_2_LIN_MAT = mat3(
        2.85847e+0,  -1.62879e+0, -2.48910e-2,
        -2.10182e-1,  1.15820e+0,  3.24281e-4,
        -4.18120e-2, -1.18169e-1,  1.06867e+0
    );

    vec3 lms = pixel * LIN_2_LMS_MAT;
    lms *= balance;
    return lms * LMS_2_LIN_MAT;
}

vec3 HillACES(vec3 pixel)
{
    const mat3 ACESInputMat = mat3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    );

    const mat3 ACESOutputMat = mat3(
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    );

    vec3 color = pixel * ACESInputMat;

    vec3 a = color * (color + 0.0245786f) - 0.000090537f;
    vec3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
    color = a / b;

    color = color * ACESOutputMat;

    return clamp(color, 0.0, 1.0);
}

void main()
{
    vec4 pixel = texture(InputBaseColor, texCoord);

    //----------HIGH DYNAMIC RANGE----------//
    
    //exposure
    pixel.xyz = pixel.xyz * inputData.exposure;
    pixel.x = max(0.0, pixel.x);
    pixel.y = max(0.0, pixel.y);
    pixel.z = max(0.0, pixel.z);

    //white balance
    pixel.xyz = whiteBalance(pixel.xyz, inputData.WBtemp, inputData.WBtint);
    pixel.x = max(0.0, pixel.x);
    pixel.y = max(0.0, pixel.y);
    pixel.z = max(0.0, pixel.z);

    //contrast and brightness
    pixel.xyz = inputData.contrast * (pixel.xyz - 0.5) + 0.5 + inputData.brightness;
    pixel.x = max(0.0, pixel.x);
    pixel.y = max(0.0, pixel.y);
    pixel.z = max(0.0, pixel.z);

    //color filtering
    pixel.xyz *= inputData.colorFilter.xyz;

    //saturation
    const vec3 grayscaleConstant = vec3(0.299, 0.587, 0.114);
    float grayscalePixel = dot(pixel.xyz, grayscaleConstant);
    pixel.x = max(0.0, mix(grayscalePixel, pixel.x, inputData.saturation));
    pixel.y = max(0.0, mix(grayscalePixel, pixel.y, inputData.saturation));
    pixel.z = max(0.0, mix(grayscalePixel, pixel.z, inputData.saturation));

    //tonemapping
    pixel.xyz = HillACES(pixel.xyz); //converts into LDR

    //----------LOW DYNAMIC RANGE----------//

    //gamma correction
    pixel.x = pow(pixel.x, inputData.gammaCorrection);
    pixel.y = pow(pixel.y, inputData.gammaCorrection);
    pixel.z = pow(pixel.z, inputData.gammaCorrection);
    
    color = vec4(1.0, 0.0, 1.0, 1.0);//pixel;
}