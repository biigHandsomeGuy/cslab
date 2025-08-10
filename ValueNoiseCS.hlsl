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

static const uint THREADS_X = 16;
static const uint THREADS_Y = 16;
groupshared float g_GridCache[(THREADS_Y + 1) * (THREADS_X + 1)]; // index = x + y * (THREADS_X + 1)

// noiseCoord = uv * NoiseScale * frequency
// grid cell size = 1
// grid index = floor(noiseCoord)

#if super
[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    int2 dims;
    PerlinTexture.GetDimensions(dims.x, dims.y);

    float2 uv = float2(DTid.xy) / float2(dims);
    
    // group origin in pixel coords
    int2 groupPixelOrigin = int2(Gid.xy) * int2(THREADS_X, THREADS_Y);
    // group local pixel index
    int2 localPixel = GTid.xy;
    
    float2 uv_group_min = float2(groupPixelOrigin) / float2(dims);
    float2 uv_group_max = float2(groupPixelOrigin + int2(THREADS_X - 1, THREADS_Y - 1)) / float2(dims);
    
    float3 color = 0;
    
    float frequency = 1;
    float amplitude = 1;
    float stacking = 0;
    for (int octave = 0; octave < Octaves; octave++)
    {
        // compute scaled uv bounds in noise space for this octave
        float2 scaled_min = uv_group_min * (NoiseScale * frequency);
        float2 scaled_max = uv_group_max * (NoiseScale * frequency);
        
        // compute needed integer grid range: floor(min) .. floor(max) plus +1 for top/right for interpolation
        int2 grid_min = int2(floor(scaled_min));
        int2 grid_max = int2(floor(scaled_max)) + int2(1, 1); // inclusive max corner index
        
        float2 group_tl_noise = uv_group_min * (NoiseScale * frequency);
        int2 base = int2(floor(group_tl_noise));
        
        //  cached [gx, gy] = base + int2(gx, gy)
        uint cacheWidth = THREADS_X + 1;
        uint cacheHeight = THREADS_Y + 1;
        uint cacheSize = cacheWidth * cacheHeight;
        
        uint tidLinear = GTid.y * THREADS_X + GTid.x;
        uint entriesPerThread = (cacheSize + THREADS_X * THREADS_Y - 1) / (THREADS_X * THREADS_Y);
        uint startEntry = tidLinear * entriesPerThread;
        uint endEntry = min(startEntry + entriesPerThread, cacheSize);
        
        for (uint e = startEntry; e < endEntry; ++e)
        {
            uint gx = e % cacheWidth;
            uint gy = e / cacheWidth;
            int2 gridCoord = base + int2(gx, gy);

            // compute hash/random for that grid point
            float v = rand2dTo1d(gridCoord);

            // store to shared
            g_GridCache[gy * cacheWidth + gx] = v;
        }
        
        GroupMemoryBarrierWithGroupSync();
        
        float2 noise_uv = uv * (NoiseScale * frequency);

        float2 local = noise_uv - float2(base);
        
        uint gx0 = (uint) floor(local.x);
        uint gy0 = (uint) floor(local.y);
        
        gx0 = (gx0 <= THREADS_X) ? gx0 : THREADS_X;
        gy0 = (gy0 <= THREADS_Y) ? gy0 : THREADS_Y;

        uint gx1 = min(gx0 + 1, cacheWidth - 1);
        uint gy1 = min(gy0 + 1, cacheHeight - 1);

        float2 f = float2(local.x - gx0, local.y - gy0);
        float2 u = Fade(f);

        // lookup cached corner values
        float top_left = g_GridCache[gy0 * cacheWidth + gx0];
        float top_right = g_GridCache[gy0 * cacheWidth + gx1];
        float bottom_left = g_GridCache[gy1 * cacheWidth + gx0];
        float bottom_right = g_GridCache[gy1 * cacheWidth + gx1];

        float top = lerp(top_left, top_right, u.x);
        float bottom = lerp(bottom_left, bottom_right, u.x);
        float val = lerp(top, bottom, u.y);
        
        stacking += amplitude;
        color += amplitude * val;

        // octave step
        frequency *= 1.8;
        amplitude *= 0.7;
        GroupMemoryBarrierWithGroupSync();
    }
    
    color /= stacking;
    
    
    PerlinTexture[DTid.xy] = float4(color, 1);
}

#else
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
        frequency *= 1.8;
        
        amplitude *= 0.7;

    }
    
    color /= stacking;
  
    PerlinTexture[id.xy] = float4(color, 1);
}


#endif