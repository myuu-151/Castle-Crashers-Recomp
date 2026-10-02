#include "common/crash.h"

#ifdef _WIN32

#include <windows.h>

#include <dbghelp.h>

#include <cstdio>

namespace crash {

namespace {

LONG WINAPI on_crash(EXCEPTION_POINTERS* info) {
    std::fflush(stdout);
    std::fprintf(stderr, "crash: exception %08lx at %p\n", info->ExceptionRecord->ExceptionCode,
                 info->ExceptionRecord->ExceptionAddress);
    HANDLE process = GetCurrentProcess(), thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame = {};
    DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 40; depth++) {
        if (!StackWalk64(machine, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) ||
            !frame.AddrPC.Offset)
            break;
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 offset = 0;
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD line_offset = 0;
        const char* name = SymFromAddr(process, frame.AddrPC.Offset, &offset, symbol) ? symbol->Name : "?";
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_offset, &line))
            std::fprintf(stderr, "  %s  %s:%lu\n", name, line.FileName, line.LineNumber);
        else
            std::fprintf(stderr, "  %s  +0x%llx\n", name, static_cast<unsigned long long>(offset));
    }
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void install() { SetUnhandledExceptionFilter(&on_crash); }

}  // namespace crash

#else

namespace crash {
void install() {}
}  // namespace crash

#endif
