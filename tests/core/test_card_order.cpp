#include <catch_amalgamated.hpp>
#include "ClashEnv.h"
#include <algorithm>

// CardRegistry is an unordered_map; its iteration order is the standard
// library's choice (MSVC and libc++ differ): the Python probes take the first
// ids as a deck, which was legal on Windows and put a Hero in slot 0 on macOS.
// (Seeded random decks can still differ across standard libraries: the
// <random> distributions are not specified bit-for-bit.)
TEST_CASE("getAllCardIds is sorted, so it is the same on every platform", "[card_registry][portability]") {
    const std::vector<int> ids = getAllCardIds();
    REQUIRE_FALSE(ids.empty());
    REQUIRE(std::is_sorted(ids.begin(), ids.end()));
}
