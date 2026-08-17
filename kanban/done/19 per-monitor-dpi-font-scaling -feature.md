# Per-monitor DPI font scaling

**Type:** feature
**Disposition:** Implemented

`App::on_display_scale_changed()` reinitializes text metrics at the new display PPI
and reapplies renderer, input, and layout metrics without rewriting the configured
font size. `tests/dpi_scaling_tests.cpp` and
`tests/dpi_hotplug_integration_tests.cpp` cover 1x/1.5x/2x changes, rapid changes,
cell geometry, and callback delivery.
