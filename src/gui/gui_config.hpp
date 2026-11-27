#pragma once

#include "gui_common.hpp"

#include <algorithm>
#include <fstream>
#include <vector>

namespace openrar::gui {

struct UserConfig {
    fs::path last_dir;
    std::vector<fs::path> favorites;
    int win_width = 860;
    int win_height = 540;
    std::string language_code;
};

inline fs::path get_config_file_path() {
    PWSTR appdata = nullptr;
    fs::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata)) && appdata) {
        dir = fs::path(appdata) / "OpenRaR";
        CoTaskMemFree(appdata);
    } else {
        dir = get_exe_path().parent_path();
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / "config.ini";
}

inline UserConfig load_user_config() {
    UserConfig cfg;
    std::ifstream f(get_config_file_path());
    if (!f) return cfg;
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.starts_with("last_dir=")) {
            fs::path p = path_from_utf8(line.substr(9));
            std::error_code ec;
            if (fs::is_directory(p, ec)) cfg.last_dir = p;
        } else if (line.starts_with("fav=") && cfg.favorites.size() < 20) {
            cfg.favorites.push_back(path_from_utf8(line.substr(4)));
        } else if (line.starts_with("win_w=")) {
            try { cfg.win_width = std::clamp(std::stoi(line.substr(6)), 640, 3840); } catch (...) {}
        } else if (line.starts_with("win_h=")) {
            try { cfg.win_height = std::clamp(std::stoi(line.substr(6)), 400, 2160); } catch (...) {}
        } else if (line.starts_with("lang=")) {
            cfg.language_code = line.substr(5);
        }
    }
    return cfg;
}

inline void save_user_config(const UserConfig& cfg) {
    std::ofstream f(get_config_file_path(), std::ios::trunc);
    if (!f) return;
    if (!cfg.last_dir.empty()) f << "last_dir=" << path_to_utf8(cfg.last_dir) << "\n";
    f << "win_w=" << cfg.win_width << "\n";
    f << "win_h=" << cfg.win_height << "\n";
    if (!cfg.language_code.empty()) f << "lang=" << cfg.language_code << "\n";
    for (const auto& fav : cfg.favorites) {
        f << "fav=" << path_to_utf8(fav) << "\n";
    }
}

} // namespace openrar::gui
