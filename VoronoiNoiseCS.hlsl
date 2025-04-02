#include "Noise.hlsli"

RWTexture2D<float4> PerlinTexture : register(u0);
cbuffer CB : register(b0)
{
    float NoiseScale;
    int pad;
    float time;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 dims;
    PerlinTexture.GetDimensions(dims.x, dims.y);

    float2 uv = float2(id.xy) / float2(dims);
    
    uv = uv * 2 - 1;
    
    uv *= NoiseScale;
        
    float min_distance = 10000;
    float4 color = 0;
    
        
    float2 local_uv = frac(uv);
    float2 block_id = floor(uv);
    
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 offset = float2(x, y);
            
            float2 n = rand2dTo2d(block_id + offset);
            float2 block_position = offset + sin(n * time)*0.3;
            float distance = length(local_uv - block_position);

            if (distance < min_distance)
            {
                min_distance = distance;
            }
            
        }

    }
    
    color = min_distance;

    PerlinTexture[id.xy] = color;
}
