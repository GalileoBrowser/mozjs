/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef util_FastDecimal_h
#define util_FastDecimal_h

#include <cstddef>
#include <system_error>
#include <type_traits>

#include "../../../mfbt/fast_float/include/fast_float/fast_float.h"

namespace js {

// The caller handles Unicode whitespace, non-decimal syntax and Infinity.
// Out-of-range or unsupported inputs retain the established conversion path.
// Input storage is borrowed only during this allocation-free, non-GC call.
template <typename CharT>
inline bool TryFastDecimalChars(const CharT* begin, const CharT* end,
                                bool complete, double* value,
                                const CharT** after) {
  static_assert(sizeof(CharT) == 1 || sizeof(CharT) == 2);
  using FloatChar = std::conditional_t<sizeof(CharT) == 2, char16_t, char>;
  const auto* first = reinterpret_cast<const FloatChar*>(begin);
  const auto* last = reinterpret_cast<const FloatChar*>(end);
  double parsed;
  auto result = fast_float::from_chars(
      first, last, parsed,
      static_cast<fast_float::chars_format>(
          fast_float::chars_format::general |
          fast_float::chars_format::no_infnan |
          fast_float::chars_format::allow_leading_plus));
  if (result.ec != std::errc() || result.ptr == first ||
      (complete && result.ptr != last)) {
    return false;
  }
  *value = parsed;
  *after = begin + (result.ptr - first);
  return true;
}

}  // namespace js

#endif  // util_FastDecimal_h
