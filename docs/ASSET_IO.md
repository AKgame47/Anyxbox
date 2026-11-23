# Explicit launcher asset access

`run_exe --i-built-this sample.exe --asset-root DIR` gives the translated file API read-only access to regular files
under an existing host-selected directory. With no root, file opens fail. No root is inferred from the executable path
or the working directory. The caller can also pass `RunOptions::asset_root` to `run_pe`.

This slice supports CreateFileA with GENERIC_READ/OPEN_EXISTING, ReadFile, SetFilePointer, GetFileSize and CloseHandle.
Write/create opens through the launcher's asset root fail. There is no launcher save-root option yet.
Leading slashes and backslashes mean root-relative paths, not host absolute paths. Parent components, colons/drive paths,
empty paths and embedded NULs are rejected. Each guest path component is opened relative to an owned directory descriptor;
symlinks in guest components and non-regular final files are rejected. Read-only root itself must be a non-symlink directory.
Intermediate guest directories are never created in read-only mode.

This is not an OS sandbox. Guest machine code still runs with the host's privileges. Only run trusted, self-built programs.
The host selects and manages the root, its ancestors, mounts, hard links and contents. Those remain trusted configuration;
this routing layer does not isolate arbitrary native syscalls or promise resistance to a hostile host changing filesystem topology.

Existing manually configured writable MappedFolder callers keep their three older file modes. OPEN_ALWAYS still routes to
append in kernel32 and is not a complete Windows implementation. File sharing, asynchronous I/O, Unicode file names,
all creation dispositions and precise last-error behavior are not claimed by this slice.

Tests use only synthetic temporary files/directories. They verify lexical traversal, component/final symlink denial,
FIFO rejection without blocking, read-only write denial, file data/size/seek, default launcher denial, opt-in read success,
truncating-write rejection and invalid root failure. CI separately builds asset_read.c with MinGW and tests the actual CLI
with and without the root. Independent CI results are required before calling that binary path verified.
