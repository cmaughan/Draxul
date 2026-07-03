# SatView Earth Textures

The Earth texture set in `textures/` comes from Solar System Scope:

https://www.solarsystemscope.com/textures/

Files:

- `earth_day_8k.jpg` from `8k_earth_daymap.jpg`
- `earth_night_8k.jpg` from `8k_earth_nightmap.jpg`
- `earth_clouds_8k.jpg` from `8k_earth_clouds.jpg`

Solar System Scope publishes these textures under the Creative Commons
Attribution 4.0 International license:

https://creativecommons.org/licenses/by/4.0/

Attribution:

Earth texture maps by Solar System Scope, based on NASA elevation and imagery
data, used under CC BY 4.0.

## Moon Texture

`textures/moon_lroc_8k.jpg` is an 8192x4096 high-quality JPEG conversion of
NASA Scientific Visualization Studio's `lroc_color_poles_8k.tif` from the CGI
Moon Kit:

https://svs.gsfc.nasa.gov/4720/

The map is centered on 0 degrees longitude and was adapted from the Lunar
Reconnaissance Orbiter Camera Hapke-normalized WAC color mosaic assembled from
more than 100,000 Wide Angle Camera images. The source TIFF was converted to
JPEG quality 93 without resizing.

Credit: NASA's Scientific Visualization Studio; Ernie Wright (USRA),
visualizer; Noah Petro (NASA/GSFC), scientist; LROC WAC data from the Lunar
Reconnaissance Orbiter Camera team at Arizona State University.

## Sun Texture

`textures/sun_solar_system_scope_4k.jpg` is the 4096x2048 equirectangular Sun
surface texture published by Solar System Scope. It is based on NASA imagery,
with color saturation and gap filling intended for visual presentation:

https://commons.wikimedia.org/wiki/File:Solarsystemscope_texture_8k_sun.jpg

Solar System Scope publishes the texture under the Creative Commons
Attribution 4.0 International license:

https://creativecommons.org/licenses/by/4.0/

Attribution: Sun texture map by Solar System Scope, based on NASA imagery,
used under CC BY 4.0.

## Live Cloud Map

At runtime SatView asynchronously downloads the latest 8192x4096 greyscale
cloud map from the Live Cloud Maps project and caches it for three hours. The
bundled cloud texture remains the default; enable `Realistic clouds` in the
SatView panel to select the downloaded map. Either map is rendered on a
separate shell approximately 9.5 km above the normalized Earth surface:

https://clouds.matteason.co.uk/images/8192x4096/clouds.jpg

https://github.com/matteason/live-cloud-maps

The downloaded image is used under CC0 1.0. Its EUMETSAT-derived source data
requires this attribution, which is also displayed in the SatView panel:

Contains modified EUMETSAT data

When the service or network is unavailable, SatView keeps using the bundled
`earth_clouds_8k.jpg` texture.

## Star Catalog

`catalog/stars.dxstar` is a compact binary catalog generated from the Hipparcos
main catalog (`I/239/hip_main`) through VizieR:

https://cdsarc.cds.unistra.fr/viz-bin/cat/I/239

The file contains the 100,000 brightest usable Hipparcos records sorted by
visual magnitude. Each record stores a render-space unit direction, source
visual magnitude, saturated display color, and tiny-quad screen size. SatView
defaults to drawing stars with apparent visual magnitudes from -1.5 through 6.0.
The UI exposes minimum and maximum apparent-magnitude controls; records outside
that range are not uploaded for drawing, and stars inside it are scaled across
the selected range. A separate brightness scalar multiplies the resulting
starfield without changing which stars are included.

## HDR Rendering

SatView samples the Earth, Moon, Sun, and cloud color maps as sRGB textures and
renders the complete scene in linear light to an `RGBA16F` target. The scene
uses the highest supported common color/depth MSAA mode in the order 4x, 2x,
then 1x, resolves before tone mapping, applies the ACES curve using the saved
`Exposure` and `White point` controls, and lets the final sRGB attachment encode
the display image. `Star brightness` remains a star-only linear-light gain;
exposure affects the whole scene.

Enable `HDR buffer debug` in the SatView panel to open the `SatView HDR
Buffers` window. It reports the active MSAA fallback and displays a sample
difference heat map, the resolved HDR image, and the final tone-mapped image.

Regenerate it from the repository root with:

```powershell
python scripts\build_satview_star_catalog.py --max-stars 100000
```

## Sample Catalog

`catalog/sample_gp.json` is a tiny synthetic fixture using the CelesTrak GP JSON
field names. It is not downloaded satellite data; it exists so SatView can
exercise the catalog parser and status path deterministically while offline.
