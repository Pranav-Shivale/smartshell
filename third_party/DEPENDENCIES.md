# SmartShell Third-Party Dependencies

## CivetWeb

- **Version**: v1.16
- **Repository**: https://github.com/civetweb/civetweb
- **License**: MIT License (see `civetweb/LICENSE.md`)
- **Files included**: `civetweb.c`, `civetweb.h`
- **Purpose**: Embedded C HTTP/WebSocket server library.
- **Pinned at**: Git tag `v1.16` from upstream repository.

## cJSON

- **Version**: v1.7.19
- **Repository**: https://github.com/DaveGamble/cJSON
- **License**: MIT License (see `cjson/LICENSE`)
- **Files included**: `cJSON.c`, `cJSON.h`
- **Purpose**: Lightweight JSON parser and generator for C.
- **Pinned at**: Git tag `v1.7.19` from upstream repository.

## Policy

- Dependencies are vendored (source included) to avoid runtime download requirements.
- Versions are explicitly pinned. Updates require manual review and version bump.
- No dependencies are downloaded during normal `make` builds.

