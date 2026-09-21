# Validate every part before slicing a score window

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/scoreview/product/draxul-score-learn/src/source_slicer.cpp:256` validates against the first part’s bar count, then indexes every part at `:286` and `:314`. A shorter subsequent part causes out-of-bounds access.

**Investigation**

- [ ] Construct multipart scores with shorter and empty later parts.

**Fix strategy**

- [ ] Validate each requested measure and attribute-state index per part.
- [ ] Reject incompatible windows or define an explicit safe missing-measure policy.

**Acceptance criteria**

- [ ] Unequal part lengths return a controlled result without invalid access.
- [ ] Equal-length multipart windows preserve their notation and attribute state.
