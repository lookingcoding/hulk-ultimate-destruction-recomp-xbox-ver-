#pragma once
#include <filesystem>

// Rasterizes the component's actual ImDrawData on the CPU. No window, GPU,
// game, audio device, or operating-system input is created.
struct ImVec2;
void CaptureControlsPng(const std::filesystem::path& path, ImVec2 origin, ImVec2 size);
