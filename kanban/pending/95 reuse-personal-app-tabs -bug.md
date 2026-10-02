# Reuse Personal Assistant and Flashcards tabs

**Priority:** P2
**Summary:** Reopening Personal Assistant or Flashcards should focus the existing app tab instead of creating duplicates.

- [x] Reuse Personal Assistant tabs across Spaces and let Flashcards opt into generic plugin tab reuse.
- [x] Cover CLI/palette command routing, matching plugin identity/configuration, and unchanged multi-instance plugins.
- [ ] Configure this Mac's Personal Agents collection and Flashcards build source from the user's Dropbox locations.
- [x] Run the core/Flashcards aggregate and same-cache startup smoke; document behavior and validation.

Personal Assistant reads `[agents].personal_root` in the shared server. Flashcards currently embeds `DRAXUL_FLASHCARDS_VOCABULARY_SOURCE` at build time. Keep personal paths and generated personal deck data out of Git.

## Implementation and validation (2026-10-02)

- The shared server returns an existing Personal Assistant tab across Spaces. The Personal Agents rail also searches all Spaces and selects the requested definition.
- Plugin manifests can opt into tab reuse. Flashcards opts in; matching uses plugin ID and semantic JSON configuration. GUI startup (`--plugin`) and command-palette launches carry that policy. Other plugins and differing configurations retain independent tabs; explicit splits remain separate.
- Core/protocol tests cover reuse and manifest validation; app tests verify palette focus and startup tab selection across Spaces.
- `python3 do.py test debug --flashcards`: all 59 CTest entries passed (38.76 seconds; build plus tests 51.15 seconds), including Flashcards rendering. The initial run exposed missing Personal Assistant palette metadata in the app test harness; registered the metadata and reran successfully.
- `python3 do.py smoke --skip-build`: passed (1.31 seconds), same Debug cache.
- Live isolated-server verification: two `--host personal-assistant --smoke-test` launches retained `tab-6`; two `--plugin dev.draxul.flashcards --smoke-test` launches retained `tab-9`. No duplicate app panes. Shut down only that isolated server afterward.
- The existing user server was left untouched and needs restart to use the updated tab-reuse behavior.

- Before pushing, integrated the incoming bidirectional Flashcards changes (`2862364`, adopted by root `78209816`) and reran the core/Flashcards aggregate: all 59 entries passed in 40.26 seconds. The same-cache startup smoke passed again.

## Remaining local configuration

Awaiting the user's Personal Agents collection directory and Flashcards vocabulary JSON path. No Dropbox directory was found in the home directory or `~/Library/CloudStorage`, and no Dropbox account-location metadata exists here. Do not guess paths or replace the public fixture with personal data in tracked files. Once supplied, validate the collection/journal, set `[agents].personal_root` in the local Draxul config and `DRAXUL_FLASHCARDS_VOCABULARY_SOURCE` in the local build cache, rebuild Flashcards, and coordinate the server restart. This is why the card remains pending.
