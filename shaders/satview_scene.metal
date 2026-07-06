#include <metal_stdlib>
using namespace metal;

struct SatViewFrameUniforms
{
    float4x4 view_proj;
    float4 camera_pos;
    float4 camera_orientation;
    float4 sun_dir_time;
    float4 render_params;
};

struct SatViewVertexOut
{
    float4 position [[position]];
    float3 normal;
    float3 world;
    float2 uv;
};

struct SatViewOrbitOut
{
    float4 position [[position]];
    float4 color;
};

struct SatViewSceneVertex
{
    float4 position;
    float4 color;
    float4 paired_position;
};

struct SatViewMarkerInstance
{
    float4 position0_size;
    float4 position1_selected;
    float4 color;
    float4 style;
};

struct SatViewStarInstance
{
    float4 direction_magnitude;
    float4 color_size;
};

struct SatViewStarOut
{
    float4 position [[position]];
    float4 color;
    float2 uv;
};

struct SatViewLabelInstance
{
    float4 direction_priority;
    float4 uv_rect;
    float4 pixel_size_offset;
    float4 color;
};

struct SatViewLabelOut
{
    float4 position [[position]];
    float4 color;
    float2 uv;
};

struct SatViewLandscapeTriangleInstance
{
    float4 local_direction0;
    float4 local_direction1;
    float4 local_direction2;
    float4 color;
};

struct SatViewLandscapeLineInstance
{
    float4 local_direction0;
    float4 local_direction1;
    float4 color;
};

struct SatViewCelestialLineInstance
{
    float4 start_direction_width;
    float4 end_direction_dash;
    float4 color;
    float4 style;
};

struct SatViewCelestialLineOut
{
    float4 position [[position]];
    float4 color;
    float line_distance [[center_no_perspective]];
    float line_across [[center_no_perspective]];
    float2 dash_gap [[flat]];
};

constant float kPi = 3.14159265358979323846f;
constant float kStarDistanceEarthRadii = 48.0f;
constant float3 kLunarNorthPoleRender = float3(
    0.39812155f,
    0.91733267f,
    0.00003544f);

static float3 rotate_vector_by_quaternion(float3 value, float4 quaternion)
{
    float3 twice_cross = 2.0f * cross(quaternion.xyz, value);
    return value + quaternion.w * twice_cross + cross(quaternion.xyz, twice_cross);
}

static bool is_ground_projection(constant SatViewFrameUniforms& frame)
{
    return frame.camera_pos.w >= 0.5f;
}

static bool is_stereographic_ground_projection(constant SatViewFrameUniforms& frame)
{
    return frame.camera_pos.w >= 2.5f;
}

static bool ground_horizon_occlusion_enabled(constant SatViewFrameUniforms& frame)
{
    int projection_code = int(floor(frame.camera_pos.w + 0.5f));
    return projection_code == 2 || projection_code == 4;
}

static float3x3 ground_world_to_camera(constant SatViewFrameUniforms& frame)
{
    return float3x3(
        frame.view_proj[0].xyz,
        frame.view_proj[1].xyz,
        frame.view_proj[2].xyz);
}

static float ground_projection_aspect_scale(constant SatViewFrameUniforms& frame)
{
    if (is_stereographic_ground_projection(frame))
        return max(abs(frame.view_proj[3][3]), 0.000001f);
    return max(abs(frame.view_proj[0][0] / frame.view_proj[1][1]), 0.000001f);
}

static bool ground_world_position_visible(
    float3 world_position,
    constant SatViewFrameUniforms& frame)
{
    if (!is_ground_projection(frame) || !ground_horizon_occlusion_enabled(frame))
        return true;
    float3 observer_up = normalize(frame.camera_pos.xyz);
    return dot(world_position - frame.camera_pos.xyz, observer_up) > 0.0f;
}

static float4 project_world_position(
    float3 world_position,
    constant SatViewFrameUniforms& frame)
{
    if (!is_stereographic_ground_projection(frame))
        return frame.view_proj * float4(world_position, 1.0f);

    float3 offset = world_position - frame.camera_pos.xyz;
    float distance_to_camera = length(offset);
    if (distance_to_camera <= 0.000001f)
        return float4(2.0f, 2.0f, 1.0f, 1.0f);

    float3 camera_direction = ground_world_to_camera(frame) * (offset / distance_to_camera);
    float denominator = 1.0f - camera_direction.z;
    if (denominator <= 0.000001f)
        return float4(2.0f, 2.0f, 1.0f, 1.0f);

    float scale = max(abs(frame.view_proj[3][2]), 0.000001f);
    float aspect_scale = ground_projection_aspect_scale(frame);
    float2 plane = camera_direction.xy / denominator;
    float2 ndc = plane * float2(aspect_scale, 1.0f) / scale;
    float depth = distance_to_camera / (distance_to_camera + 1.0f);
    return float4(ndc, depth, 1.0f);
}

static float3 render_teme_to_ecef(float3 render_position, float sidereal_angle)
{
    float3 teme = float3(-render_position.z, -render_position.x, render_position.y);
    float c = cos(sidereal_angle);
    float s = sin(sidereal_angle);
    return float3(
        c * teme.x + s * teme.y,
        -s * teme.x + c * teme.y,
        teme.z);
}

static float3 ecef_to_render_teme(float3 ecef, float sidereal_angle)
{
    float c = cos(sidereal_angle);
    float s = sin(sidereal_angle);
    float3 teme = float3(
        c * ecef.x - s * ecef.y,
        s * ecef.x + c * ecef.y,
        ecef.z);
    return float3(-teme.y, teme.z, -teme.x);
}

static float3 ecef_to_map_local(float3 ecef, float2 center)
{
    float cos_longitude = cos(center.x);
    float sin_longitude = sin(center.x);
    float cos_latitude = cos(center.y);
    float sin_latitude = sin(center.y);
    float3 center_axis = float3(
        cos_latitude * cos_longitude,
        cos_latitude * sin_longitude,
        sin_latitude);
    float3 east_axis = float3(-sin_longitude, cos_longitude, 0.0f);
    float3 north_axis = float3(
        -sin_latitude * cos_longitude,
        -sin_latitude * sin_longitude,
        cos_latitude);
    return float3(
        dot(ecef, center_axis),
        dot(ecef, east_axis),
        dot(ecef, north_axis));
}

static float3 map_local_to_ecef(float3 local, float2 center)
{
    float cos_longitude = cos(center.x);
    float sin_longitude = sin(center.x);
    float cos_latitude = cos(center.y);
    float sin_latitude = sin(center.y);
    float3 center_axis = float3(
        cos_latitude * cos_longitude,
        cos_latitude * sin_longitude,
        sin_latitude);
    float3 east_axis = float3(-sin_longitude, cos_longitude, 0.0f);
    float3 north_axis = float3(
        -sin_latitude * cos_longitude,
        -sin_latitude * sin_longitude,
        cos_latitude);
    return local.x * center_axis + local.y * east_axis + local.z * north_axis;
}

static float3 render_to_lunar_body(
    float3 render_position,
    constant SatViewFrameUniforms& frame)
{
    float3 far_axis = normalize(frame.camera_pos.xyz);
    float3 north_axis = normalize(
        kLunarNorthPoleRender
        - far_axis * dot(kLunarNorthPoleRender, far_axis));
    float3 local_x_axis = normalize(cross(north_axis, far_axis));
    float3 moon_relative = render_position - frame.camera_pos.xyz;
    return float3(
        dot(moon_relative, -far_axis),
        dot(moon_relative, -local_x_axis),
        dot(moon_relative, north_axis));
}

static float3 render_to_solar_body(
    float3 render_position,
    constant SatViewFrameUniforms& frame)
{
    float4 render_to_body = float4(-frame.sun_dir_time.xyz, frame.sun_dir_time.w);
    return rotate_vector_by_quaternion(
        render_position - frame.camera_pos.xyz,
        render_to_body);
}

static float2 map_position_from_render_teme(
    float3 render_position,
    constant SatViewFrameUniforms& frame,
    bool earth_fixed)
{
    float3 body_position = frame.camera_orientation.z > 1.5f
        ? render_to_solar_body(render_position, frame)
        : frame.camera_orientation.z > 0.5f
        ? render_to_lunar_body(render_position, frame)
        : earth_fixed
            ? render_position
            : render_teme_to_ecef(render_position, frame.render_params.z);
    float3 local = ecef_to_map_local(body_position, frame.camera_orientation.xy);
    float radius = max(length(local), 0.000001f);
    return float2(
        atan2(local.y, local.x) / kPi,
        2.0f * asin(clamp(local.z / radius, -1.0f, 1.0f)) / kPi);
}

static float2 quad_corner(uint corner_index)
{
    switch (corner_index)
    {
    case 0:
        return float2(0.0f, 0.0f);
    case 1:
        return float2(1.0f, 1.0f);
    case 2:
        return float2(1.0f, 0.0f);
    case 3:
        return float2(0.0f, 0.0f);
    case 4:
        return float2(0.0f, 1.0f);
    default:
        return float2(1.0f, 1.0f);
    }
}

vertex SatViewVertexOut satview_earth_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    uint lat_bands = max(1u, uint(frame.render_params.x + 0.5f));
    uint lon_bands = max(1u, uint(frame.render_params.y + 0.5f));
    uint tri_vertex = vertex_id % 6u;
    uint quad = vertex_id / 6u;
    uint lon = quad % lon_bands;
    uint lat = quad / lon_bands;
    bool map_projection = frame.camera_pos.w < 0.0f;

    float2 corner = quad_corner(tri_vertex);
    float u = map_projection
        ? corner.x
        : (float(lon) + corner.x) / float(lon_bands);
    float v = map_projection
        ? corner.y
        : (float(lat) + corner.y) / float(lat_bands);
    float theta = u * 2.0f * kPi + frame.render_params.z;
    float phi = mix(-0.5f * kPi, 0.5f * kPi, v);
    float cp = cos(phi);
    float3 world = float3(cp * sin(theta), sin(phi), cp * cos(theta));

    SatViewVertexOut out;
    out.position = map_projection
        ? frame.view_proj * float4(u * 2.0f - 1.0f, v * 2.0f - 1.0f, 0.8f, 1.0f)
        : project_world_position(world, frame);
    out.normal = world;
    out.world = world;
    out.uv = float2(u, v);
    return out;
}

fragment float4 satview_earth_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    texture2d<float> earth_day_tex [[texture(0)]],
    texture2d<float> earth_night_tex [[texture(1)]],
    texture2d<float> earth_cloud_tex [[texture(2)]],
    texture2d<float> earth_live_cloud_tex [[texture(3)]],
    sampler earth_sampler [[sampler(0)]])
{
    bool map_projection = frame.camera_pos.w < 0.0f;
    float3 n = normalize(in.normal);
    float2 uv = float2(fract(in.uv.x), 1.0f - clamp(in.uv.y, 0.0f, 1.0f));
    if (map_projection)
    {
        // Quad corner normals collapse to the poles, so recover the sphere per fragment.
        float map_longitude = (in.uv.x - 0.5f) * 2.0f * kPi;
        float phi = mix(-0.5f * kPi, 0.5f * kPi, in.uv.y);
        float cp = cos(phi);
        float3 map_local = float3(
            cp * cos(map_longitude),
            cp * sin(map_longitude),
            sin(phi));
        float3 ecef = map_local_to_ecef(map_local, frame.camera_orientation.xy);
        n = normalize(ecef_to_render_teme(ecef, frame.render_params.z));
        float earth_longitude = atan2(ecef.y, ecef.x);
        float earth_latitude = asin(clamp(ecef.z, -1.0f, 1.0f));
        uv = float2(
            fract(earth_longitude / (2.0f * kPi) + 0.5f),
            0.5f - earth_latitude / kPi);
    }
    float3 light = normalize(frame.sun_dir_time.xyz);
    float3 view = map_projection ? n : normalize(frame.camera_pos.xyz - in.world);

    float3 day_surface = earth_day_tex.sample(earth_sampler, uv).rgb;
    float3 night_surface = earth_night_tex.sample(earth_sampler, uv).rgb;
    float ndl = dot(n, light);
    float day = smoothstep(-0.08f, 0.14f, ndl);
    float diffuse = max(ndl, 0.0f);
    float3 lit = day_surface * (0.22f + diffuse * 1.08f);
    float ocean_hint = smoothstep(0.03f, 0.24f, day_surface.b - max(day_surface.r, day_surface.g));
    float specular = pow(max(dot(reflect(-light, n), view), 0.0f), 48.0f)
        * ocean_hint * smoothstep(0.0f, 0.25f, ndl);
    lit += float3(0.55f, 0.72f, 0.90f) * specular * 0.30f;

    float3 night = night_surface * 1.85f + day_surface * 0.015f;
    float3 color = mix(night, lit, day);
    if (map_projection && frame.sun_dir_time.w > 0.5f)
    {
        float3 bundled_cloud_sample = earth_cloud_tex.sample(earth_sampler, uv).rgb;
        float3 live_cloud_sample = earth_live_cloud_tex.sample(earth_sampler, uv).rgb;
        float3 cloud_sample = mix(
            bundled_cloud_sample,
            live_cloud_sample,
            clamp(frame.sun_dir_time.w - 1.0f, 0.0f, 1.0f));
        float opacity = smoothstep(
            0.20f, 0.78f, dot(cloud_sample, float3(0.299f, 0.587f, 0.114f)));
        float daylight = smoothstep(-0.10f, 0.16f, ndl);
        float3 day_cloud = float3(0.91f, 0.95f, 1.0f) * (0.34f + diffuse * 0.76f);
        float3 night_cloud = float3(0.025f, 0.030f, 0.042f);
        float3 cloud_color = mix(night_cloud, day_cloud, daylight);
        float cloud_alpha = opacity * mix(0.32f, 0.78f, daylight);
        color = mix(color, cloud_color, cloud_alpha);
    }
    return float4(color, 1.0f);
}

vertex SatViewVertexOut satview_moon_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    bool map_projection = frame.camera_pos.w < 0.0f;
    uint lat_bands = max(1u, uint(frame.render_params.x + 0.5f));
    uint lon_bands = max(1u, uint(frame.render_params.y + 0.5f));
    uint tri_vertex = vertex_id % 6u;
    uint quad = vertex_id / 6u;
    uint lon = quad % lon_bands;
    uint lat = quad / lon_bands;

    float2 corner = quad_corner(tri_vertex);
    float u = map_projection
        ? corner.x
        : (float(lon) + corner.x) / float(lon_bands);
    float v = map_projection
        ? corner.y
        : (float(lat) + corner.y) / float(lat_bands);
    float theta = u * 2.0f * kPi;
    float phi = mix(-0.5f * kPi, 0.5f * kPi, v);
    float cp = cos(phi);
    float3 local_normal = float3(cp * sin(theta), sin(phi), cp * cos(theta));

    float3 moon_position = map_projection
        ? frame.camera_pos.xyz
        : frame.camera_orientation.xyz;
    float3 far_axis = normalize(moon_position);
    float3 north_axis = normalize(
        kLunarNorthPoleRender
        - far_axis * dot(kLunarNorthPoleRender, far_axis));
    float3 local_x_axis = normalize(cross(north_axis, far_axis));
    float3 normal = normalize(
        local_x_axis * local_normal.x
        + north_axis * local_normal.y
        + far_axis * local_normal.z);
    float3 world = moon_position + normal * frame.camera_orientation.w;

    SatViewVertexOut out;
    out.position = map_projection
        ? frame.view_proj * float4(u * 2.0f - 1.0f, v * 2.0f - 1.0f, 0.8f, 1.0f)
        : project_world_position(world, frame);
    out.normal = normal;
    out.world = world;
    out.uv = float2(u, v);
    return out;
}

fragment float4 satview_moon_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    texture2d<float> moon_tex [[texture(4)]],
    sampler moon_sampler [[sampler(0)]])
{
    bool map_projection = frame.camera_pos.w < 0.0f;
    float2 uv = float2(fract(in.uv.x), 1.0f - clamp(in.uv.y, 0.0f, 1.0f));
    float3 normal = normalize(in.normal);
    float3 moon_position = map_projection
        ? frame.camera_pos.xyz
        : frame.camera_orientation.xyz;
    if (map_projection)
    {
        float map_longitude = (in.uv.x - 0.5f) * 2.0f * kPi;
        float map_latitude = mix(-0.5f * kPi, 0.5f * kPi, in.uv.y);
        float cp = cos(map_latitude);
        float3 map_local = float3(
            cp * cos(map_longitude),
            cp * sin(map_longitude),
            sin(map_latitude));
        float3 body = map_local_to_ecef(map_local, frame.camera_orientation.xy);
        float3 far_axis = normalize(moon_position);
        float3 north_axis = normalize(
            kLunarNorthPoleRender
            - far_axis * dot(kLunarNorthPoleRender, far_axis));
        float3 local_x_axis = normalize(cross(north_axis, far_axis));
        normal = normalize(
            -far_axis * body.x
            - local_x_axis * body.y
            + north_axis * body.z);
        float longitude = atan2(body.y, body.x);
        float latitude = asin(clamp(body.z, -1.0f, 1.0f));
        uv = float2(
            fract(longitude / (2.0f * kPi) + 0.5f),
            0.5f - latitude / kPi);
    }
    float3 surface = moon_tex.sample(moon_sampler, uv).rgb;
    float3 sunlight_direction = normalize(frame.sun_dir_time.xyz);
    float3 earth_direction = -normalize(moon_position);
    float diffuse = max(dot(normal, sunlight_direction), 0.0f);
    float earth_phase = 0.5f * (1.0f - dot(earth_direction, sunlight_direction));
    float earthshine = max(dot(normal, earth_direction), 0.0f) * earth_phase * 0.055f;
    float illumination = 0.006f + diffuse * 1.12f + earthshine;
    return float4(surface * illumination, 1.0f);
}

vertex SatViewVertexOut satview_sun_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    bool map_projection = frame.camera_pos.w < 0.0f;
    uint lat_bands = max(1u, uint(frame.render_params.x + 0.5f));
    uint lon_bands = max(1u, uint(frame.render_params.y + 0.5f));
    uint tri_vertex = vertex_id % 6u;
    uint quad = vertex_id / 6u;
    uint lon = quad % lon_bands;
    uint lat = quad / lon_bands;

    float2 corner = quad_corner(tri_vertex);
    float u = map_projection
        ? corner.x
        : (float(lon) + corner.x) / float(lon_bands);
    float v = map_projection
        ? corner.y
        : (float(lat) + corner.y) / float(lat_bands);
    float longitude = (u - 0.5f) * 2.0f * kPi;
    float latitude = mix(-0.5f * kPi, 0.5f * kPi, v);
    float cp = cos(latitude);
    float3 body_normal = float3(
        cp * cos(longitude),
        cp * sin(longitude),
        sin(latitude));
    float3 normal = normalize(rotate_vector_by_quaternion(body_normal, frame.sun_dir_time));
    float3 sun_position = map_projection
        ? frame.camera_pos.xyz
        : frame.camera_orientation.xyz;
    float3 world = sun_position + normal * frame.camera_orientation.w;

    SatViewVertexOut out;
    out.position = map_projection
        ? frame.view_proj * float4(u * 2.0f - 1.0f, v * 2.0f - 1.0f, 0.8f, 1.0f)
        : project_world_position(world, frame);
    out.normal = normal;
    out.world = world;
    out.uv = float2(u, v);
    return out;
}

fragment float4 satview_sun_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    texture2d<float> sun_tex [[texture(5)]],
    sampler sun_sampler [[sampler(0)]])
{
    bool map_projection = frame.camera_pos.w < 0.0f;
    float2 uv = float2(fract(in.uv.x), 1.0f - clamp(in.uv.y, 0.0f, 1.0f));
    if (map_projection)
    {
        float map_longitude = (in.uv.x - 0.5f) * 2.0f * kPi;
        float map_latitude = mix(-0.5f * kPi, 0.5f * kPi, in.uv.y);
        float cp = cos(map_latitude);
        float3 body = map_local_to_ecef(
            float3(cp * cos(map_longitude), cp * sin(map_longitude), sin(map_latitude)),
            frame.camera_orientation.xy);
        float longitude = atan2(body.y, body.x);
        float latitude = asin(clamp(body.z, -1.0f, 1.0f));
        uv = float2(
            fract(longitude / (2.0f * kPi) + 0.5f),
            0.5f - latitude / kPi);
    }

    float3 surface = sun_tex.sample(sun_sampler, uv).rgb;
    float3 emission = surface * float3(1.16f, 0.94f, 0.72f)
        * (map_projection ? 0.65f : 2.15f);
    if (!map_projection)
    {
        float3 view_direction = normalize(frame.camera_pos.xyz - in.world);
        float mu = max(dot(normalize(in.normal), view_direction), 0.0f);
        float limb_darkening = mix(0.48f, 1.0f, pow(mu, 0.42f));
        float limb_glow = pow(1.0f - mu, 3.0f);
        emission = emission * limb_darkening
            + float3(1.25f, 0.30f, 0.035f) * limb_glow * 0.55f;
    }
    return float4(emission, 1.0f);
}

constant float kCloudRadius = 1.0015f;

vertex SatViewVertexOut satview_cloud_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    bool map_projection = frame.camera_pos.w < 0.0f;
    uint lat_bands = max(1u, uint(frame.render_params.x + 0.5f));
    uint lon_bands = max(1u, uint(frame.render_params.y + 0.5f));
    uint tri_vertex = vertex_id % 6u;
    uint quad = vertex_id / 6u;
    uint lon = quad % lon_bands;
    uint lat = quad / lon_bands;

    float2 corner = quad_corner(tri_vertex);
    float u = map_projection
        ? corner.x
        : (float(lon) + corner.x) / float(lon_bands);
    float v = map_projection
        ? corner.y
        : (float(lat) + corner.y) / float(lat_bands);
    float theta = u * 2.0f * kPi + frame.render_params.z;
    float phi = mix(-0.5f * kPi, 0.5f * kPi, v);
    float cp = cos(phi);
    float3 normal = float3(cp * sin(theta), sin(phi), cp * cos(theta));
    float3 world = normal * kCloudRadius;

    SatViewVertexOut out;
    out.position = map_projection
        ? frame.view_proj * float4(u * 2.0f - 1.0f, v * 2.0f - 1.0f, 0.8f, 1.0f)
        : frame.view_proj * float4(world, 1.0f);
    out.normal = normal;
    out.world = world;
    out.uv = float2(u, v);
    return out;
}

static float2 cloud_ray_sphere(float3 origin, float3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0f)
        return float2(-1.0f);
    float root = sqrt(discriminant);
    return float2(-b - root, -b + root);
}

fragment float4 satview_cloud_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    texture2d<float> earth_cloud_tex [[texture(2)]],
    texture2d<float> earth_live_cloud_tex [[texture(3)]],
    sampler earth_sampler [[sampler(0)]])
{
    float cloud_mode = frame.sun_dir_time.w;
    if (cloud_mode < 0.5f)
        discard_fragment();

    float3 ray_origin = frame.camera_pos.xyz;
    float3 ray_direction = normalize(in.world - ray_origin);
    float2 shell_hit = cloud_ray_sphere(ray_origin, ray_direction, kCloudRadius);
    float fragment_distance = length(in.world - ray_origin);
    if (fragment_distance > 0.5f * (shell_hit.x + shell_hit.y))
        discard_fragment();

    float2 uv = float2(fract(in.uv.x), 1.0f - clamp(in.uv.y, 0.0f, 1.0f));
    float3 bundled_cloud_sample = earth_cloud_tex.sample(earth_sampler, uv).rgb;
    float3 live_cloud_sample = earth_live_cloud_tex.sample(earth_sampler, uv).rgb;
    float3 cloud_sample = mix(
        bundled_cloud_sample, live_cloud_sample, clamp(cloud_mode - 1.0f, 0.0f, 1.0f));
    float opacity = smoothstep(
        0.20f, 0.78f, dot(cloud_sample, float3(0.299f, 0.587f, 0.114f)));
    if (opacity < 0.002f)
        discard_fragment();

    float3 normal = normalize(in.normal);
    float3 light = normalize(frame.sun_dir_time.xyz);
    float ndl = dot(normal, light);
    float daylight = smoothstep(-0.10f, 0.16f, ndl);
    float diffuse = max(ndl, 0.0f);
    float3 day_color = float3(0.91f, 0.95f, 1.0f) * (0.34f + diffuse * 0.76f);
    float3 night_color = float3(0.025f, 0.030f, 0.042f);
    float3 cloud_color = mix(night_color, day_color, daylight);
    float alpha = opacity * mix(0.32f, 0.78f, daylight);
    return float4(cloud_color * alpha, alpha);
}

constant float kAtmosphereRadius = 1.02f;
constant float kRayleighScaleHeight = 0.00125f;
constant float kMieScaleHeight = 0.00019f;
constant float3 kRayleighBeta = float3(36.0f, 84.0f, 205.0f);
constant float3 kMieBeta = float3(22.0f, 20.0f, 18.0f);
constant float kMieG = 0.76f;
constant int kAtmosphereViewSteps = 12;
constant int kAtmosphereLightSteps = 6;

vertex SatViewVertexOut satview_atmosphere_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    uint lat_bands = max(1u, uint(frame.render_params.x + 0.5f));
    uint lon_bands = max(1u, uint(frame.render_params.y + 0.5f));
    uint tri_vertex = vertex_id % 6u;
    uint quad = vertex_id / 6u;
    uint lon = quad % lon_bands;
    uint lat = quad / lon_bands;

    float2 corner = quad_corner(tri_vertex);
    float u = (float(lon) + corner.x) / float(lon_bands);
    float v = (float(lat) + corner.y) / float(lat_bands);
    float theta = u * 2.0f * kPi + frame.render_params.z;
    float phi = mix(-0.5f * kPi, 0.5f * kPi, v);
    float cp = cos(phi);
    float3 world = kAtmosphereRadius * float3(cp * sin(theta), sin(phi), cp * cos(theta));

    SatViewVertexOut out;
    out.position = frame.view_proj * float4(world, 1.0f);
    out.normal = normalize(world);
    out.world = world;
    out.uv = float2(u, v);
    return out;
}

static float2 atmosphere_ray_sphere(float3 origin, float3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0f)
        return float2(-1.0f);
    float root = sqrt(discriminant);
    return float2(-b - root, -b + root);
}

static float2 atmosphere_density(float3 position)
{
    float altitude = max(length(position) - 1.0f, 0.0f);
    return float2(
        exp(-altitude / kRayleighScaleHeight),
        exp(-altitude / kMieScaleHeight));
}

static float2 atmosphere_optical_depth_to_sun(float3 origin, float3 sun_direction)
{
    float2 planet_hit = atmosphere_ray_sphere(origin, sun_direction, 1.0f);
    if (planet_hit.x > 0.00001f)
        return float2(-1.0f);

    float2 atmosphere_hit = atmosphere_ray_sphere(origin, sun_direction, kAtmosphereRadius);
    if (atmosphere_hit.y <= 0.0f)
        return float2(-1.0f);

    float2 optical_depth = float2(0.0f);
    for (int step_index = 0; step_index < kAtmosphereLightSteps; ++step_index)
    {
        float u0 = float(step_index) / float(kAtmosphereLightSteps);
        float u1 = float(step_index + 1) / float(kAtmosphereLightSteps);
        float distance0 = atmosphere_hit.y * u0 * u0;
        float distance1 = atmosphere_hit.y * u1 * u1;
        float step_length = distance1 - distance0;
        float distance = 0.5f * (distance0 + distance1);
        optical_depth += atmosphere_density(origin + sun_direction * distance) * step_length;
    }
    return optical_depth;
}

fragment float4 satview_atmosphere_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    float3 ray_origin = frame.camera_pos.xyz;
    float3 ray_direction = normalize(in.world - ray_origin);
    float3 sun_direction = normalize(frame.sun_dir_time.xyz);

    float2 atmosphere_hit = atmosphere_ray_sphere(ray_origin, ray_direction, kAtmosphereRadius);
    if (atmosphere_hit.y <= 0.0f)
        discard_fragment();

    float ray_start = max(atmosphere_hit.x, 0.0f);
    float ray_end = atmosphere_hit.y;
    float fragment_distance = length(in.world - ray_origin);
    if (fragment_distance > 0.5f * (ray_start + ray_end))
        discard_fragment();

    float2 planet_hit = atmosphere_ray_sphere(ray_origin, ray_direction, 1.0f);
    bool hits_planet = planet_hit.x > ray_start && planet_hit.x < ray_end;
    if (hits_planet)
        ray_end = planet_hit.x;
    if (ray_end <= ray_start)
        discard_fragment();

    float segment_length = ray_end - ray_start;
    float2 view_optical_depth = float2(0.0f);
    float3 rayleigh_sum = float3(0.0f);
    float3 mie_sum = float3(0.0f);
    for (int step_index = 0; step_index < kAtmosphereViewSteps; ++step_index)
    {
        float u0 = float(step_index) / float(kAtmosphereViewSteps);
        float u1 = float(step_index + 1) / float(kAtmosphereViewSteps);
        float distance0 = segment_length * (hits_planet
                ? 1.0f - (1.0f - u0) * (1.0f - u0)
                : u0);
        float distance1 = segment_length * (hits_planet
                ? 1.0f - (1.0f - u1) * (1.0f - u1)
                : u1);
        float step_length = distance1 - distance0;
        float distance = ray_start + 0.5f * (distance0 + distance1);
        float3 sample_position = ray_origin + ray_direction * distance;
        float2 density = atmosphere_density(sample_position);
        float2 light_optical_depth = atmosphere_optical_depth_to_sun(sample_position, sun_direction);
        if (light_optical_depth.x >= 0.0f)
        {
            float2 total_optical_depth = view_optical_depth + light_optical_depth;
            float3 transmittance = exp(-(
                kRayleighBeta * total_optical_depth.x
                + kMieBeta * total_optical_depth.y));
            rayleigh_sum += density.x * transmittance * step_length;
            mie_sum += density.y * transmittance * step_length;
        }
        view_optical_depth += density * step_length;
    }

    float cosine = dot(-ray_direction, sun_direction);
    float rayleigh_phase = 3.0f * (1.0f + cosine * cosine) / (16.0f * kPi);
    float mie_denominator = pow(1.0f + kMieG * kMieG - 2.0f * kMieG * cosine, 1.5f);
    float mie_phase = (1.0f - kMieG * kMieG) / (4.0f * kPi * mie_denominator);
    float3 scattering = (
        rayleigh_sum * kRayleighBeta * rayleigh_phase
        + mie_sum * kMieBeta * mie_phase)
        * 6.0f;
    float extinction = dot(
        kRayleighBeta * view_optical_depth.x + kMieBeta * view_optical_depth.y,
        float3(0.2126f, 0.7152f, 0.0722f));
    float alpha = clamp(1.0f - exp(-extinction), 0.0f, 0.92f);
    scattering = min(scattering, float3(alpha));
    return float4(scattering, alpha);
}

static float2 fullscreen_corner(uint vertex_id)
{
    switch (vertex_id)
    {
    case 0:
        return float2(-1.0f, -1.0f);
    case 1:
        return float2(-1.0f, 3.0f);
    default:
        return float2(3.0f, -1.0f);
    }
}

vertex SatViewVertexOut satview_ground_atmosphere_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    float2 ndc = fullscreen_corner(vertex_id);

    SatViewVertexOut out;
    out.position = float4(ndc, 1.0f, 1.0f);
    out.normal = float3(0.0f);
    out.world = float3(0.0f);
    out.uv = ndc;
    return out;
}

static float3 ground_ray_direction(float2 ndc, constant SatViewFrameUniforms& frame)
{
    if (is_stereographic_ground_projection(frame))
    {
        float scale = max(abs(frame.view_proj[3][2]), 0.000001f);
        float aspect_scale = ground_projection_aspect_scale(frame);
        float2 plane = float2(ndc.x / aspect_scale, ndc.y) * scale;
        float radius_sq = dot(plane, plane);
        float3 camera_direction = float3(
            2.0f * plane,
            radius_sq - 1.0f) / (1.0f + radius_sq);
        return normalize(transpose(ground_world_to_camera(frame)) * camera_direction);
    }

    float3 right = normalize(rotate_vector_by_quaternion(float3(1.0f, 0.0f, 0.0f), frame.camera_orientation));
    float3 up = normalize(rotate_vector_by_quaternion(float3(0.0f, 1.0f, 0.0f), frame.camera_orientation));
    float3 forward = normalize(rotate_vector_by_quaternion(float3(0.0f, 0.0f, -1.0f), frame.camera_orientation));

    const float3 center = frame.camera_pos.xyz + forward;
    float4 center_clip = project_world_position(center, frame);
    float4 right_clip = frame.view_proj * float4(center + right, 1.0f);
    float4 up_clip = frame.view_proj * float4(center + up, 1.0f);
    float2 center_ndc = center_clip.xy / center_clip.w;
    const float x_scale = max(abs(right_clip.x / right_clip.w - center_ndc.x), 0.0001f);
    const float y_scale = max(abs(up_clip.y / up_clip.w - center_ndc.y), 0.0001f);

    float2 local_ndc = ndc - center_ndc;
    float3 camera_ray = normalize(float3(local_ndc.x / x_scale, local_ndc.y / y_scale, -1.0f));
    return normalize(rotate_vector_by_quaternion(camera_ray, frame.camera_orientation));
}

fragment float4 satview_skybox_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    texture2d<float> milky_way_tex [[texture(6)]],
    sampler sky_sampler [[sampler(0)]])
{
    float3 direction = ground_ray_direction(in.uv, frame);
    if (is_ground_projection(frame) && ground_horizon_occlusion_enabled(frame))
    {
        float3 observer_up = normalize(frame.camera_pos.xyz);
        float observer_radius_sq = max(dot(frame.camera_pos.xyz, frame.camera_pos.xyz), 1.0f);
        float horizon_cosine = -sqrt(max(1.0f - 1.0f / observer_radius_sq, 0.0f));
        if (dot(direction, observer_up) <= horizon_cosine)
            discard_fragment();
    }

    // Inverse of SatView's ICRF-to-render basis: eq = (-z, -x, y).
    float right_ascension = atan2(-direction.x, -direction.z);
    float declination = asin(clamp(direction.y, -1.0f, 1.0f));
    float2 uv = float2(
        fract(0.5f - right_ascension / (2.0f * kPi)),
        0.5f - declination / kPi);
    return float4(
        milky_way_tex.sample(sky_sampler, uv).rgb
            * clamp(frame.render_params.w, 0.0f, 1.0f),
        1.0f);
}

fragment float4 satview_ground_atmosphere_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]])
{
    float3 ray_origin = frame.camera_pos.xyz;
    float3 ray_direction = ground_ray_direction(in.uv, frame);
    float3 sun_direction = normalize(frame.sun_dir_time.xyz);
    float3 observer_up = normalize(ray_origin);
    float observer_radius_sq = max(dot(ray_origin, ray_origin), 1.0f);
    float horizon_cosine = -sqrt(max(1.0f - 1.0f / observer_radius_sq, 0.0f));
    if (dot(ray_direction, observer_up) <= horizon_cosine)
        discard_fragment();

    float2 atmosphere_hit = atmosphere_ray_sphere(ray_origin, ray_direction, kAtmosphereRadius);
    if (atmosphere_hit.y <= 0.0f)
        discard_fragment();

    float ray_start = 0.0f;
    float ray_end = atmosphere_hit.y;
    float2 planet_hit = atmosphere_ray_sphere(ray_origin, ray_direction, 1.0f);
    if (planet_hit.x > 0.0f)
        ray_end = min(ray_end, planet_hit.x);
    if (ray_end <= ray_start)
        discard_fragment();

    float segment_length = ray_end - ray_start;
    float2 view_optical_depth = float2(0.0f);
    float3 rayleigh_sum = float3(0.0f);
    float3 mie_sum = float3(0.0f);
    for (int step_index = 0; step_index < 16; ++step_index)
    {
        float u0 = float(step_index) / 16.0f;
        float u1 = float(step_index + 1) / 16.0f;
        float distance0 = segment_length * (1.0f - (1.0f - u0) * (1.0f - u0));
        float distance1 = segment_length * (1.0f - (1.0f - u1) * (1.0f - u1));
        float step_length = distance1 - distance0;
        float distance = ray_start + 0.5f * (distance0 + distance1);
        float3 sample_position = ray_origin + ray_direction * distance;
        float2 density = atmosphere_density(sample_position);
        float2 light_optical_depth = atmosphere_optical_depth_to_sun(sample_position, sun_direction);
        if (light_optical_depth.x >= 0.0f)
        {
            float2 total_optical_depth = view_optical_depth + light_optical_depth;
            float3 transmittance = exp(-(
                kRayleighBeta * total_optical_depth.x
                + kMieBeta * total_optical_depth.y));
            rayleigh_sum += density.x * transmittance * step_length;
            mie_sum += density.y * transmittance * step_length;
        }
        view_optical_depth += density * step_length;
    }

    float cosine = dot(-ray_direction, sun_direction);
    float rayleigh_phase = 3.0f * (1.0f + cosine * cosine) / (16.0f * kPi);
    float mie_denominator = pow(1.0f + kMieG * kMieG - 2.0f * kMieG * cosine, 1.5f);
    float mie_phase = (1.0f - kMieG * kMieG) / (4.0f * kPi * mie_denominator);
    float3 scattering = (
        rayleigh_sum * kRayleighBeta * rayleigh_phase
        + mie_sum * kMieBeta * mie_phase)
        * 4.5f;
    float extinction = dot(
        kRayleighBeta * view_optical_depth.x + kMieBeta * view_optical_depth.y,
        float3(0.2126f, 0.7152f, 0.0722f));
    float alpha = clamp(1.0f - exp(-extinction), 0.0f, 1.0f);
    float3 night_tint = float3(0.004f, 0.006f, 0.014f);
    scattering = max(scattering, night_tint * (1.0f - alpha));
    return float4(scattering, alpha);
}

fragment float4 satview_ground_surface_fragment(
    SatViewVertexOut in [[stage_in]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    texture2d<float> earth_day_tex [[texture(0)]],
    texture2d<float> earth_night_tex [[texture(1)]],
    texture2d<float> earth_cloud_tex [[texture(2)]],
    texture2d<float> earth_live_cloud_tex [[texture(3)]],
    sampler earth_sampler [[sampler(0)]])
{
    float3 ray_origin = frame.camera_pos.xyz;
    float3 ray_direction = ground_ray_direction(in.uv, frame);
    float3 up = normalize(ray_origin);
    float observer_radius_sq = max(dot(ray_origin, ray_origin), 1.0f);
    float horizon_cosine = -sqrt(max(1.0f - 1.0f / observer_radius_sq, 0.0f));
    float ground_alpha = 1.0f - smoothstep(
        horizon_cosine - 0.0015f,
        horizon_cosine + 0.0015f,
        dot(ray_direction, up));
    if (ground_alpha <= 0.001f)
        discard_fragment();

    float2 planet_hit = atmosphere_ray_sphere(ray_origin, ray_direction, 1.0f);
    float hit_distance = planet_hit.x > 0.0f ? planet_hit.x : planet_hit.y;
    if (hit_distance <= 0.0f)
        discard_fragment();

    float3 world = ray_origin + ray_direction * hit_distance;
    float3 normal = normalize(world);
    float3 ecef = normalize(render_teme_to_ecef(normal, frame.render_params.z));
    float longitude = atan2(ecef.y, ecef.x);
    float latitude = asin(clamp(ecef.z, -1.0f, 1.0f));
    float2 uv = float2(
        fract(longitude / (2.0f * kPi) + 0.5f),
        0.5f - latitude / kPi);

    float3 light = normalize(frame.sun_dir_time.xyz);
    float3 view = normalize(frame.camera_pos.xyz - world);
    float3 day_surface = earth_day_tex.sample(earth_sampler, uv).rgb;
    float3 night_surface = earth_night_tex.sample(earth_sampler, uv).rgb;
    float ndl = dot(normal, light);
    float day = smoothstep(-0.08f, 0.14f, ndl);
    float diffuse = max(ndl, 0.0f);
    float3 lit = day_surface * (0.22f + diffuse * 1.08f);
    float ocean_hint = smoothstep(0.03f, 0.24f, day_surface.b - max(day_surface.r, day_surface.g));
    float specular = pow(max(dot(reflect(-light, normal), view), 0.0f), 48.0f)
        * ocean_hint * smoothstep(0.0f, 0.25f, ndl);
    lit += float3(0.55f, 0.72f, 0.90f) * specular * 0.30f;

    float3 night = night_surface * 1.85f + day_surface * 0.015f;
    float3 color = mix(night, lit, day);
    if (frame.sun_dir_time.w > 0.5f)
    {
        float3 bundled_cloud_sample = earth_cloud_tex.sample(earth_sampler, uv).rgb;
        float3 live_cloud_sample = earth_live_cloud_tex.sample(earth_sampler, uv).rgb;
        float3 cloud_sample = mix(
            bundled_cloud_sample,
            live_cloud_sample,
            clamp(frame.sun_dir_time.w - 1.0f, 0.0f, 1.0f));
        float opacity = smoothstep(
            0.20f, 0.78f, dot(cloud_sample, float3(0.299f, 0.587f, 0.114f)));
        float daylight = smoothstep(-0.10f, 0.16f, ndl);
        float3 day_cloud = float3(0.91f, 0.95f, 1.0f) * (0.34f + diffuse * 0.76f);
        float3 night_cloud = float3(0.025f, 0.030f, 0.042f);
        float3 cloud_color = mix(night_cloud, day_cloud, daylight);
        float cloud_alpha = opacity * mix(0.32f, 0.78f, daylight);
        color = mix(color, cloud_color, cloud_alpha);
    }
    return float4(color * ground_alpha, ground_alpha);
}

static float2 star_quad_corner(uint corner_index)
{
    switch (corner_index)
    {
    case 0:
        return float2(-1.0f, -1.0f);
    case 1:
        return float2(-1.0f, 1.0f);
    case 2:
        return float2(1.0f, -1.0f);
    case 3:
        return float2(1.0f, -1.0f);
    case 4:
        return float2(-1.0f, 1.0f);
    default:
        return float2(1.0f, 1.0f);
    }
}

vertex SatViewStarOut satview_star_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    constant SatViewStarInstance* stars [[buffer(1)]])
{
    SatViewStarInstance star = stars[instance_id];
    float3 direction = normalize(star.direction_magnitude.xyz);
    float3 center = frame.camera_pos.xyz + direction * kStarDistanceEarthRadii;
    float4 center_clip = project_world_position(center, frame);

    SatViewStarOut out;
    float min_magnitude = frame.render_params.x;
    float max_magnitude = max(frame.render_params.y, min_magnitude + 0.001f);
    if (star.direction_magnitude.w < min_magnitude || star.direction_magnitude.w > max_magnitude)
    {
        out.color = float4(0.0f);
        out.uv = float2(2.0f);
        out.position = float4(2.0f, 2.0f, 0.0f, 1.0f);
        return out;
    }

    float magnitude_t = clamp(
        (star.direction_magnitude.w - min_magnitude) / (max_magnitude - min_magnitude),
        0.0f,
        1.0f);
    float visibility = 1.0f - magnitude_t;
    float max_channel = max(star.color_size.r, max(star.color_size.g, star.color_size.b));
    float3 chroma = max_channel > 0.00001f ? star.color_size.rgb / max_channel : float3(1.0f);
    float brightness = mix(0.035f, 1.0f, pow(visibility, 1.85f)) * max(frame.render_params.w, 0.0f);
    float star_size = mix(0.0009f, 0.0090f, pow(visibility, 0.72f));
    out.color = float4(chroma * brightness, 1.0f);
    if (!ground_world_position_visible(center, frame) || center_clip.w <= 0.0f)
    {
        out.color.a = 0.0f;
        out.uv = float2(2.0f);
        out.position = float4(2.0f, 2.0f, 0.0f, 1.0f);
        return out;
    }

    float2 corner = star_quad_corner(vertex_id);
    float x_scale = max(frame.render_params.z, 0.0001f);
    center_clip.xy += corner * float2(x_scale, 1.0f) * star_size * center_clip.w;
    out.position = center_clip;
    out.uv = corner;
    return out;
}

static float2 celestial_line_endpoint_side(uint vertex_id)
{
    switch (vertex_id)
    {
    case 0:
        return float2(0.0f, -1.0f);
    case 1:
        return float2(0.0f, 1.0f);
    case 2:
        return float2(1.0f, -1.0f);
    case 3:
        return float2(0.0f, 1.0f);
    case 4:
        return float2(1.0f, 1.0f);
    default:
        return float2(1.0f, -1.0f);
    }
}

vertex SatViewCelestialLineOut satview_celestial_line_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    constant SatViewCelestialLineInstance* lines [[buffer(1)]])
{
    const float kLineDistanceEarthRadii = 48.0f;
    SatViewCelestialLineInstance line = lines[instance_id];
    float3 start_world = frame.camera_pos.xyz
        + normalize(line.start_direction_width.xyz) * kLineDistanceEarthRadii;
    float3 end_world = frame.camera_pos.xyz
        + normalize(line.end_direction_dash.xyz) * kLineDistanceEarthRadii;
    float4 start_clip = project_world_position(start_world, frame);
    float4 end_clip = project_world_position(end_world, frame);
    bool visible = ground_world_position_visible(start_world, frame)
        && ground_world_position_visible(end_world, frame)
        && start_clip.w > 0.0f
        && end_clip.w > 0.0f;

    float2 start_ndc = start_clip.xy / max(start_clip.w, 0.000001f);
    float2 end_ndc = end_clip.xy / max(end_clip.w, 0.000001f);
    float2 viewport = max(frame.render_params.xy, float2(1.0f));
    float2 screen_delta = (end_ndc - start_ndc) * viewport * 0.5f;
    float segment_pixels = length(screen_delta);
    visible = visible
        && !any(isnan(float4(start_ndc, end_ndc)))
        && !any(isinf(float4(start_ndc, end_ndc)))
        && segment_pixels > 0.0001f
        && length(end_ndc - start_ndc) < 8.0f;

    float2 endpoint_side = celestial_line_endpoint_side(vertex_id);
    float t = endpoint_side.x;
    float4 clip = mix(start_clip, end_clip, t);
    float2 perpendicular = segment_pixels > 0.0001f
        ? normalize(float2(-screen_delta.y, screen_delta.x))
        : float2(0.0f, 1.0f);
    float half_width = max(line.start_direction_width.w, 0.5f) * 0.5f;
    float2 offset_ndc = perpendicular * endpoint_side.y * half_width * 2.0f / viewport;
    clip.xy = (mix(start_ndc, end_ndc, t) + offset_ndc) * clip.w;

    SatViewCelestialLineOut out;
    out.position = visible ? clip : float4(2.0f, 2.0f, 0.0f, 1.0f);
    out.color = visible ? line.color : float4(0.0f);
    float segment_angle = acos(clamp(dot(
        normalize(line.start_direction_width.xyz),
        normalize(line.end_direction_dash.xyz)), -1.0f, 1.0f));
    float phase_pixels = line.style.y * segment_pixels / max(segment_angle, 0.0001f);
    out.line_distance = phase_pixels + t * segment_pixels;
    out.line_across = endpoint_side.y;
    out.dash_gap = float2(max(line.end_direction_dash.w, 0.0f), max(line.style.x, 0.0f));
    return out;
}

fragment float4 satview_celestial_line_fragment(SatViewCelestialLineOut in [[stage_in]])
{
    float edge_width = max(fwidth(in.line_across), 0.02f);
    float edge_alpha = 1.0f - smoothstep(
        1.0f - edge_width, 1.0f, abs(in.line_across));
    float dash_alpha = 1.0f;
    if (in.dash_gap.x > 0.0f && in.dash_gap.y > 0.0f)
    {
        float period = in.dash_gap.x + in.dash_gap.y;
        float position = fmod(in.line_distance, period);
        float dash_edge = max(fwidth(in.line_distance), 0.5f);
        dash_alpha = 1.0f - smoothstep(
            in.dash_gap.x - dash_edge,
            in.dash_gap.x + dash_edge,
            position);
    }
    return float4(in.color.rgb, in.color.a * edge_alpha * dash_alpha);
}

static float2 label_quad_corner(uint corner_index)
{
    switch (corner_index)
    {
    case 0:
        return float2(-1.0f, -1.0f);
    case 1:
        return float2(1.0f, -1.0f);
    case 2:
        return float2(1.0f, 1.0f);
    case 3:
        return float2(-1.0f, -1.0f);
    case 4:
        return float2(1.0f, 1.0f);
    default:
        return float2(-1.0f, 1.0f);
    }
}

vertex SatViewLabelOut satview_label_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    constant SatViewLabelInstance* labels [[buffer(1)]])
{
    const float kLabelDistanceEarthRadii = 48.0f;
    SatViewLabelInstance label = labels[instance_id];
    float3 world_position = frame.camera_pos.xyz
        + normalize(label.direction_priority.xyz) * kLabelDistanceEarthRadii;
    float4 projected = project_world_position(world_position, frame);
    bool visible = ground_world_position_visible(world_position, frame)
        && projected.w > 0.0f;
    float2 corner = label_quad_corner(vertex_id);
    float2 viewport = max(frame.render_params.xy, float2(1.0f));
    float2 pixel_offset = corner * label.pixel_size_offset.xy * 0.5f
        + label.pixel_size_offset.zw;
    projected.xy += pixel_offset * (2.0f / viewport) * projected.w;

    SatViewLabelOut out;
    out.position = visible ? projected : float4(2.0f, 2.0f, 0.0f, 1.0f);
    out.color = visible ? label.color : float4(0.0f);
    out.uv = mix(label.uv_rect.xy, label.uv_rect.zw, corner * 0.5f + 0.5f);
    return out;
}

fragment float4 satview_label_fragment(
    SatViewLabelOut in [[stage_in]],
    texture2d<float> label_atlas [[texture(7)]],
    sampler label_sampler [[sampler(1)]])
{
    float2 texel = 1.0f / float2(label_atlas.get_width(), label_atlas.get_height());
    float coverage = label_atlas.sample(label_sampler, in.uv).a;
    float nearby = coverage;
    nearby = max(nearby, label_atlas.sample(label_sampler, in.uv + float2(texel.x, 0.0f)).a);
    nearby = max(nearby, label_atlas.sample(label_sampler, in.uv - float2(texel.x, 0.0f)).a);
    nearby = max(nearby, label_atlas.sample(label_sampler, in.uv + float2(0.0f, texel.y)).a);
    nearby = max(nearby, label_atlas.sample(label_sampler, in.uv - float2(0.0f, texel.y)).a);
    float glyph_alpha = coverage * in.color.a;
    float halo_alpha = max(nearby - coverage, 0.0f) * 0.72f * in.color.a;
    float alpha = glyph_alpha + halo_alpha * (1.0f - glyph_alpha);
    float3 premultiplied = in.color.rgb * glyph_alpha
        + float3(0.004f, 0.006f, 0.010f) * halo_alpha * (1.0f - glyph_alpha);
    return float4(premultiplied, alpha);
}

static float3 landscape_world_direction(
    float3 local_direction,
    constant SatViewFrameUniforms& frame)
{
    float3 up = normalize(frame.camera_pos.xyz);
    float3 east = cross(float3(0.0f, 1.0f, 0.0f), up);
    if (dot(east, east) < 1.0e-8f)
        east = float3(1.0f, 0.0f, 0.0f);
    east = normalize(east);
    float3 north = normalize(cross(up, east));
    return normalize(
        east * local_direction.x
        + north * local_direction.y
        + up * local_direction.z);
}

static float4 project_landscape_direction(
    float3 direction,
    constant SatViewFrameUniforms& frame)
{
    const float kLandscapeDistanceEarthRadii = 48.0f;
    return project_world_position(
        frame.camera_pos.xyz + direction * kLandscapeDistanceEarthRadii,
        frame);
}

static bool landscape_projection_valid(
    float3 direction,
    float4 projected,
    constant SatViewFrameUniforms& frame)
{
    float2 ndc = projected.xy / max(projected.w, 0.000001f);
    bool valid = projected.w > 0.0f
        && all(isfinite(ndc))
        && max(abs(ndc.x), abs(ndc.y)) < 8.0f;
    if (is_stereographic_ground_projection(frame))
    {
        float3 camera_direction = ground_world_to_camera(frame) * direction;
        valid = valid && (1.0f - camera_direction.z) > 0.02f;
    }
    return valid;
}

vertex SatViewOrbitOut satview_landscape_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    constant SatViewLandscapeTriangleInstance* instances [[buffer(1)]])
{
    SatViewLandscapeTriangleInstance instance = instances[instance_id];
    float3 directions[3] = {
        landscape_world_direction(instance.local_direction0.xyz, frame),
        landscape_world_direction(instance.local_direction1.xyz, frame),
        landscape_world_direction(instance.local_direction2.xyz, frame)
    };
    float4 projected[3] = {
        project_landscape_direction(directions[0], frame),
        project_landscape_direction(directions[1], frame),
        project_landscape_direction(directions[2], frame)
    };
    bool visible = landscape_projection_valid(directions[0], projected[0], frame)
        && landscape_projection_valid(directions[1], projected[1], frame)
        && landscape_projection_valid(directions[2], projected[2], frame);

    SatViewOrbitOut out;
    out.position = visible ? projected[vertex_id] : float4(2.0f, 2.0f, 0.0f, 1.0f);
    out.color = visible ? instance.color : float4(0.0f);
    return out;
}

vertex SatViewOrbitOut satview_landscape_rim_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    constant SatViewLandscapeLineInstance* instances [[buffer(1)]])
{
    SatViewLandscapeLineInstance instance = instances[instance_id];
    float3 directions[2] = {
        landscape_world_direction(instance.local_direction0.xyz, frame),
        landscape_world_direction(instance.local_direction1.xyz, frame)
    };
    float4 projected[2] = {
        project_landscape_direction(directions[0], frame),
        project_landscape_direction(directions[1], frame)
    };
    bool visible = landscape_projection_valid(directions[0], projected[0], frame)
        && landscape_projection_valid(directions[1], projected[1], frame);

    SatViewOrbitOut out;
    out.position = visible ? projected[vertex_id] : float4(2.0f, 2.0f, 0.0f, 1.0f);
    out.color = visible ? instance.color : float4(0.0f);
    return out;
}

fragment float4 satview_star_fragment(SatViewStarOut in [[stage_in]])
{
    float radius2 = dot(in.uv, in.uv);
    float alpha = (1.0f - smoothstep(0.0f, 1.0f, radius2)) * in.color.a;
    alpha *= alpha;
    float3 low = in.color.rgb / 12.92f;
    float3 high = pow((in.color.rgb + 0.055f) / 1.055f, float3(2.4f));
    float3 linear_color = select(high, low, in.color.rgb <= 0.04045f);
    return float4(linear_color * alpha, 0.0f);
}

vertex SatViewOrbitOut satview_orbit_vertex(
    uint vertex_id [[vertex_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    constant SatViewSceneVertex* vertices [[buffer(1)]])
{
    SatViewOrbitOut out;
    SatViewSceneVertex scene_vertex = vertices[vertex_id];
    bool sun_centered = frame.render_params.w < -0.5f;
    float3 track_center = frame.camera_pos.w < 0.0f
        ? frame.camera_pos.xyz
        : frame.camera_orientation.xyz;
    float3 position = scene_vertex.position.xyz
        + (sun_centered ? track_center : float3(0.0f));
    float3 paired_position = scene_vertex.paired_position.xyz
        + (sun_centered ? track_center : float3(0.0f));
    if (frame.camera_pos.w < 0.0f)
    {
        bool earth_fixed = abs(scene_vertex.position.w) > 1.5f;
        float2 projected = map_position_from_render_teme(position, frame, earth_fixed);
        float2 paired = map_position_from_render_teme(paired_position, frame, earth_fixed);
        if (scene_vertex.position.w > 0.0f)
        {
            float delta = projected.x - paired.x;
            if (delta > 1.0f)
                projected.x -= 2.0f;
            else if (delta < -1.0f)
                projected.x += 2.0f;
        }
        projected.x += frame.camera_orientation.w;
        out.position = frame.view_proj * float4(projected, 0.4f, 1.0f);
    }
    else
    {
        out.position = project_world_position(position, frame);
    }
    out.color = scene_vertex.color;
    return out;
}

static float3 rotate_by_quaternion(float3 value, float4 quaternion)
{
    float3 twice_cross = 2.0f * cross(quaternion.xyz, value);
    return value + quaternion.w * twice_cross + cross(quaternion.xyz, twice_cross);
}

static float2 marker_endpoint(int style, uint segment, uint endpoint)
{
    float sign_value = endpoint == 0u ? -1.0f : 1.0f;
    if (style == 1)
    {
        if (segment == 0u)
            return endpoint == 0u ? float2(-0.9f, 0.65f) : float2(0.9f, 0.65f);
        if (segment == 1u)
            return endpoint == 0u ? float2(0.9f, 0.65f) : float2(0.0f, -0.75f);
        if (segment == 2u)
            return endpoint == 0u ? float2(0.0f, -0.75f) : float2(-0.9f, 0.65f);
        return endpoint == 0u ? float2(0.0f, -0.75f) : float2(0.0f, -1.2f);
    }
    if (style == 2)
    {
        if (segment == 0u)
            return endpoint == 0u ? float2(-0.9f, -0.7f) : float2(0.9f, -0.7f);
        if (segment == 1u)
            return endpoint == 0u ? float2(0.9f, -0.7f) : float2(0.9f, 0.7f);
        if (segment == 2u)
            return endpoint == 0u ? float2(0.9f, 0.7f) : float2(-0.9f, 0.7f);
        return endpoint == 0u ? float2(-0.9f, 0.7f) : float2(-0.9f, -0.7f);
    }
    if (style == 3)
    {
        if (segment == 0u)
            return endpoint == 0u ? float2(0.0f, -1.0f) : float2(1.0f, 0.0f);
        if (segment == 1u)
            return endpoint == 0u ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f);
        if (segment == 2u)
            return endpoint == 0u ? float2(0.0f, 1.0f) : float2(-1.0f, 0.0f);
        return endpoint == 0u ? float2(-1.0f, 0.0f) : float2(0.0f, -1.0f);
    }
    float2 axis = segment == 0u ? float2(1.0f, 0.0f)
        : segment == 1u ? float2(0.0f, 1.0f)
        : segment == 2u ? float2(1.0f, 1.0f) * 0.70710678f
        : float2(1.0f, -1.0f) * 0.70710678f;
    return axis * sign_value;
}

vertex SatViewOrbitOut satview_marker_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant SatViewFrameUniforms& frame [[buffer(0)]],
    constant SatViewMarkerInstance* markers [[buffer(1)]])
{
    SatViewMarkerInstance marker = markers[instance_id];
    uint segment = vertex_id / 2u;
    uint endpoint = vertex_id & 1u;
    float selected = marker.position1_selected.w;
    int style = int(round(marker.style.x));
    float2 marker_offset = marker_endpoint(style, segment, endpoint);
    float alpha = clamp(frame.render_params.w, 0.0f, 1.0f);

    float3 center = mix(marker.position0_size.xyz, marker.position1_selected.xyz, alpha);
    SatViewOrbitOut out;
    out.color = marker.color;
    if (style == 0 && segment >= 2u && selected < 0.5f)
        out.color.a = 0.0f;
    if (frame.camera_pos.w < 0.0f)
    {
        float2 map_center = map_position_from_render_teme(center, frame, false);
        float x_scale = abs(frame.camera_pos.w);
        map_center.x += marker.style.y;
        float2 map_position = map_center
            + marker_offset * float2(x_scale, 1.0f) * marker.position0_size.w * 0.75f;
        out.position = frame.view_proj * float4(map_position, 0.2f, 1.0f);
    }
    else if (is_ground_projection(frame))
    {
        float4 center_clip = project_world_position(center, frame);
        float2 center_ndc = center_clip.xy / max(center_clip.w, 0.000001f);
        if (!ground_world_position_visible(center, frame)
            || center_clip.w <= 0.0f
            || abs(center_ndc.x) > 1.5f
            || abs(center_ndc.y) > 1.5f)
        {
            out.color.a = 0.0f;
            out.position = float4(2.0f, 2.0f, 0.0f, 1.0f);
            return out;
        }
        float x_scale = ground_projection_aspect_scale(frame);
        center_clip.xy += marker_offset * float2(x_scale, 1.0f)
            * marker.position0_size.w * 0.75f * center_clip.w;
        out.position = center_clip;
    }
    else
    {
        float3 right = normalize(rotate_by_quaternion(float3(1.0f, 0.0f, 0.0f), frame.camera_orientation));
        float3 up = normalize(rotate_by_quaternion(float3(0.0f, 1.0f, 0.0f), frame.camera_orientation));
        float3 world = center
            + (right * marker_offset.x + up * marker_offset.y) * marker.position0_size.w;
        out.position = frame.view_proj * float4(world, 1.0f);
    }
    return out;
}

fragment float4 satview_orbit_fragment(SatViewOrbitOut in [[stage_in]])
{
    float3 low = in.color.rgb / 12.92f;
    float3 high = pow((in.color.rgb + 0.055f) / 1.055f, float3(2.4f));
    float3 linear_color = select(high, low, in.color.rgb <= 0.04045f);
    return float4(linear_color, in.color.a);
}

struct SatViewFullscreenOut
{
    float4 position [[position]];
    float2 uv;
};

vertex SatViewFullscreenOut satview_fullscreen_vertex(uint vertex_id [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(3.0f, -1.0f),
        float2(-1.0f, 3.0f)
    };
    const float2 uvs[3] = {
        float2(0.0f, 1.0f),
        float2(2.0f, 1.0f),
        float2(0.0f, -1.0f)
    };
    SatViewFullscreenOut out;
    out.position = float4(positions[vertex_id], 0.0f, 1.0f);
    out.uv = uvs[vertex_id];
    return out;
}

static float3 satview_tone_map_aces(float3 hdr, float exposure, float white_point)
{
    float3 color = max(hdr, float3(0.0f)) * max(exposure, 0.0f);
    color /= max(white_point, 1e-3f);
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0f, 1.0f);
}

fragment float4 satview_post_fragment(
    SatViewFullscreenOut in [[stage_in]],
    constant float4& tone_map [[buffer(0)]],
    texture2d<float> hdr_scene [[texture(0)]],
    sampler linear_sampler [[sampler(0)]])
{
    float4 hdr = hdr_scene.sample(linear_sampler, in.uv);
    return float4(satview_tone_map_aces(hdr.rgb, tone_map.x, tone_map.y), clamp(hdr.a, 0.0f, 1.0f));
}

fragment float4 satview_present_fragment(
    SatViewFullscreenOut in [[stage_in]],
    texture2d<float> final_scene [[texture(0)]],
    sampler linear_sampler [[sampler(0)]])
{
    return final_scene.sample(linear_sampler, in.uv);
}

static float3 satview_msaa_heat_map(float value)
{
    float t = clamp(value, 0.0f, 1.0f);
    return clamp(float3(
        1.5f * t,
        1.5f * (1.0f - abs(2.0f * t - 1.0f)),
        1.5f * (1.0f - t)),
        0.0f,
        1.0f);
}

fragment float4 satview_msaa_debug_fragment(
    SatViewFullscreenOut in [[stage_in]],
    constant uint& sample_count_value [[buffer(0)]],
    texture2d_ms<float> msaa_scene [[texture(0)]])
{
    uint sample_count = clamp(sample_count_value, 1u, 4u);
    uint2 size = uint2(msaa_scene.get_width(), msaa_scene.get_height());
    uint2 coord = min(uint2(in.position.xy), size - uint2(1u));
    float3 minimum_color = msaa_scene.read(coord, 0).rgb;
    float3 maximum_color = minimum_color;
    for (uint sample_index = 1; sample_index < sample_count; ++sample_index)
    {
        float3 color = msaa_scene.read(coord, sample_index).rgb;
        minimum_color = min(minimum_color, color);
        maximum_color = max(maximum_color, color);
    }
    float3 difference_rgb = maximum_color - minimum_color;
    float difference = max(difference_rgb.r, max(difference_rgb.g, difference_rgb.b));
    float intensity = sample_count > 1u ? 1.0f - exp(-difference * 6.0f) : 0.0f;
    return float4(satview_msaa_heat_map(intensity) * intensity, 1.0f);
}
