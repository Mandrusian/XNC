#pragma once

#include <string>
#include <string_view>

namespace snc {

inline constexpr std::string_view kDefaultHomeUrl = "https://duckduckgo.com/";

/// Resolves raw address bar input into a navigable URL or DuckDuckGo search URL.
std::string resolve_input(std::string_view raw);

/// Extracts the lowercase host (without port) from a URL, or empty string if absent.
std::string extract_host(std::string_view url);

/// Returns a concise host label for display in tabs and toolbars.
std::string display_host(std::string_view url);

/// Percent-encodes a UTF-8 query component.
std::string percent_encode(std::string_view input);

} // namespace snc
