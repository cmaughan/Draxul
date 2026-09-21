# Roll back incomplete Rezonality GPU resource creation

**Severity:** CRITICAL; includes a MEDIUM resource leak  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/rezonality_plugin.cpp:1000` treats a non-null vertex buffer as ready after allocation/binding failure at `:1033`. Retrying a candidate can use an unbacked buffer. Separately, model-texture failure at `:1245` or `:1258` leaks resources held only by the local object at `:1522`.

**Investigation**

- [ ] Inject vertex memory, bind/map, sampler, and upload allocation failures.

**Fix strategy**

- [x] Construct vertex and model-texture resources transactionally with scoped ownership.
- [x] Publish vertex-buffer readiness only after complete initialization and clear partial backend state.

**Acceptance criteria**

- [ ] Failed candidates preserve the active generation.
- [ ] Retrying succeeds without invalid buffer use or cumulative GPU leaks.
