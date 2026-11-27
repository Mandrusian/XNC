#pragma once

#include <string>
#include <string_view>

namespace xnc {

inline constexpr std::string_view kDefaultHomeUrl = "https://duckduckgo.com/";

/// Normalizes a custom SearXNG instance URL or template (empty disables custom SearXNG).
std::string normalize_searxng_base(std::string_view raw);

/// Returns the home page URL for the configured SearXNG base (or DuckDuckGo if empty).
std::string home_url_for_search_engine(std::string_view search_base_url);

/// Resolves raw address bar input into a navigable URL or search query URL.
std::string resolve_input(std::string_view raw, std::string_view search_base_url = {});

/// Extracts the lowercase host (without port) from a URL, or empty string if absent.
std::string extract_host(std::string_view url);

/// Returns a concise host label for display in tabs and toolbars.
std::string display_host(std::string_view url);

/// Percent-encodes a UTF-8 query component.
std::string percent_encode(std::string_view input);

} // namespace xnc
