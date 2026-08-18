# ToastHost lifecycle coverage

**Type:** test
**Disposition:** Covered

`tests/toast_host_tests.cpp` covers stacking, the intentional no-cap policy, expiry,
fade, frame requests, cross-thread deadlines, pre-initialization replay, null handles,
and the configuration gate. This supersedes the stale max-stack-eviction assumption.
