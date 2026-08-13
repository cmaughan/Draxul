#include <draxul/codeviz_scene_sort.h>

#include <algorithm>
#include <draxul/perf_timing.h>
#include <glm/geometric.hpp>

namespace draxul
{

void sort_scene_objects(CodeVizSceneSnapshot& scene)
{
    PERF_MEASURE();
    const glm::vec3 cam_pos(scene.camera.camera_pos);

    auto partition_it = std::stable_partition(
        scene.objects.begin(), scene.objects.end(),
        [](const CodeVizRenderable& obj) { return obj.color.a >= 1.0f; });

    scene.opaque_count = static_cast<uint32_t>(std::distance(scene.objects.begin(), partition_it));

    std::sort(partition_it, scene.objects.end(),
        [&cam_pos](const CodeVizRenderable& a, const CodeVizRenderable& b) {
            const glm::vec3 ca(a.world[3]);
            const glm::vec3 cb(b.world[3]);
            return glm::dot(ca - cam_pos, ca - cam_pos) > glm::dot(cb - cam_pos, cb - cam_pos);
        });
}

} // namespace draxul
