#include "extractor.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "../../third_party/miniz/miniz.h"

namespace fs = std::filesystem;

bool is_game_installed(const char *target_dir) {
    fs::path root(target_dir);
    if (!fs::exists(root / "libS3DClient.so")) return false;
    if (!fs::exists(root / "S3DMain.stk")) return false;
    if (!fs::exists(root / "app_splash.png")) return false;
    return true;
}

bool extract_apk_to_dir(const char *apk_path, const char *target_dir, ExtractionProgressCallback callback) {
    auto report = [&](float p, const char *msg) {
        if (callback) callback(p, msg);
    };

    fs::path out_root(target_dir);
    std::error_code ec;
    fs::create_directories(out_root, ec);

    report(0.10f, "Opening APK archive...");

    // Open APK archive using miniz zip reader
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    if (!mz_zip_reader_init_file(&zip_archive, apk_path, 0)) {
        report(0.0f, "Error: Could not open APK file as a ZIP archive.");
        return false;
    }

    // Pre-flight check: verify all required files exist before writing to disk
    report(0.25f, "Locating required game files...");
    int so_idx = mz_zip_reader_locate_file(&zip_archive, "lib/armeabi-v7a/libS3DClient.so", NULL, 0);
    if (so_idx < 0) {
        mz_zip_reader_end(&zip_archive);
        report(0.0f, "Error: lib/armeabi-v7a/libS3DClient.so not found in APK.");
        return false;
    }

    int stk_idx = mz_zip_reader_locate_file(&zip_archive, "assets/S3DMain.smf", NULL, 0);
    if (stk_idx < 0) {
        mz_zip_reader_end(&zip_archive);
        report(0.0f, "Error: assets/S3DMain.smf not found in APK.");
        return false;
    }

    int splash_idx = mz_zip_reader_locate_file(&zip_archive, "res/drawable/app_splash.png", NULL, 0);
    if (splash_idx < 0) {
        mz_zip_reader_end(&zip_archive);
        report(0.0f, "Error: res/drawable/app_splash.png not found in APK.");
        return false;
    }

    // Extract ARMv7 guest engine shared library
    report(0.40f, "Extracting libS3DClient.so...");
    fs::path so_dest = out_root / "libS3DClient.so";
    if (!mz_zip_reader_extract_to_file(&zip_archive, (mz_uint)so_idx, so_dest.string().c_str(), 0)) {
        mz_zip_reader_end(&zip_archive);
        report(0.0f, "Error: Failed to extract libS3DClient.so.");
        return false;
    }

    // Extract primary ShiVa3D asset package (renamed to S3DMain.stk)
    report(0.70f, "Extracting S3DMain.stk archive...");
    fs::path stk_dest = out_root / "S3DMain.stk";
    if (!mz_zip_reader_extract_to_file(&zip_archive, (mz_uint)stk_idx, stk_dest.string().c_str(), 0)) {
        mz_zip_reader_end(&zip_archive);
        report(0.0f, "Error: Failed to extract S3DMain.smf.");
        return false;
    }

    // Extract splash graphic presented while guest engine initializes (not an STK asset)
    report(0.90f, "Extracting Ubisoft startup splash...");
    fs::path splash_dest = out_root / "app_splash.png";
    if (!mz_zip_reader_extract_to_file(&zip_archive, (mz_uint)splash_idx,
                                       splash_dest.string().c_str(), 0)) {
        mz_zip_reader_end(&zip_archive);
        report(0.0f, "Error: Failed to extract app_splash.png.");
        return false;
    }

    mz_zip_reader_end(&zip_archive);

    report(1.0f, "Extraction complete! Game is ready to play.");
    return true;
}
