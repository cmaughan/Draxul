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

## Sample Catalog

`catalog/sample_gp.json` is a tiny synthetic fixture using the CelesTrak GP JSON
field names. It is not downloaded satellite data; it exists so SatView can
exercise the catalog parser and status path deterministically while offline.
