# Mars Surface Objects

`mars_surface_objects.csv` is a curated offline v1 catalogue of Mars landing
sites. Coordinates are planetocentric, east-positive longitudes normalized to
the `[-180, 180]` range used by SatView surface markers.

The catalogue intentionally records stable landing-site positions, not live rover
traverse endpoints. Rows with `Rover landing site` source kinds should be read
as the initial landed site for that rover unless a later dynamic traverse source
is explicitly imported.

Coordinate and mission sources:

- Viking 1: NASA Technical Reports Server revised location,
  https://ntrs.nasa.gov/citations/19810041868
- Viking 2: NASA Science mission page,
  https://science.nasa.gov/mission/viking-2/
- Mars Pathfinder: PDS IMP EDR dataset description,
  https://planetarydata.jpl.nasa.gov/img/data/mpfl-m-imp-2-edr-v1.0/mpim_0001/document/dataset.htm
- Spirit and Opportunity target/landing context: JPL DESCANSO MER navigation
  summary,
  https://descanso.jpl.nasa.gov/DPSummary/DESCANSO_MER_NAV_051215C_low.pdf
- Opportunity orbital hardware localization: JPL image PIA21494,
  https://www.jpl.nasa.gov/images/pia21494-rovers-landing-hardware-at-eagle-crater-mars/
- Phoenix: JPL image PIA11202,
  https://www.jpl.nasa.gov/images/pia11202-phoenix-landing-site-indicated-on-global-view/
- Curiosity: JPL Mars Science Laboratory landing press kit,
  https://www.jpl.nasa.gov/news/press_kits/MSLLanding.pdf
- InSight: Golombek et al. 2020,
  https://doi.org/10.1029/2020EA001248
- Perseverance: NASA Technical Reports Server Mars 2020 EDL assessment,
  https://ntrs.nasa.gov/citations/20210024480
