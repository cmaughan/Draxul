bool satview_is_ground_projection()
{
    return push.camera_pos.w >= 0.5;
}

bool satview_is_stereographic_ground_projection()
{
    return push.camera_pos.w >= 2.5;
}

bool satview_ground_horizon_occlusion_enabled()
{
    int projection_code = int(floor(push.camera_pos.w + 0.5));
    return projection_code == 2 || projection_code == 4;
}

float satview_ground_projection_aspect_scale()
{
    if (satview_is_stereographic_ground_projection())
        return max(abs(push.view_proj[3][3]), 0.000001);
    return max(abs(push.view_proj[0][0] / push.view_proj[1][1]), 0.000001);
}

bool satview_ground_world_position_visible(vec3 world_position)
{
    if (!satview_is_ground_projection() || !satview_ground_horizon_occlusion_enabled())
        return true;
    vec3 observer_up = normalize(push.camera_pos.xyz);
    return dot(world_position - push.camera_pos.xyz, observer_up) > 0.0;
}

vec4 satview_project_world_position(vec3 world_position)
{
    if (!satview_is_stereographic_ground_projection())
        return push.view_proj * vec4(world_position, 1.0);

    vec3 offset = world_position - push.camera_pos.xyz;
    float distance_to_camera = length(offset);
    if (distance_to_camera <= 0.000001)
        return vec4(2.0, 2.0, 1.0, 1.0);

    vec3 camera_direction = mat3(push.view_proj) * (offset / distance_to_camera);
    float denominator = 1.0 - camera_direction.z;
    if (denominator <= 0.000001)
        return vec4(2.0, 2.0, 1.0, 1.0);

    float scale = max(abs(push.view_proj[3][2]), 0.000001);
    float aspect_scale = satview_ground_projection_aspect_scale();
    vec2 plane = camera_direction.xy / denominator;
    vec2 ndc = plane * vec2(aspect_scale, 1.0) / scale;
    // Reversed-Z: depth decreases with distance (1 at the camera, 0 at infinity).
    float depth = 1.0 / (distance_to_camera + 1.0);
    return vec4(ndc, depth, 1.0);
}

vec3 satview_ground_ray_direction(vec2 ndc)
{
    if (!satview_is_stereographic_ground_projection())
    {
        mat4 inv_view_proj = inverse(push.view_proj);
        vec4 world_far = inv_view_proj * vec4(ndc, 1.0, 1.0);
        world_far /= world_far.w;
        return normalize(world_far.xyz - push.camera_pos.xyz);
    }

    float scale = max(abs(push.view_proj[3][2]), 0.000001);
    float aspect_scale = satview_ground_projection_aspect_scale();
    vec2 plane = vec2(ndc.x / aspect_scale, ndc.y) * scale;
    float radius_sq = dot(plane, plane);
    vec3 camera_direction = vec3(
        2.0 * plane,
        radius_sq - 1.0) / (1.0 + radius_sq);
    return normalize(transpose(mat3(push.view_proj)) * camera_direction);
}
