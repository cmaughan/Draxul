# Commit ephemeris metadata only after accepting a row

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/core/satview_catalog.cpp:1353` updates source and frame before validating the frame and track horizon. A rejected final row can overwrite metadata describing previously accepted samples.

**Investigation**

- [x] Append invalid-frame and invalid-horizon rows after valid samples for the same object.

**Fix strategy**

- [x] Parse row metadata into temporary values.
- [x] Commit metadata and sample data together only after complete validation.

**Acceptance criteria**

- [x] Rejected rows leave accepted samples and their metadata unchanged.
- [x] Valid rows retain intended source, frame, and horizon values.
- [x] Define consistent handling for conflicting accepted frames.
