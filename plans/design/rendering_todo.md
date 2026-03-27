# Rendering TODO

## Final Output Color Space

- Megacity currently does lighting in linear space and then applies a manual output gamma step in the final scene shaders before writing to a non-sRGB target.
- The cleaner long-term path is to switch the final presentation target to an actual sRGB framebuffer/attachment format on both Metal and Vulkan.
- When that happens, remove the manual gamma encode from the shader to avoid double-encoding.
- Keep the rule clear:
  - sample authored albedo/base-color textures as sRGB
  - do all lighting and material math in linear
  - let the final sRGB render target perform the linear-to-sRGB conversion
- Audit debug views separately when this changes, because some previews should continue to show raw linear data while presentation/output should be display-encoded.

## Material Branching In The Forward Shader

- The current `material_id` branch in the Megacity forward shader is mostly about surface evaluation, not different lighting models.
- The shared lighting path is the same once the shader has resolved:
  - albedo
  - world-space normal
  - roughness
  - material AO
- The branching is currently needed because Megacity geometry is generic and does not yet provide fully authored per-material UVs/tangents for everything.
- In practice the branch currently decides:
  - which texture set to sample
  - how UVs are generated
  - how the tangent basis/TBN is built
  - how strongly normal/AO maps are applied
- Roads use world-space planar UVs and a simple fixed TBN.
- Buildings currently use dominant-axis mapping because stacked cube geometry does not yet carry authored facade UVs/tangents.
- A future material/mesh cleanup could move more of this into shared material evaluation helpers or mesh-authored data, reducing explicit shader branching.
