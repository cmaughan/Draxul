# Solar-System Texture Attribution

## Planet Maps

The 2k Mercury, Venus atmosphere, Mars, Jupiter, Saturn, Uranus, and Neptune
maps are the high-resolution equirectangular textures published by Solar
System Scope:

https://www.solarsystemscope.com/textures/

They are based on NASA elevation and imagery data, with color tuning and
presentation gap filling by Solar System Scope. They are used under the
Creative Commons Attribution 4.0 International license:

https://creativecommons.org/licenses/by/4.0/

Attribution: Planet texture maps by Solar System Scope, based on NASA elevation
and imagery data, used under CC BY 4.0.

The Earth, Moon, and Sun maps retain their existing provenance described in
`../README.md`.

## USGS/PDS Major-Moon Mosaics

The following 1024-pixel-wide equirectangular browse products come from the
USGS Astrogeology Science Center's Lunar and Planetary Cartographic Catalog.
The source pages identify the products as public domain or PDS data; products
that request author citation retain that request below.

| Bundled file | Source product |
|---|---|
| `phobos_usgs_1k.jpg` | [Phobos Viking Global Mosaic 5m](https://astrogeology.usgs.gov/search/map/phobos_viking_global_mosaic_5m), Phil Stooke / PDS / USGS Astrogeology |
| `io_usgs_1k.jpg` | [Io Galileo SSI Global Color Merge Mosaic 1km](https://astrogeology.usgs.gov/search/map/io_galileo_ssi_global_color_merge_mosaic_1km), PDS / USGS Astrogeology |
| `europa_usgs_1k.jpg` | [Europa Voyager–Galileo SSI Global Mosaic 500m](https://astrogeology.usgs.gov/search/map/europa_voyager_galileo_ssi_global_mosaic_500m), Tammy Becker and listed originators / USGS Astrogeology |
| `ganymede_usgs_1k.jpg` | [Ganymede Voyager–Galileo SSI Color Global Mosaic 1.4km](https://astrogeology.usgs.gov/search/map/ganymede_voyager_galileo_ssi_color_global_mosaic_1_4km), USGS Astrogeology |
| `callisto_usgs_1k.jpg` | [Callisto Galileo/Voyager Global Mosaic 1km](https://astrogeology.usgs.gov/search/map/callisto_galileo_voyager_global_mosaic_1km), USGS Astrogeology |
| `enceladus_usgs_1k.jpg` | [Enceladus Cassini Global Mosaic 100m](https://astrogeology.usgs.gov/search/map/enceladus-cassini-global-mosaic-100m-schenk), Paul Schenk and William McKinnon / PDS |
| `titan_usgs_1k.jpg` | [Titan Cassini ISS Global Mosaic 4005m](https://astrogeology.usgs.gov/search/map/titan_cassini_iss_global_mosaic_4005m), Cassini ISS / PDS / USGS Astrogeology |
| `iapetus_usgs_1k.jpg` | [Iapetus Cassini–Voyager Global Mosaic 803m](https://astrogeology.usgs.gov/search/map/iapetus_cassini_voyager_global_mosaic_803m), Space Science Institute, Cassini Team, JPL / USGS Astrogeology |

The browse images were downloaded without geometric or color modification.
`plugins/satview/tools/build_satview_solar_system_textures.py` pins their source URLs and
SHA-256 digests.

## Procedural Presentation Maps

Complete, directly usable color mosaics are not available for every selected
moon. The following maps are deterministic presentation stand-ins generated
locally by `plugins/satview/tools/build_satview_procedural_moon_textures.py`:

- Deimos
- Mimas
- Tethys
- Dione
- Rhea
- Miranda
- Ariel
- Umbriel
- Titania
- Oberon
- Triton

These files are deliberately named `*_procedural_2k.jpg`. They approximate
the bodies' broad colors and cratered or icy character but are not scientific
surface products and must not be interpreted as mapped geography.
