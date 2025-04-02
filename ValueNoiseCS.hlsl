#include "Noise.hlsli"

RWTexture2D<float4> PerlinTexture : register(u0);
cbuffer CB : register(b0)
{
    float NoiseScale;
    int Octaves;
}

float ValueNoise(float2 uv)
{
    float2 local_uv = Fade(frac(uv));
    
    float2 block_id = floor(uv);
    
    float bottom_left = rand2dTo1d(block_id + float2(0, 1));
    float bottom_right = rand2dTo1d(block_id + float2(1, 1));
    float bottom = lerp(bottom_left, bottom_right, local_uv.x);
    
    float top_left = rand2dTo1d(block_id);
    float top_right = rand2dTo1d(block_id + float2(1, 0));
    float top = lerp(top_left, top_right, local_uv.x);
    
    return lerp(top, bottom, local_uv.y);
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 dims;
    PerlinTexture.GetDimensions(dims.x, dims.y);

    float2 uv = float2(id.xy) / float2(dims);
    

    float3 color = 0;
    
    float frequency = 1;
    float amplitude = 1;
    float stacking = 0;
    for (int i = 0; i < Octaves; i++)
    {
        stacking += amplitude;
        color += amplitude * ValueNoise(uv * NoiseScale.x * frequency);
        frequency *= 2;
        
        amplitude *= 0.5;

    }
    
    color /= stacking;
    
    
    PerlinTexture[id.xy] = float4(color, 1);
}
