#ifndef EXTRACTOR_H
#define EXTRACTOR_H

#include <functional>
#include <string>

typedef std::function<void(float progress, const char *status_msg)> ExtractionProgressCallback;

// Check if all core game, startup UI assets, and binaries exist in the directory
bool is_game_installed(const char *target_dir = ".");

// Extract the engine, STK archive, and Android startup splash from an APK.
bool extract_apk_to_dir(const char *apk_path, const char *target_dir = ".", ExtractionProgressCallback callback = nullptr);

#endif // EXTRACTOR_H
