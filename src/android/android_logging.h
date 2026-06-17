#pragma once

#if defined(__ANDROID__)

namespace zelda64::android {
void append_log(const char* format, ...);
void install_crash_handlers();
void redirect_stdio_to_log();
void write_crash_file(const char* message);
}

#endif
