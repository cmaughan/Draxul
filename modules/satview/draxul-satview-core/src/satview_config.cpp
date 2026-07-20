#include <draxul/satview/satview_config.h>

namespace draxul::satview
{

bool satview_planet_track_enabled(
    const SatViewPlanetTrackConfig& tracks,
    SatViewCameraPov body)
{
    switch (body)
    {
    case SatViewCameraPov::Mercury:
        return tracks.mercury;
    case SatViewCameraPov::Venus:
        return tracks.venus;
    case SatViewCameraPov::Earth:
        return tracks.earth;
    case SatViewCameraPov::Mars:
        return tracks.mars;
    case SatViewCameraPov::Jupiter:
        return tracks.jupiter;
    case SatViewCameraPov::Saturn:
        return tracks.saturn;
    case SatViewCameraPov::Uranus:
        return tracks.uranus;
    case SatViewCameraPov::Neptune:
        return tracks.neptune;
    case SatViewCameraPov::Moon:
    case SatViewCameraPov::Sun:
    case SatViewCameraPov::Phobos:
    case SatViewCameraPov::Deimos:
    case SatViewCameraPov::Io:
    case SatViewCameraPov::Europa:
    case SatViewCameraPov::Ganymede:
    case SatViewCameraPov::Callisto:
    case SatViewCameraPov::Mimas:
    case SatViewCameraPov::Enceladus:
    case SatViewCameraPov::Tethys:
    case SatViewCameraPov::Dione:
    case SatViewCameraPov::Rhea:
    case SatViewCameraPov::Titan:
    case SatViewCameraPov::Iapetus:
    case SatViewCameraPov::Miranda:
    case SatViewCameraPov::Ariel:
    case SatViewCameraPov::Umbriel:
    case SatViewCameraPov::Titania:
    case SatViewCameraPov::Oberon:
    case SatViewCameraPov::Triton:
        return false;
    }
    return false;
}

void satview_set_planet_track_enabled(
    SatViewPlanetTrackConfig& tracks,
    SatViewCameraPov body,
    bool enabled)
{
    switch (body)
    {
    case SatViewCameraPov::Mercury:
        tracks.mercury = enabled;
        return;
    case SatViewCameraPov::Venus:
        tracks.venus = enabled;
        return;
    case SatViewCameraPov::Earth:
        tracks.earth = enabled;
        return;
    case SatViewCameraPov::Mars:
        tracks.mars = enabled;
        return;
    case SatViewCameraPov::Jupiter:
        tracks.jupiter = enabled;
        return;
    case SatViewCameraPov::Saturn:
        tracks.saturn = enabled;
        return;
    case SatViewCameraPov::Uranus:
        tracks.uranus = enabled;
        return;
    case SatViewCameraPov::Neptune:
        tracks.neptune = enabled;
        return;
    case SatViewCameraPov::Moon:
    case SatViewCameraPov::Sun:
    case SatViewCameraPov::Phobos:
    case SatViewCameraPov::Deimos:
    case SatViewCameraPov::Io:
    case SatViewCameraPov::Europa:
    case SatViewCameraPov::Ganymede:
    case SatViewCameraPov::Callisto:
    case SatViewCameraPov::Mimas:
    case SatViewCameraPov::Enceladus:
    case SatViewCameraPov::Tethys:
    case SatViewCameraPov::Dione:
    case SatViewCameraPov::Rhea:
    case SatViewCameraPov::Titan:
    case SatViewCameraPov::Iapetus:
    case SatViewCameraPov::Miranda:
    case SatViewCameraPov::Ariel:
    case SatViewCameraPov::Umbriel:
    case SatViewCameraPov::Titania:
    case SatViewCameraPov::Oberon:
    case SatViewCameraPov::Triton:
        return;
    }
}

} // namespace draxul::satview
