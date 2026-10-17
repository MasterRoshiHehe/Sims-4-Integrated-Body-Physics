// Rewrites a game skinned vertex shader (DXBC) so it can move breast vertices: see wavepatch.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wavepatch {
// Added to a bone slot written into cb0 row 3 (see wavepatch.cpp).
constexpr uint32_t kSlotTag = 0x3F000;
// The patched shader in `out` and its bone palette's first row in cb0 (`base`), or false (shader left alone:
// not skinned, reads cb0 row 3 itself, or not laid out as expected).
bool patch(const uint8_t* code, size_t size, std::vector<uint8_t>& out, unsigned* base);
}  // namespace wavepatch
