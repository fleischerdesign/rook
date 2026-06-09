#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace rook::adapters::audio {

using ProgressFn = std::function<void(float progress)>;
using DoneFn = std::function<void(bool success)>;

void downloadFile(std::string_view url, std::string_view dest_path,
                  ProgressFn on_progress, DoneFn on_done);

} // namespace rook::adapters::audio
