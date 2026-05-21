#ifndef SYSTEM_PLATFORM_H
#define SYSTEM_PLATFORM_H

class PlatformInspector
{
    public:
        /// @brief Returns `true` if running as a standalone executable rather than a
        /// runtime-loaded plugin or DLL.
        static bool is_running_standalone();
};

#endif
