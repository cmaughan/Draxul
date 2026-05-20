# WI 125 — Overlay Host Registry (Data-Driven Overlay Management)

**Type:** Refactor  
**Severity:** Medium (maintainability; every new overlay currently needs broad app.cpp edits)  
**Source:** Gemini review, GPT review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`app/app.cpp` hardwires four overlay hosts (`chrome_host_`, `palette_host_`, `toast_host_`, `diagnostics_host_`) individually through:
- `initialize()` — each is init'd in a specific order
- Render-tree assembly — each is inserted at a specific z-position
- Viewport update — each is notified separately
- Diagnostics — each is queried individually
- `shutdown()` — each is shut down in reverse order

From Gemini: "Every new overlay now means broad app-layer edits and merge conflicts. Overlay management is not data-driven." (L323, L1079, L1220, L1386 cited.)

Adding a 5th overlay (e.g. a file-picker overlay, a notification center) currently requires touching all of these locations.

---

## Proposed Design

Introduce an `OverlayRegistry` that manages an ordered list of `IOverlayHost` (or similar thin interface):

```cpp
struct IOverlayHost {
    virtual void initialize(OverlayDeps&) = 0;
    virtual void shutdown() = 0;
    virtual void update_viewport(Rect) = 0;
    virtual void render(IRenderContext&) = 0;
    virtual int z_order() const = 0;
    virtual bool consumes_input() const = 0;
    virtual DiagnosticsInfo diagnostics() const { return {}; }
};

class OverlayRegistry {
    std::vector<std::unique_ptr<IOverlayHost>> overlays_;  // sorted by z_order
public:
    void add(std::unique_ptr<IOverlayHost>);
    void initialize_all(OverlayDeps&);
    void shutdown_all();
    void update_viewports(Rect);
    void render_all(IRenderContext&);
    // ...
};
```

`App` registers overlays in one place at startup; the registry handles the rest.

---

## Investigation Steps

- [ ] Read `app/app.cpp` — enumerate every location each overlay host is touched
- [ ] Read `app/chrome_host.h`, `app/toast_host.h`, `app/command_palette_host.h`, `app/diagnostics_host.h` to understand their init/shutdown contracts
- [ ] Design the minimal `IOverlayHost` interface that covers all four existing overlays
- [ ] Prototype the registry with two overlays first to validate the interface

---

## Acceptance Criteria

- [ ] Adding a new overlay requires only: implementing `IOverlayHost`, registering in one place
- [ ] All four existing overlays managed through the registry with no behaviour change
- [ ] **WI 119** (ChromeHost tests), **WI 120** (ToastHost tests), **WI 121** (render-tree ordering tests) pass throughout the refactor
- [ ] CI green

---

## Interdependencies

- **Depends on WI 119, WI 120, WI 121** (tests must exist before this refactor lands)
- Benefits **WI 128** (tab rename), **WI 132** (distraction-free mode) which may need new overlays
- Subagent recommended: this is a large, multi-file refactor well-suited to a focused subagent
