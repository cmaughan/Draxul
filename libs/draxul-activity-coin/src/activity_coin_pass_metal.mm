#import <draxul/activity_coin_pass.h>

#import <draxul/metal/metal_render_context.h>

#include <draxul/log.h>

namespace draxul
{
namespace
{

// Metal twin of shaders/activity_coin.vert and activity_coin.frag (ported
// from TokenFu). Compiled at runtime like the NanoVG Metal backend.
const char* kActivityCoinMetalSource = R"metal(
#include <metal_stdlib>
using namespace metal;

struct CoinUniforms {
    float4 viewport_radius;   // {viewport width, viewport height, radius, brightness}
    float4 coin;              // {x, y, angle, style}
};

struct CoinVertexOut {
    float4 position [[position]];
    float3 normal;
    float2 uv;
    float face;
    int style [[flat]];
    float brightness;
};

constant int kSegments = 32;
constant float kHalfThickness = 0.14;
constant float kTau = 6.28318530718;

static float3 rotate_y(float3 v, float c, float s)
{
    return float3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

vertex CoinVertexOut activity_coin_vertex(
    constant CoinUniforms& u [[buffer(0)]],
    uint vertex_id [[vertex_id]])
{
    int segment = int(vertex_id) / 12;
    int corner = int(vertex_id) % 12;
    float a0 = kTau * float(segment) / float(kSegments);
    float a1 = kTau * float(segment + 1) / float(kSegments);
    float2 p0 = float2(cos(a0), sin(a0));
    float2 p1 = float2(cos(a1), sin(a1));

    CoinVertexOut out;
    float3 position;
    float3 n;
    float3 facet;
    if (corner < 3) {
        float2 p = corner == 0 ? float2(0.0) : corner == 1 ? p0 : p1;
        position = float3(p, kHalfThickness);
        n = float3(0.0, 0.0, 1.0);
        facet = n;
        out.uv = 0.5 + p * 0.5;
        out.face = 1.0;
    } else if (corner < 6) {
        float2 p = corner == 3 ? float2(0.0) : corner == 4 ? p1 : p0;
        position = float3(p, -kHalfThickness);
        n = float3(0.0, 0.0, -1.0);
        facet = n;
        out.uv = 0.5 + p * 0.5;
        out.face = -1.0;
    } else {
        int r = corner - 6;
        bool second = r == 2 || r == 4 || r == 5;
        bool front = r == 0 || r == 3 || r == 5;
        float2 p = second ? p1 : p0;
        position = float3(p, front ? kHalfThickness : -kHalfThickness);
        n = float3(p, 0.0);
        float mid = 0.5 * (a0 + a1);
        facet = float3(cos(mid), sin(mid), 0.0);
        out.uv = float2(float(second ? segment + 1 : segment) / float(kSegments),
            front ? 1.0 : 0.0);
        out.face = 0.0;
    }

    float c = cos(u.coin.z);
    float s = sin(u.coin.z);
    position = rotate_y(position, c, s);
    out.normal = normalize(rotate_y(n, c, s));
    out.style = int(u.coin.w);
    out.brightness = u.viewport_radius.w;

    if (rotate_y(facet, c, s).z <= 0.0) {
        out.position = float4(2.0, 2.0, 2.0, 1.0);
        return out;
    }

    float2 pixel = u.coin.xy + position.xy * u.viewport_radius.z;
    out.position = float4(
        2.0 * pixel.x / u.viewport_radius.x - 1.0,
        1.0 - 2.0 * pixel.y / u.viewport_radius.y,
        0.5,
        1.0);
    return out;
}

static float segment_distance(float2 point, float2 a, float2 b)
{
    float2 pa = point - a;
    float2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

static float claude_mark(float2 point)
{
    float distance_to_mark = 10.0;
    for (int ray = 0; ray < 4; ++ray) {
        float angle = 0.78539816339 * float(ray);
        float2 direction = float2(cos(angle), sin(angle));
        distance_to_mark = min(distance_to_mark,
            segment_distance(point, -direction * 0.55, direction * 0.55));
    }
    return 1.0 - smoothstep(0.085, 0.14, distance_to_mark);
}

static float codex_mark(float2 point)
{
    float angle = atan2(point.y, point.x);
    float radius = length(point);
    float loop_radius = 0.47 + 0.075 * cos(angle * 6.0);
    float ring = 1.0 - smoothstep(0.07, 0.13, abs(radius - loop_radius));
    float hub = 1.0 - smoothstep(0.17, 0.23, radius);
    return max(ring, hub * 0.72);
}

static float grok_mark(float2 point)
{
    float radius = length(point);
    float ring = 1.0 - smoothstep(0.075, 0.13, abs(radius - 0.48));
    float slash_distance = segment_distance(
        point, float2(-0.64, 0.55), float2(0.64, -0.55));
    float slash = 1.0 - smoothstep(0.075, 0.13, slash_distance);
    return max(ring, slash);
}

static float neutral_mark(float2 point)
{
    float radius = length(point);
    float ring = 1.0 - smoothstep(0.075, 0.13, abs(radius - 0.48));
    float hub = 1.0 - smoothstep(0.14, 0.2, radius);
    return max(ring, hub);
}

fragment float4 activity_coin_fragment(CoinVertexOut in [[stage_in]])
{
    float3 n = normalize(in.normal);
    float3 light = normalize(float3(-0.45, -0.65, 1.0));
    float3 view_direction = float3(0.0, 0.0, 1.0);
    float diffuse = 0.34 + 0.58 * max(dot(n, light), 0.0);
    float specular = pow(max(dot(reflect(-light, n), view_direction), 0.0), 28.0) * 0.42;
    float rim = pow(1.0 - abs(dot(n, view_direction)), 2.5) * 0.18;

    float3 base = in.style == 0 ? float3(0.22, 0.50, 0.88)
        : in.style == 1 ? float3(1.0, 0.59, 0.43)
        : in.style == 2 ? float3(0.48, 0.52, 0.60) : float3(0.62, 0.64, 0.68);
    if (abs(in.face) < 0.5) {
        float grooves = 0.91 + 0.09 * sin(in.uv.x * 150.796447372);
        base *= 0.68 * grooves;
    }
    float3 color = base * diffuse + float3(specular + rim);

    if (abs(in.face) > 0.5) {
        float2 point = (in.uv - 0.5) * 2.0;
        float mark = in.style == 0 ? codex_mark(point)
            : in.style == 1 ? claude_mark(point)
            : in.style == 2 ? grok_mark(point) : neutral_mark(point);
        if (in.style == 0)
            color = mix(color, float3(1.0), mark);
        else if (in.style == 1)
            color = mix(color, float3(0.16, 0.075, 0.045) * (0.82 + specular), mark * 0.9);
        else if (in.style == 2)
            color = mix(color, float3(0.96, 0.97, 1.0), mark);
        else
            color = mix(color, float3(0.20, 0.21, 0.24), mark * 0.85);
    }
    return float4(color * in.brightness, 1.0);
}
)metal";

constexpr NSUInteger kCoinVertexCount = 32 * 12;

struct CoinUniforms
{
    float viewport_radius[4];
    float coin[4];
};

class MetalActivityCoinPass final : public IActivityCoinPass
{
public:
    void set_coins(std::vector<ActivityCoinInstance> coins) override
    {
        coins_ = std::move(coins);
    }

    void record(IRenderContext& ctx) override
    {
        if (coins_.empty())
            return;
        auto& mtl_ctx = static_cast<MetalRenderContext&>(ctx);
        id<MTLRenderCommandEncoder> encoder = mtl_ctx.encoder();
        const int width = mtl_ctx.viewport_w();
        const int height = mtl_ctx.viewport_h();
        if (!encoder || width <= 0 || height <= 0 || !ensure_pipeline(mtl_ctx.device()))
        {
            coins_.clear();
            return;
        }

        [encoder setRenderPipelineState:pipeline_];
        [encoder setCullMode:MTLCullModeNone];
        for (const ActivityCoinInstance& coin : coins_)
        {
            const CoinUniforms uniforms{
                { static_cast<float>(width), static_cast<float>(height), coin.radius,
                    coin.brightness },
                { coin.center_x, coin.center_y, coin.angle,
                    static_cast<float>(static_cast<int>(coin.style)) },
            };
            [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:kCoinVertexCount];
        }
        coins_.clear();
    }

private:
    bool ensure_pipeline(id<MTLDevice> device)
    {
        if (!device)
            return false;
        if (pipeline_ && device_ == device)
            return true;
        if (failed_ && device_ == device)
            return false;
        device_ = device;
        pipeline_ = nil;
        failed_ = true;

        NSError* error = nil;
        NSString* source = [NSString stringWithUTF8String:kActivityCoinMetalSource];
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Activity coin shader compilation failed: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        descriptor.label = @"Activity coin";
        descriptor.vertexFunction = [library newFunctionWithName:@"activity_coin_vertex"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"activity_coin_fragment"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pipeline_ = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!pipeline_)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Activity coin pipeline creation failed: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        failed_ = false;
        return true;
    }

    std::vector<ActivityCoinInstance> coins_;
    id<MTLDevice> device_ = nil;
    id<MTLRenderPipelineState> pipeline_ = nil;
    bool failed_ = false;
};

} // namespace

std::unique_ptr<IActivityCoinPass> create_activity_coin_pass()
{
    return std::make_unique<MetalActivityCoinPass>();
}

} // namespace draxul
