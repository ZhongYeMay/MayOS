# Explorer HTTP follow-up

Continues the published 0.4.0-pre3 browser and boot-video work.

- HTTP request targets preserve query parameters and omit URL fragments.
- Addresses with only a query use `/` as the path.
- A Stop button cancels the Linux worker and reports cancellation.
- The ten-second request deadline uses monotonic time.
- Invalid port numbers and request-line control characters are rejected.

Transport tests exercise a real local HTTP server, query/fragment handling,
URL rejection and cancellation. Both Linux and freestanding GUI compilation
remain required. HTTPS, redirects and full HTML rendering are still pending.

The VERSION baseline now follows the published 0.4.0 line. Automatic prerelease
increments update VERSION, GUI, default-file labels and Linux os-release together.
Published pre3 ISO files and videos retain their original version and metadata.

## Navigation follow-up

The Linux preview now supports back, forward, reload, opening the session
bookmark, and Up/Down response scrolling. History retains eight requested URLs,
suppresses consecutive duplicates, and discards forward entries on a new branch.
These controls reload responses; they do not cache pages or implement HTML.
Source publishing uses GitHub Actions for asset uploads and allows the website
to keep pointing to the last verified ISO and video release. Existing binary
releases are retained as required by the current release verification policy.
