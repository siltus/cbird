# Copilot Instructions for cbird

## Repository Policy

- **Never** push to, create PRs against, or modify the upstream repo (`scrubbbbs/cbird`).
- All changes must be made only in our fork (`siltus/cbird`).

## Workflow

- **Always** build and test before committing any code changes.

## Build System

cbird is a Qt6 C++ application cross-compiled for Windows using MXE inside Docker.

### Build (compile only)
```powershell
docker run --rm -v "D:\src\cbird:/src/cbird" -w /src/cbird mxe:latest bash -c 'source /build/build.env; export PATH="$MXE_DIR/usr/$MXE_TARGET/qt6/bin:$MXE_DIR/usr/bin:$PATH"; make -j$(nproc)'
```

### Build + Package
```powershell
docker run --rm -v "D:\src\cbird:/src/cbird" -w /src/cbird mxe:latest bash -c 'source /build/build.env; export PATH="$MXE_DIR/usr/$MXE_TARGET/qt6/bin:$MXE_DIR/usr/bin:$PATH"; make -j$(nproc) && make install'
```

### Run
```
D:\src\cbird\_win32\cbird-win\cbird.exe
```

### Regenerate Makefile (after .pro changes)
```powershell
docker run --rm -v "D:\src\cbird:/src/cbird" -w /src/cbird mxe:latest bash -c 'source /build/build.env; export PATH="$MXE_DIR/usr/$MXE_TARGET/qt6/bin:$MXE_DIR/usr/bin:$PATH"; qmake && make -j$(nproc)'
```

### Notes
- Docker Desktop must be running before building.
- The `strip` step during packaging can be slow if DLLs are locked by a running cbird instance.
- `ffplay.exe` missing during packaging is a known benign error.

## Architecture Overview

- **Build**: qmake `.pro` files, cross-compiled via MXE in Docker (`docker/Dockerfile.mxe`)
- **Database**: SQLite via Qt SQL, files in `<root>/_index/media0.db`
- **Path model**: Media paths stored **relative** to DB root in SQLite; reconstructed to absolute in memory via `Database::toRealPath()` / `Database::toVirtualPath()`. Additional source directories use `_sources/<name>/` prefix in DB.
- **Scanning**: `Scanner::scanDirectory()` walks a single directory tree. `Engine::update()` calls it once per source directory.
- **GUI**: Qt Widgets (`src/gui/`)
- **Index types**: DCT hash, DCT features, CV features, DCT video, Color descriptor — each has its own `Index` subclass and may use a separate `.db` file.
- **Video index**: `.vdx` files stored in `_index/video/`, keyed by media ID (not path).

## Key Conventions

- Media objects hold **absolute paths** in memory; the DB stores **virtual relative paths**.
- `_index/` is a reserved directory name (`INDEX_DIRNAME` macro in `global.h`).
- Analysis items are fake Media objects with paths ending in `***` (e.g., `-diff***`). Must not be sorted with real items.
- `PropertyCompare` in `media.cpp` supports `^` prefix for descending sort.
- Qt JSON (`QJsonDocument`, `QJsonArray`, `QJsonObject`) is used for config files.
- `PROGRESS_LOGGER` macro is used for rate-limited progress output.
- `SQL_FATAL` macro is used for fatal SQL errors.
- `qMessageLogSetRootPath()` sets the root path for log message path elision.
