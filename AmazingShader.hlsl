RWTexture2D<float4> PerlinTexture : register(u0);
cbuffer CB : register(b0)
{
    float2 NoiseScale;
    float time;
}
float3 palette(float t)
{
    float3 a = float3(0.5, 0.5, 0.5);
    float3 b = float3(0.5, 0.5, 0.5);
    float3 c = float3(1.0, 1.0, 1.0);
    float3 d = float3(0.263, 0.416, 0.557);
    
    return a + b * cos(6.28318 * (c * t + d));
}
// Compute Shader 主函数
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 dims;
    PerlinTexture.GetDimensions(dims.x, dims.y);

    float2 uv = float2(id.xy) / float2(dims);
    
    float2 uv0 = uv;
    
    uv = frac(uv) - 0.5;
    
    float d = length(uv);
    
    float3 color = palette(time);
    
    d = sin(d * 8.0 + time) / 8.0;
    d = abs(d);
    
    //d = smoothstep(0.0, 0.1, d);
    
    d = 0.02 / d;
    
    color *= d;
    
    
    // 将噪声值写入目标纹理
    PerlinTexture[id.xy] = float4(color, 1);
}
