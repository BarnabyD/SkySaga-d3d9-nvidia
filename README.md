# SkySaga d3d9 NVIDIA crash fix

A drop-in `d3d9.dll` proxy that stops the SkySaga client (build 36731) from crashing while the world loads on NVIDIA GPUs.

## The problem

The SkySaga client unmasks the floating-point divide-by-zero exception (`_controlfp(0, _EM_ZERODIVIDE)`) in several places: at `0x42DCD0`, at `0x6DCF40`, and around resource loads at `0x48B790`. Most code expects these exceptions to stay masked. The NVIDIA Direct3D 9 driver does too. While the world loads, the driver's floating-point code raises `STATUS_FLOAT_MULTIPLE_TRAPS`. Nothing handles it, so the client crashes.

Forcing D3D9On12 (`ForceD3D9On12`) avoids the crash, but the game then renders nothing.

## What the fix does

`d3d9.dll` is a thin proxy that sits next to the game executable:

1. **It forwards every export.** All 17 `d3d9.dll` exports (`Direct3DCreate9`, `Direct3DCreate9Ex`, `D3DPERF_*` and the rest) jump straight to the real `d3d9.dll` in the Windows system directory. Rendering itself is unchanged.
2. **It masks floating-point exceptions when they fire.** The proxy installs a vectored exception handler. When any floating-point exception reaches it (x87 or SSE, including `STATUS_FLOAT_MULTIPLE_TRAPS`/`FAULTS`), the handler:
   - masks all x87 exceptions and clears the pending x87 status flags
   - masks all SSE exceptions in `MXCSR` and clears its flags
   - retries the faulting instruction

   With the exceptions masked, the CPU produces the default result, such as infinity or NaN, instead of trapping. That is what would have happened if the game had never unmasked them.

Non-floating-point exceptions pass through untouched.

### Log file

The proxy writes `d3d9_fpu.log` next to itself. The file records that the proxy loaded and the first 20 floating-point exceptions it masked, with the exception code, address and thread ID. If the file exists and lists masked exceptions, the fix is working.

## Installing

1. Get `d3d9.dll`: build it (see below) or use a prebuilt copy.
2. Copy `d3d9.dll` into the SkySaga `Client` directory, next to the game executable.
3. Start the game as usual.

To uninstall, delete `d3d9.dll` (and `d3d9_fpu.log`) from the `Client` directory.

> Don't copy this DLL into `System32`/`SysWOW64`. It loads the real `d3d9.dll` from the system directory and has to live in the game folder.

## Building

Requirements:

- Windows
- Visual Studio 2022 Community with the **Desktop development with C++** workload, including the x86 build tools

Run:

```bat
build.bat
```

The script calls `vcvars32.bat` to set up the 32-bit MSVC environment, then compiles `d3d9_fpu.c` with the export list in `d3d9.def` and writes `d3d9.dll` to the repository folder.

If Visual Studio is installed somewhere else (a different edition, or not on `C:`), edit the `VCVARS` path at the top of `build.bat`.

The DLL must be 32-bit to match the SkySaga client. The export thunks use MSVC inline x86 assembly, so the code only builds with MSVC targeting x86.

## Files

| File | Purpose |
| --- | --- |
| `d3d9_fpu.c` | Proxy source: export thunks, system `d3d9.dll` loader, floating-point exception handler, logging |
| `d3d9.def` | Maps the real `d3d9` export names to the proxy's thunks |
| `build.bat` | Builds `d3d9.dll` with the Visual Studio 2022 x86 toolchain |
