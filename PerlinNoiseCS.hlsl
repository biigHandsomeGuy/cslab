#include "Noise.hlsli"

RWTexture2D<float4> PerlinTexture : register(u0); 
cbuffer CB : register(b0)
{
    float NoiseScale; 
    int Octaves;
}

static const float2 Gradients[8] =
{
    float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
    float2(0.707, 0.707), float2(-0.707, 0.707), float2(0.707, -0.707), float2(-0.707, -0.707)
};

uint Hash(int x, int y)
{
    uint a = (x * 1836311903) + (y * 2971215073);
    a = (a ^ (a >> 16)) * 0x85ebca6b;
    a = (a ^ (a >> 13)) * 0xc2b2ae35;
    a = a ^ (a >> 16);
    return a & 7;
}

float GradientDot(int2 grid_coord, float2 uv)
{
    float2 gradient = Gradients[Hash(grid_coord.x, grid_coord.y)];
    return dot(gradient, float2(grid_coord - uv));
}



float PerlinNoise(float2 uv)
{
    float2 block_id = floor(uv);
    
    float2 local_uv = Fade(frac(uv));
    
    float top_left = GradientDot(block_id, uv);
    float top_right = GradientDot(block_id + float2(1, 0), uv);
     
    float bottom_left = GradientDot(block_id + float2(0, 1), uv);
    float bottom_right = GradientDot(block_id + float2(1, 1), uv);
    
    float lerp_top = lerp(top_left, top_right, local_uv.x);
    float lerp_bottom = lerp(bottom_left, bottom_right, local_uv.x);
    
    return lerp(lerp_top, lerp_bottom, local_uv.y) * 0.5 + 0.5;
}

float fbm(float2 uv)
{
    float color = 0;
    
    float frequency = 1;
    float amplitude = 1;
    float stacking = 0;
    for (int i = 0; i < Octaves; i++)
    {
        stacking += amplitude;
        color += amplitude * PerlinNoise(uv * NoiseScale.x * frequency);
        frequency *= 1.8;
        
        amplitude *= 0.7;

    }

    color /= stacking;
    return color;
}

float pattern(float2 p)
{

    return fbm(p + fbm(p + fbm(p)));
}


[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 dims;
    PerlinTexture.GetDimensions(dims.x, dims.y);

    float2 uv = float2(id.xy) / float2(dims);

    float3 color = fbm(uv);
    
  
    PerlinTexture[id.xy] = float4(color, 1);
}
