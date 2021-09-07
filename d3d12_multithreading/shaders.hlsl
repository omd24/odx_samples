Texture2D smap : register(t0);
Texture2D diffuse_map : register(t1);
Texture2D nmap : register(t2);

SamplerState sample_wrap : register(s0);
SamplerState sample_clamp : register(s1);

#define NUM_LIGHTS 3
#define SHADOW_DEPTH_BIAS 0.00005f

struct LightState {
    float3 position;
    float3 direction;
    float4 color;
    float4 falloff;
    float4x4 view;
    float4x4 projection;
};
cbuffer SceneConstantBuffer : register(b0){
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4 ambient_color;
    bool sample_smap;
    LightState lights[NUM_LIGHTS];
};
struct PSInput {
    float4 position : SV_POSITION;
    float4 worldpos : POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};
//
// -- sample normal map, convert to signed, apply tangent-to-world space transform
float3 CalcPerPixelNormal(
    float2 tex_coord, float3 normal, float3 tangent
){
    // -- compute tangent space to world space matrix transform
    normal = normalize(normal);
    tangent = normalize(tangent);
    
    float3 binormal = normalize(cross(tangent, normal));
    float3x3 tangent_space_to_world_space =
        float3x3(tangent, binormal, normal);
    
    // -- compute per-pixel normal
    float3 bump_normal = (float3)nmap.Sample(sample_wrap, tex_coord);
    bump_normal = 2.0f * bump_normal - 1.0f;
    
    return mul(bump_normal, tangent_space_to_world_space);
}
//
// -- diffuse lighting calculation with angle and distance falloff
float4 CalcLightingColor(
    float3 lightpos, float3 lightdir, float4 lightcolor,
    float4 falloffs, float3 posworld, float3 per_pixel_normal
) {
    float3 light_to_pixel = posworld - lightpos;
    
    // -- dist = 0 at falloffs.x and 1 at falloffs.x - falloffs.y
    float dist = length(light_to_pixel);
    
    float dist_falloff = saturate((falloffs.x - dist) / falloffs.y);
    
    // -- normalize:
    light_to_pixel = light_to_pixel / dist;
    
    // -- angle = 0 at falloffs.z and 1 at falloffs.z - falloffs.w
    float cos_angle = dot(light_to_pixel, lightdir / length(lightdir));
    float angle_falloff = saturate((cos_angle - falloffs.z) / falloffs.w);
    
    // -- diffuse contribution:
    float normal_dot_light = saturate(-dot(light_to_pixel, per_pixel_normal));
    
    return lightcolor * normal_dot_light * dist_falloff * angle_falloff;
}
//
// -- test how much pixel is in shadow, using 2x2 percentage-closer filgering
float4 CalcUnshadowedAmountPCF2x2(int light_index, float4 posworld) {
    // -- compute pixel position in light space
    float4 lightspace_pos = posworld;
    lightspace_pos = mul(lightspace_pos, lights[light_index].view);
    lightspace_pos = mul(lightspace_pos, lights[light_index].projection);
    
    lightspace_pos.xyz /= lightspace_pos.w;
    
    // -- translate from homogenous coords to texture coords
    float2 shadow_tex_coord = 0.5f * lightspace_pos.xy + 0.5f;
    shadow_tex_coord.y = 1.0f - shadow_tex_coord.y;
    
    // -- depth bias to avoid pixel self-shadowing
    float lightspace_depth = lightspace_pos.z - SHADOW_DEPTH_BIAS;
    
    // -- find sub-pixel weights
    float2 smap_dims = float2(1280.0f, 720.0f); // needs to be kept in sync with c++ side
    float4 subpixel_coords = float4(1.0f, 1.0f, 1.0f, 1.0f);
    subpixel_coords.xy = frac(smap_dims * shadow_tex_coord);
    subpixel_coords.zw = 1.0f - subpixel_coords.xy;
    float4 bilinear_weights = subpixel_coords.zxzx * subpixel_coords.wwyy;
    
    // -- 2x2 percentage closer filtering
    float2 texel_units = 1.0f / smap_dims;
    float4 shadow_depths;
    shadow_depths.x = smap.Sample(sample_clamp, shadow_tex_coord);
    shadow_depths.y = smap.Sample(sample_clamp, shadow_tex_coord + float2(texel_units.x, 0.0f));
    shadow_depths.z = smap.Sample(sample_clamp, shadow_tex_coord + float2(0.0f, texel_units.y));
    shadow_depths.w = smap.Sample(sample_clamp, shadow_tex_coord + texel_units);
    
    // -- what weighted fraction of the 4 samples are nearer to light than this pixel
    float4 shadow_tests = (shadow_depths >= lightspace_depth) ? 1.0f : 0.0f;
    return dot(bilinear_weights, shadow_tests);
}
PSInput VSMain(
    float3 pos : POSITION, float3 normal : NORMAL,
    float2 uv : TEXCOORD0, float3 tangent : TANGENT
) {
    PSInput result;
    
    float4 newpos = float4(pos, 1.0f);
    
    normal.z *= -1.0f;
    newpos = mul(newpos, model);
    
    result.worldpos = newpos;
    
    newpos = mul(newpos, view);
    newpos = mul(newpos, projection);
    
    result.position = newpos;
    result.uv = uv;
    result.normal = normal;
    result.tangent = tangent;
    
    return result;
}
float4 PSMain(PSInput input) : SV_TARGET{
    float4 diffuse_color = diffuse_map.Sample(sample_wrap, input.uv);
    float3 pixel_normal = CalcPerPixelNormal(input.uv, input.normal, input.tangent);
    float4 total_light = ambient_color;
    
    for(int i = 0;i < NUM_LIGHTS;++i) {
        float4 light_pass = CalcLightingColor(
            lights[i].position, lights[i].direction, lights[i].color,
            lights[i].falloff, input.worldpos.xyz, pixel_normal
        );
        if (sample_smap && i == 0)
            light_pass *= CalcUnshadowedAmountPCF2x2(i, input.worldpos);
        total_light += light_pass;
    }
    return diffuse_color * saturate(total_light);
}
