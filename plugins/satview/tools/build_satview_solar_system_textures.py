#!/usr/bin/env python3
"""Download and verify SatView's sourced planet and moon texture maps."""

from __future__ import annotations

import hashlib
from pathlib import Path
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets" / "satview" / "textures"

# filename: (source URL, pinned SHA-256)
SOURCES = {
    "mercury_solar_system_scope_2k.jpg": ("https://www.solarsystemscope.com/textures/download/2k_mercury.jpg", "5a5c80607f643496bac9a631e71957def35ed788895f18b678ac849c2b38e48a"),
    "venus_solar_system_scope_2k.jpg": ("https://www.solarsystemscope.com/textures/download/2k_venus_atmosphere.jpg", "225012ad4911730605c4e189ca2a3bf674fce50cc48aab4102b936b47d6991ac"),
    "mars_solar_system_scope_2k.jpg": ("https://www.solarsystemscope.com/textures/download/2k_mars.jpg", "2d187f3e77a98eaa8cea5f4cc722f633c122ef170b9e94ace6b5fb6cbc3f8e01"),
    "jupiter_solar_system_scope_2k.jpg": ("https://www.solarsystemscope.com/textures/download/2k_jupiter.jpg", "b0f04d005350252636b0e3396fc592548cbd9e9126b269d32d5c6abd4b0e4f2b"),
    "saturn_solar_system_scope_2k.jpg": ("https://www.solarsystemscope.com/textures/download/2k_saturn.jpg", "54a900ca9bf7ab62e70f862852759abdf342e6d6436a95a2fe9ebdb6bcd3bbac"),
    "uranus_solar_system_scope_2k.jpg": ("https://www.solarsystemscope.com/textures/download/2k_uranus.jpg", "d15239d46f82d3ea13d2b260b5b29b2a382f42f2916dae0694d0387b1204a09d"),
    "neptune_solar_system_scope_2k.jpg": ("https://www.solarsystemscope.com/textures/download/2k_neptune.jpg", "cb42ea82709741d28b0af44d8b283cbc6dbd0c521a7f0e1e1e010ade00977df6"),
    "io_usgs_1k.jpg": ("https://astrogeology.usgs.gov/ckan/dataset/0fc15885-24ee-4d9d-9666-11de0667c10c/resource/73d4c1f7-8c07-4b28-90ea-f47f7531c5ca/download/full.jpg", "d722a545c9290b33f3b57cb7ccb9e173b367a7a8fa4867e74a8e11538f274282"),
    "europa_usgs_1k.jpg": ("https://astrogeology.usgs.gov/ckan/dataset/4080036f-afc5-422e-abe9-1c0c8e4f98ea/resource/3647e7b3-425e-4dcf-951b-cc4a22fb0129/download/europa_voyager_galileossi_global_mosaic_500m_1024.jpg", "96453723e4dd7d7b709cbc097282b9292360908139c72a617b0d26d9ae9ce5f9"),
    "ganymede_usgs_1k.jpg": ("https://astrogeology.usgs.gov/ckan/dataset/e1422336-3291-4b65-b903-c942d53de073/resource/eb32abd7-fee2-47d1-9f96-9d7d8824cc3a/download/ganymede_voyager_galileossi_global_clrmosaic_1024.jpg", "6ebc2809a32743408c03be4255d82df7952df82a5c51dc1973ff8248101a28a7"),
    "callisto_usgs_1k.jpg": ("https://astrogeology.usgs.gov/ckan/dataset/a80abd68-7ed9-440e-829a-76376779164f/resource/ac628525-cb1c-4742-928b-5a0a60f372cd/download/callisto_voyager_galileossi_global_mosaic_1024.jpg", "ec3202dca0a0182c885f88f37a6ce484f344785e4c3135fc8c43d53f87e879c7"),
    "phobos_usgs_1k.jpg": ("https://astrogeology.usgs.gov/ckan/dataset/85290c2c-7524-44ba-9251-61ea69fcd9dd/resource/876ae3aa-ac7b-434c-86f4-a2e50cc9826d/download/phobos_viking_mosaic_dlrcontrol_1024.jpg", "d8a00068ac8e13821528d546b2e1d2c613e5225a22da04ad7d05bf3b727f3ce4"),
    "enceladus_usgs_1k.jpg": ("https://astrogeology.usgs.gov/ckan/dataset/64282c9f-ed8e-49cd-b7bb-e520d8a46908/resource/8ca2ddc0-0e54-4fda-9a27-7ccedee12fc9/download/enceladus_cassini_mosaic_global_100m_schenk2024_1024.jpg", "c8b656776c67189c6ae8ac94d392e4ea7bff4d9ab6dc7164e65164f1fc0a9079"),
    "titan_usgs_1k.jpg": ("https://astrogeology.usgs.gov/ckan/dataset/8ee17e4e-26c6-4e22-9c23-bc9a4c7ed35e/resource/c3f3006c-3174-4716-920f-44f5dc749a4a/download/titan_iss_p19658_mosaic_global_1024.jpg", "2c29bbe477f4decac65f1e8fd0b011680d41c9a36f2cfc32efe444dad733481f"),
    "iapetus_usgs_1k.jpg": ("https://astrogeology.usgs.gov/ckan/dataset/6ac8ecfb-36e7-4113-8d16-c92ba857c3d7/resource/141c2d1e-aa01-4e2f-969a-e46a581db4b9/download/full.jpg", "c8fae974a837a641769ca0e34efd9f5cf442804806b5e12f09f2080ce524cfd3"),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for filename, (url, expected) in SOURCES.items():
        target = OUTPUT / filename
        if target.exists() and sha256(target) == expected:
            print(f"verified {target.relative_to(ROOT)}")
            continue
        request = urllib.request.Request(
            url, headers={"User-Agent": "Mozilla/5.0 Draxul-SatView/1.0"}
        )
        temporary = target.with_suffix(target.suffix + ".download")
        with urllib.request.urlopen(request) as response, temporary.open("wb") as output:
            output.write(response.read())
        actual = sha256(temporary)
        if actual != expected:
            temporary.unlink(missing_ok=True)
            raise RuntimeError(f"SHA-256 mismatch for {filename}: {actual}")
        temporary.replace(target)
        print(f"downloaded {target.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
