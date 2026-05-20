# Understand Config

Human task list for learning how Draxul config behaves without re-deriving it from source every time.

## Checklist

- [ ] Confirm which `config.toml` the app is actually using on this machine.
- [ ] Separate top-level app settings from host-owned sections like `[mega_city_code]`.
- [ ] Practice the intended edit flow:
  - save with `:w`
  - run `reload_config`
  - verify the changed setting really applied
- [ ] Decide whether explicit reload is enough, or whether auto-reload still belongs on the roadmap.
- [ ] Write down the handful of settings you expect to tweak most often so they are easy to sanity-check after reloads.

## Notes

- `reload_config` is explicit; there is no file watcher yet.
- Top-level settings should live directly in `config.toml`, not inside unrelated tables.
- If a setting still refuses to update after `reload_config`, it is probably cached in a host-specific path and needs a targeted fix.
