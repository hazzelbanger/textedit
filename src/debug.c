
#include "debug.h"
#include <windows.h>

void MyDebugOutput(const wchar_t  *format, ...)
{
    wchar_t  buffer[512] = {0};
    va_list args;
    
    va_start(args, format);
    _vsnwprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t ), _TRUNCATE, format, args);
    va_end(args);
    
    OutputDebugString(buffer);
}