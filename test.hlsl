Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSInput
{
    float4 position : POSITION;
    float2 tex : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

PSInput VSMain(VSInput input)
{
    PSInput result;

    result.position = input.position;

    result.texcoord = input.tex;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 color = gTexture.Sample(gSampler, input.texcoord);
    
    return color;
}
