# SatView Ground Sky Projections

## Goal

Give Ground view an explicit stereographic or perspective sky projection, with
stereographic as the default, while allowing the camera to rotate continuously
from zenith through the horizon to nadir.

The implementation is derived from the projection geometry and does not reuse
Stellarium source code.

## Design

- Keep Globe, Map, and Ground as the existing top-level presentation modes.
- Add a Ground-only projection choice: Stereographic or Perspective.
- Define tested CPU forward and inverse projection functions using camera-space
  directions. Mirror the same equations in GLSL and Metal.
- Store the ground camera as a quaternion in the observer's local frame. Build
  its view matrix from that quaternion each frame without Euler pitch clamps.
- Treat the rendered ground surface and horizon occlusion as independent state.
- Apply the selected projection consistently to atmosphere rays, the surface,
  stars, the Sun and Moon, satellite markers, orbit tracks, and CPU picking.
- Keep the existing 128-byte Vulkan push-constant contract. In stereographic
  Ground mode, the matrix slot carries a world-to-camera rotation plus the
  stereographic scale instead of a linear clip transform.

## Acceptance Criteria

- Stereographic is the default for new and existing configurations without an
  explicit Ground projection setting.
- Perspective remains visually and behaviorally compatible with the current
  Ground view at ordinary fields of view.
- Stereographic supports a field of view up to 235 degrees; Perspective remains
  capped at 120 degrees.
- Mouse and keyboard look controls can cross zenith and nadir without a flip or
  clamp.
- Picking and every rendered sky primitive agree on screen position in both
  projections.
- Ground visibility and horizon occlusion are persisted controls.
- Windows Vulkan builds, SatView unit tests, and the application smoke test pass;
  the corresponding Metal code path remains source-compatible.
