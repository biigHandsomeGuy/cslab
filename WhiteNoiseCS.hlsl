RWTexture2D<float4> PerlinTexture : register(u0); // UAV 目标纹理
cbuffer CB : register(b0)
{
    float2 NoiseScale; // 控制噪声细节 (放大噪声坐标)
    float scale;
}


// 更强的哈希函数（随机性增强）
uint Hash(int x, int y)
{
    uint a = (x * 1836311903) + (y * 2971215073);
    a = (a ^ (a >> 16)) * 0x85ebca6b;
    a = (a ^ (a >> 13)) * 0xc2b2ae35;
    a = a ^ (a >> 16);
    return a & 7; // 返回一个 0-7 之间的值
}
float fract(float x) {
    return x - floor(x);
}
float WhiteNoise(float x, float y)
{
    return fract(sin(dot(float2(x,y),NoiseScale))*scale);

}

// Compute Shader 主函数
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 dims;
    PerlinTexture.GetDimensions(dims.x, dims.y);

    // 计算当前像素的 UV 坐标
    float2 uv = float2(id.xy) / float2(dims);

    // 使用多重分辨率噪声生成复杂的噪声图
    float noiseValue = WhiteNoise(uv.x * NoiseScale.x, uv.y * NoiseScale.y);

    // 将噪声值写入目标纹理
    PerlinTexture[id.xy] = float4(noiseValue, noiseValue, noiseValue, 1);
}
