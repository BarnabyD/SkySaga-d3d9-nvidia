/*
 * d3d9.dll proxy for SkySaga 36731: floating-point crash fix only.
 *
 * The client unmasks floating-point divide-by-zero (_controlfp(0, _EM_ZERODIVIDE)
 * at 0x42DCD0, 0x6DCF40 and around resource loads at 0x48B790). The NVIDIA D3D9
 * driver then raises STATUS_FLOAT_MULTIPLE_TRAPS while the world loads and the
 * client crashes. ForceD3D9On12 avoided the crash but renders nothing.
 *
 * This proxy forwards every d3d9 export to the system d3d9.dll and installs a
 * vectored exception handler that masks floating-point exceptions in the faulting
 * thread's context and retries the instruction, which is what the hardware would
 * have done with the exceptions masked.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define EXPORTS(X) \
    X(0, Direct3DCreate9) \
    X(1, Direct3DCreate9Ex) \
    X(2, Direct3DCreate9On12) \
    X(3, Direct3DCreate9On12Ex) \
    X(4, Direct3DShaderValidatorCreate9) \
    X(5, D3DPERF_BeginEvent) \
    X(6, D3DPERF_EndEvent) \
    X(7, D3DPERF_GetStatus) \
    X(8, D3DPERF_QueryRepeatFrame) \
    X(9, D3DPERF_SetMarker) \
    X(10, D3DPERF_SetOptions) \
    X(11, D3DPERF_SetRegion) \
    X(12, DebugSetLevel) \
    X(13, DebugSetMute) \
    X(14, Direct3D9EnableMaximizedWindowedModeShim) \
    X(15, PSGPError) \
    X(16, PSGPSampleTexture)

#define EXPORT_COUNT 17

static FARPROC g_real[EXPORT_COUNT];
static HMODULE g_system_d3d9;
static volatile LONG g_masked;
static char g_log_path[MAX_PATH];

#define NAME_ENTRY(i, name) #name,
static const char *g_names[EXPORT_COUNT] = { EXPORTS(NAME_ENTRY) };

/* Each export jumps straight to the system implementation. */
#define THUNK(i, name) \
    __declspec(naked) void name##_thunk(void) { __asm { jmp dword ptr [g_real + i * 4] } }
EXPORTS(THUNK)

static void log_line(const char *text)
{
    FILE *file = NULL;
    if (fopen_s(&file, g_log_path, "a") == 0 && file) {
        fputs(text, file);
        fputc('\n', file);
        fclose(file);
    }
}

static int is_float_exception(DWORD code)
{
    switch (code) {
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
    case 0xC00002B4: /* STATUS_FLOAT_MULTIPLE_FAULTS (SSE) */
    case 0xC00002B5: /* STATUS_FLOAT_MULTIPLE_TRAPS (SSE) */
        return 1;
    }
    return 0;
}

static LONG CALLBACK mask_float_exceptions(PEXCEPTION_POINTERS info)
{
    PCONTEXT context = info->ContextRecord;
    DWORD code = info->ExceptionRecord->ExceptionCode;

    if (!is_float_exception(code)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    /* x87: mask all six exceptions and clear the pending flags. */
    context->FloatSave.ControlWord |= 0x3F;
    context->FloatSave.StatusWord &= ~0xFFu;

    /* SSE: MXCSR sits at offset 24 of the FXSAVE area. Mask all, clear flags. */
    if ((context->ContextFlags & CONTEXT_EXTENDED_REGISTERS) == CONTEXT_EXTENDED_REGISTERS) {
        DWORD *mxcsr = (DWORD *)(context->ExtendedRegisters + 24);
        *mxcsr = (*mxcsr | 0x1F80) & ~0x3Fu;
    }

    if (InterlockedIncrement(&g_masked) <= 20) {
        char line[128];
        sprintf_s(line, sizeof(line), "masked fp exception 0x%08lX at %p thread %lu",
                  code, info->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
        log_line(line);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void load_system_d3d9(void)
{
    char path[MAX_PATH];
    UINT length = GetSystemDirectoryA(path, MAX_PATH);
    int i;

    if (length == 0 || length + 10 >= MAX_PATH) {
        return;
    }
    strcat_s(path, sizeof(path), "\\d3d9.dll");
    g_system_d3d9 = LoadLibraryA(path);
    if (!g_system_d3d9) {
        log_line("could not load the system d3d9.dll");
        return;
    }
    for (i = 0; i < EXPORT_COUNT; i++) {
        g_real[i] = GetProcAddress(g_system_d3d9, g_names[i]);
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        GetModuleFileNameA(instance, g_log_path, MAX_PATH);
        strcpy_s(strrchr(g_log_path, '\\') + 1,
                 sizeof(g_log_path) - (strrchr(g_log_path, '\\') + 1 - g_log_path),
                 "d3d9_fpu.log");
        load_system_d3d9();
        AddVectoredExceptionHandler(1, mask_float_exceptions);
        log_line("d3d9 fpu proxy (fp fix only) loaded");
    }
    return TRUE;
}
