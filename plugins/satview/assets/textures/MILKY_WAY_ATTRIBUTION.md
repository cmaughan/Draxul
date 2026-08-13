# Milky Way Texture Attribution

`milky_way_nasa_4k.jpg` is a 4096x2048 sRGB JPEG conversion of
`milkyway_2020_4k.exr` from NASA Scientific Visualization Studio's Deep Star
Maps 2020:

https://svs.gsfc.nasa.gov/4851/

The source is a plate carree full-sky Milky Way background in ICRF/J2000
celestial coordinates, centered at 0 hours right ascension with right
ascension increasing to the left. Bright Hipparcos and Tycho stars are omitted
from the source so SatView can render its own magnitude-aware Hipparcos layer
without duplication.

The source linear half-float OpenEXR was transformed to the sRGB transfer
function and encoded as a full-resolution 4:4:4 JPEG. It was not cropped,
rotated, or resized. The reproducible conversion is in
`scripts/build_satview_milky_way_texture.py`.

Credit: NASA/Goddard Space Flight Center Scientific Visualization Studio. Gaia
DR2 data: ESA/Gaia/DPAC.
