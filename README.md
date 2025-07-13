# Memory Scanner for Windows

This is a C++ API to perform very simple memory scans of processes on
Windows, similar to Cheat Engine, but lacks the ability write process
memory.

## API Usage

See [example.cpp](./src/example.cpp) for fully fleshed out example of
scanning and reading an int32 from a process. The comments in
[memory_scanner.hpp](./src/memory_scanner.hpp) also has documentation
on these APIs.

1. Obtain a HANDLE to a process using a win32 API call like
[OpenProcess] or [CreateProcess].

2. Call `memory_scanner::InitialScan`. This returns a vector of
MemoryRegions containing a copy of the process's memory as reported by
[VirtualQueryEx] and [ReadProcessMemory].

3. Call `memory_scanner::NextScan` with these MemoryRegions and an
`std::function` to perform the filter. The signature of the filter is
`bool (const T &prev, const T &current)` to compare the initial
original value from InitialScan to the current value. The function
should return true if you want to keep this value or false if you want
to discard it. The return value of `memory_scanner::NextScan` is the
list of addresses in the remote process that passed your filter
function. The MemoryRegions vector is updated with the newly read
memory and regions without any valid addresses are pruned.

4. If you want to perform even more scans to refine the valid
addresses, then you can call the overload of
`memory_scanner::NextScan` with the provided list of valid addresses
which will only check those spots. The return type of this overload is
void, but both the MemoryRegions and valid addresses will be pruned as
an in-out parameter depending on the filter function.

All the pointers returned are of type `memory_scanner::IntPtr`, which
is an alias for [ULONG_PTR]. A convenience class
`memory_scanner::MemoryObject<T>` can be used with these `IntPtr`s to
read the current value in the remote process.


[CreateProcess]: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa
[GetLastError]: https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror
[OpenProcess]: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocess
[ReadProcessMemory]: https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-readprocessmemory
[ULONG_PTR]: https://learn.microsoft.com/en-us/windows/win32/winprog/windows-data-types
[VirtualQueryEx]: https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex

## Error Handling

Errors are reported back via exceptions of type
`memory_scanner::MemoryScannerException`. It contains an extra field
for the windows error code as reported by [GetLastError].

## Compiling

This code is small enough that it would be easiest to use by just
including the four files in ./src/ within your existing project.

The CMake structure here is just to compile the example.
