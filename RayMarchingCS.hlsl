RWTexture2D<float4> OutputTexture : register(u0);
cbuffer CB : register(b0)
{
    float2 NoiseScale;
    float time;
}

#define MAX_STEPS 100
#define MAX_DISTANCE 100
#define SURFACE_DISTANCE 0.01

float2x2 Rotation(float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    
    return float2x2(c, -s, s, c);

}

float Gyroid(float3 p)
{
    p.yz = mul(Rotation(time * 0.1), p.yz);
    p *= 10;
    return abs(0.7 * dot(sin(p), cos(p.zxy)) / 10) - 0.03;
}

float sdBox(float3 p, float3 b)
{
    float3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}
float sdSphere(float3 p, float s)
{
    return length(p) - s;
}

float softMin(float a, float b, float v)
{
    float k = clamp(0.5 + 0.5 * (a - b) / v, 0.0, 1.0);
    return lerp(a, b, k) - v * k * (1.0 - k);
}

float smin(float a, float b, float k)
{
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.1, 1.0);
    return lerp(b, a, h) - k * h * (1.0 - h);
}

float GetDistance(float3 p)
{
    float sphere_distance = sdSphere(p, 1);
    sphere_distance = abs(sphere_distance) - 0.03;
    float g = Gyroid(p);
    sphere_distance = smin(sphere_distance, g, -0.04);
    
    
    float plane_distance = p.y + 1;
    p.x -= time * 0.1;
    p *= 5.0;
    p.y += sin(p.z) * 0.5;
    float y = abs(dot(sin(p), cos(p.yzx))) * 0.1;
    plane_distance += y;
    
    return min(sphere_distance, plane_distance*0.9);
}
float3 GetNormal(float3 p)
{
    float d = GetDistance(p);
    float2 e = { 0.0001, 0 };
    
    return normalize(float3(d - GetDistance(p - e.xyy),
                            d - GetDistance(p - e.yxy),
                            d - GetDistance(p - e.yyx)));

}

float RayMarch(float3 ray_origin, float3 ray_direction)
{
    float distance = 0;
    
    for (int i = 0; i < MAX_STEPS; i++)
    {
        float3 p = ray_origin + ray_direction * distance;
        float ds = GetDistance(p);
        distance += ds;
        if(distance > MAX_DISTANCE || distance < SURFACE_DISTANCE)
            break;
    }
    return distance;
}



// Compute Shader Ö÷º¯Êý
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 dims;
    OutputTexture.GetDimensions(dims.x, dims.y);
    float2 uv = float2(id.xy) / float2(dims);
    uv.y = 1 - uv.y;
    // uv range[-1, 1]
    uv = uv * 2 - 1;
    
    
    float3 color = 0;
    
    // camera position
    float3 ray_origin = float3(0, 0, -2);
    float3 ray_direction = normalize(float3(uv.x, uv.y, 1));
    
    float d = RayMarch(ray_origin, ray_direction);
    
    float3 shading_point = ray_origin + ray_direction * d;
    
    float3 light_pos = { 0, 0, 0 };
    // light_pos.xz += float2(sin(time), cos(time));
    float3 light_direction = normalize(light_pos - shading_point);
    float3 normal = GetNormal(shading_point);
    
    float diffuse_color = dot(light_direction, normal) * 0.5 + 0.5;
    
    float center_distance = length(shading_point - float3(0, 0, 0));
   
    
    color = diffuse_color;
    if (center_distance > 1.03)
    {
        float s = Gyroid(-light_direction);
        float w = center_distance * 0.02;
        float shadow = smoothstep(-w, w, s);
        color *= shadow;
        
        color /= center_distance * center_distance;

    }
    else
    {
        float sss = max(0., 1. - dot(uv, uv) * 25.);
        sss *= sss;
        float s = Gyroid(shading_point);
        sss *= smoothstep(-0.03, 0, s);
        
        color += sss * float3(1, 0.1, 0.1);

    }
    
    center_distance = dot(uv, uv);
    
    float light = 0.003 / center_distance;
    color += light * smoothstep(0, 1, d-1);
    color = pow(color, 0.4545);
    
    OutputTexture[id.xy] = float4(color, 1);
}
