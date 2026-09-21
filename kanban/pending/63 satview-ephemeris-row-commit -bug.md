# Commit ephemeris metadata only after accepting a row

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/core/satview_catalog.cpp:1353` updates source and frame before validating the frame and track horizon. A rejected final row can overwrite metadata describing previously accepted samples.

**Investigation**

- [ ] Append invalid-frame and invalid-horizon rows after valid samples for the same object.

**Fix strategy**

- [ ] Parse row metadata into temporary values.
- [ ] Commit metadata and sample data together only after complete validation.

**Acceptance criteria**

- [ ] Rejected rows leave accepted samples and their metadata unchanged.
- [ ] Valid rows retain intended source, frame, and horizon values.
- [ ] Define consistent handling for conflicting accepted frames.
