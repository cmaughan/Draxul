# SatView Earth-Moon Rotating Frame Option

## Status

Future visualization option. Keep SatView's Moon-centred inertial frame as the
default.

## Motivation

CAPSTONE's near-rectilinear halo orbit is approximately periodic in the
Earth-Moon rotating (synodic) frame, but not in the Moon-centred inertial ICRF
frame currently used by SatView. The current seven-day Horizons track is
continuous, yet its endpoints do not meet in the inertial view. A rotating-frame
view would make the characteristic NRHO shape easier to understand without
inventing a closing segment.

## Proposed User Experience

- Add a Moon-POV selector: `Reference frame: Inertial / Earth-Moon rotating`.
- Keep `Inertial` as the default and persist the choice in `config.toml`.
- Clearly label rotating mode in the viewport and selected-object details.
- Transform every lunar marker and track consistently; never mix a rotating
  CAPSTONE track with inertial LRO, Danuri, or Chandrayaan tracks.
- Leave sampled tracks open unless their real transformed endpoints meet within
  a documented tolerance. Consider fading or timestamping past/future endpoints.

## Tradeoffs

### Benefits

- CAPSTONE's approximately seven-day NRHO becomes a recognizable, nearly closed
  halo.
- Earth and Moon remain on a stable axis, making Lagrange-region and three-body
  dynamics more intuitive.
- The view better matches common mission-design diagrams for Gateway and
  CAPSTONE.

### Costs

- The frame is non-inertial: Coriolis and centrifugal effects are implicit.
- Stars, constellations, and the Sun rotate in this view.
- Ordinary low lunar orbits can appear to precess or form rosettes.
- Real CAPSTONE data may still not close perfectly because the NRHO is
  quasi-periodic and includes solar perturbations, non-circular Earth-Moon
  motion, and station-keeping.
- A time-dependent transform must be applied consistently to rendering,
  selection, labels, camera controls, map projection, and track bounds.

## Technical Direction

1. Define one backend-neutral Earth-Moon synodic transform for a requested
   simulation epoch:
   - origin at the Moon for Moon POV;
   - primary axis along the instantaneous Moon-to-Earth direction;
   - normal derived from the instantaneous Earth-Moon orbital plane;
   - complete the right-handed basis from those axes.
2. Transform each marker at its own epoch and every sampled track point at that
   point's epoch. Do not rotate an entire multi-day track using only the current
   epoch's basis.
3. Apply the same frame to contextual bodies and celestial overlays, or suppress
   overlays whose meaning would be ambiguous in rotating mode.
4. Keep source ephemerides unchanged in Moon-centred ICRF. Treat the rotating
   frame strictly as a presentation transform so filtering, interpolation, and
   provenance remain frame-independent.
5. Implement the transform in shared SatView logic so Vulkan and Metal consume
   identical scene coordinates.

## Validation

- Verify CAPSTONE's transformed seven-day path is continuous and substantially
  closer to periodic without adding an artificial closing chord.
- Verify the marker lies on the transformed path while time advances, pauses,
  seeks, and runs at high speed.
- Verify all visible lunar objects use the same selected frame.
- Test transform orthonormality, handedness, and stable behavior around epoch
  changes.
- Run Windows and macOS builds, SatView unit tests, render snapshots, and smoke
  tests.

## References

- NASA, *Near Rectilinear Halo Orbit Determination*:
  https://ntrs.nasa.gov/api/citations/20200011550/downloads/20200011550.pdf
- NASA CAPSTONE overview:
  https://www.nasa.gov/smallspacecraft/capstone/

