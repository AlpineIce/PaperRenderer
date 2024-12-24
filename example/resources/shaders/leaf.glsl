float getAlpha(vec2 uv)
{
    const float x = uv.x;
    const float y = uv.y - 0.5; //split uv down middle
    const float curve = (-((1 - 2 * x) * (1 - 2 * x)) + 1) * 0.2;

    if(abs(y) < curve)
    {
        return 1.0f;
    }
    else
    {
        return 0.0f;
    }
}

float getOcclusion(vec2 uv)
{
    return (uv.x * 0.5) + 0.5;
}