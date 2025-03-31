RWTexture2D<float4> PerlinTexture : register(u0);
cbuffer CB : register(b0)
{
    float2 NoiseScale;
    float time;
}

float2 N22(float2 p)
{
    float3 a = frac(p.xyx * float3(123.34, 234.34, 345.65));
    a += dot(a, a + 34.45);
    return frac(float2(a.x * a.y, a.y * a.z));
}



[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 dims;
    PerlinTexture.GetDimensions(dims.x, dims.y);

    float2 uv = float2(id.xy) / float2(dims);
    uv = uv * 2 - 1;

    float minDist = 100;
    int cellIndex = 0;
    float4 color = 0;
    if(false)
    {
        for (int i = 0; i < 30; i++)
        {
            float2 n = N22(float2(i, i));
            float2 p = n + sin(time + i * 0.5) * 0.5;
            float d = length(uv - p);

            if (d < minDist)
            {
                minDist = d;
                cellIndex = i;
            }
        }
    }
    else
    {
        uv *= 3;
        
        float2 gv = frac(uv) - 0.5;
        float2 id = floor(uv);
        
        for (int y = -1; y <= 1; y++)
        {
            for (int x = -1; x <= 1; x++)
            {
                float2 offset = float2(x, y);
                
                float2 n = N22(id + offset);
                float2 p = offset + sin(n * time) * 0.5;
                float d = length(gv - p);

                if (d < minDist)
                {
                    minDist = d;
                    //cellIndex = i;
                }
                
            }

        }
        color = minDist;

    }

    

    PerlinTexture[id.xy] = color;
    }
