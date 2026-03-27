# Third-party vendored headers

Run `scripts/fetch_deps.sh` to populate this directory.

| Library        | Version | Purpose                        | URL                                               |
|----------------|---------|--------------------------------|---------------------------------------------------|
| nlohmann/json  | 3.11.3  | JSON serialisation (REST API)  | https://github.com/nlohmann/json                  |
| cpp-httplib    | 0.15.3  | Embedded HTTP server (REST API)| https://github.com/yhirose/cpp-httplib            |
| spdlog         | 1.13.0  | Structured logging             | https://github.com/gabime/spdlog                  |

All three are header-only and have permissive licences (MIT).
Do not commit the downloaded files — they are populated at build time.
