# Unit Tests

## Building (MXE cross-compile for Windows)

Build a single test from the Docker MXE container:

```bash
docker run --rm -v "D:\src\cbird:/src/cbird" -w /src/cbird/unit mxe:latest bash -c '
  source /build/build.env
  export PATH="$MXE_DIR/usr/$MXE_TARGET/qt6/bin:$MXE_DIR/usr/bin:$PATH"
  $MXE_TARGET-qt6-qmake -o testbatchremove.pro.make testbatchremove.pro \
    && make -f testbatchremove.pro.make -j8
'
```

Build all tests:

```bash
docker run --rm -v "D:\src\cbird:/src/cbird" -w /src/cbird/unit mxe:latest bash -c '
  source /build/build.env
  export PATH="$MXE_DIR/usr/$MXE_TARGET/qt6/bin:$MXE_DIR/usr/bin:$PATH"
  for pro in test*.pro; do
    $MXE_TARGET-qt6-qmake -o "$pro.make" "$pro" && make -f "$pro.make" -j8 || exit 1
  done
'
```

## Running on Windows (native)

Tests that don't require `TEST_DATA_DIR` (e.g. `testbatchremove`) can run
natively on Windows. The test exe needs Qt runtime DLLs.

### One-time setup: copy missing DLLs

The packaged `_win32/cbird-win/` directory has most Qt DLLs but is missing
`Qt6Test.dll` and the SQLite driver plugin. Copy them from the MXE toolchain:

```bash
docker run --rm -v "D:\src\cbird:/src/cbird" mxe:latest bash -c '
  source /build/build.env
  cp $MXE_DIR/usr/$MXE_TARGET/qt6/bin/Qt6Test.dll /src/cbird/_win32/cbird-win/
  mkdir -p /src/cbird/_win32/cbird-win/sqldrivers
  cp $MXE_DIR/usr/$MXE_TARGET/qt6/plugins/sqldrivers/qsqlite.dll \
     /src/cbird/_win32/cbird-win/sqldrivers/
'
```

### Run

Copy the test exe into `_win32/cbird-win/` (which has the DLLs) and run:

```powershell
Copy-Item unit\runtest-testbatchremove.exe _win32\cbird-win\ -Force
& _win32\cbird-win\runtest-testbatchremove.exe
```

## Running via unit.sh (Linux / wine)

The existing `unit.sh` script builds and runs all tests. It requires
`TEST_DATA_DIR` to point to a directory with test images (used by index tests).

```bash
# Linux native
export TEST_DATA_DIR=/path/to/test-data
./unit.sh

# Single test
./unit.sh -run testbatchremove

# MXE + wine (requires wine64 in PATH)
source /build/build.env
./unit.sh -mxe -run testbatchremove
```

**Note:** The MXE Docker container ships 32-bit wine only; `wine64` is not
available, so `-mxe` mode does not work there. Use the Windows-native
approach above instead.
