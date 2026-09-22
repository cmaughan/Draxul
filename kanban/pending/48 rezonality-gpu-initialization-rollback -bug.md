# Roll back incomplete Rezonality GPU resource creation

**Severity:** CRITICAL; includes a MEDIUM resource leak  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/rezonality_plugin.cpp:1000` treats a non-null vertex buffer as ready after allocation/binding failure at `:1033`. Retrying a candidate can use an unbacked buffer. Separately, model-texture failure at `:1245` or `:1258` leaks resources held only by the local object at `:1522`.

**Investigation**

- [x] Inject vertex memory, bind/map, sampler, and upload allocation failures.

**Fix strategy**

- [x] Construct vertex and model-texture resources transactionally with scoped ownership.
- [x] Publish vertex-buffer readiness only after complete initialization and clear partial backend state.

**Acceptance criteria**

- [x] Failed candidates preserve the active generation.
- [x] Retrying succeeds without invalid buffer use or cumulative GPU leaks.

**Validation**

- [x] macOS Metal: focused rollback coverage passes 45 assertions; `python3 do.py test debug --rezonality` passes 29/29 CTest entries; same-cache smoke passes.
- [ ] Build the Vulkan backend and run the Rezonality suite on Windows or Linux CI; macOS cannot compile the changed Vulkan resource path.
