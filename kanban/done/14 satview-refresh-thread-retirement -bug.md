# Retire completed refresh threads before replacement

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/services/satview_catalog_service.cpp:602` and `satview_cloud_service.cpp:233` assign new threads after checking only `refresh_in_flight_`. Completion between pump and start checks clears that flag while `worker_` remains joinable, causing `std::terminate`.

**Investigation**

- [x] Force completion between result collection and the next refresh decision in both services.

**Fix strategy**

- [x] Model completed-but-unjoined workers explicitly.
- [x] Consume completion and join the previous worker before replacing it.

**Acceptance criteria**

- [x] Automatic and manual refresh races cannot replace a joinable thread.
- [x] Completion results are applied once, and cancellation remains responsive.
