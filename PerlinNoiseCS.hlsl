RWTexture2D<float4> PerlinTexture : register(u0); // UAV 目标纹理
cbuffer CB : register(b0)
{
    float2 NoiseScale; // 控制噪声细节 (放大噪声坐标)
}

// 梯度方向（增强梯度多样性）
static const float2 Gradients[8] =
{
    float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
    float2(0.707, 0.707), float2(-0.707, 0.707), float2(0.707, -0.707), float2(-0.707, -0.707)
};

// 更强的哈希函数（随机性增强）
uint Hash(int x, int y)
{
    uint a = (x * 1836311903) + (y * 2971215073);
    a = (a ^ (a >> 16)) * 0x85ebca6b;
    a = (a ^ (a >> 13)) * 0xc2b2ae35;
    a = a ^ (a >> 16);
    return a & 7; // 返回一个 0-7 之间的值
}

// 获取梯度点积
float GradientDot(int ix, int iy, float x, float y)
{
    float2 gradient = Gradients[Hash(ix, iy)];
    return dot(gradient, float2(x - ix, y - iy));
}

// Hermite S 形曲线（平滑插值）
float Fade(float t)
{
    //return 3*t*t - 2*t*t*t;
    return t * t * t * (t * (t * 6 - 15) + 10);
}

// Perlin 噪声计算
float PerlinNoise(float x, float y)
{
    int x0 = (int) floor(x);
    int y0 = (int) floor(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float sx = Fade(x - x0);
    float sy = Fade(y - y0);

    float n0 = GradientDot(x0, y0, x, y);
    float n1 = GradientDot(x1, y0, x, y);
    float ix0 = lerp(n0, n1, sx);

    float n2 = GradientDot(x0, y1, x, y);
    float n3 = GradientDot(x1, y1, x, y);
    float ix1 = lerp(n2, n3, sx);

    return lerp(ix0, ix1, sy) * 0.5 + 0.5; // 归一化到 [0,1]
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
    float noiseValue = PerlinNoise(uv.x * NoiseScale.x, uv.y * NoiseScale.y);

    // 将噪声值写入目标纹理
    PerlinTexture[id.xy] = float4(noiseValue, noiseValue, noiseValue, 1);
}
