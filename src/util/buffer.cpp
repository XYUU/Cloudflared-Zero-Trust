// SPDX-License-Identifier: Apache-2.0
#include "cfd/buffer.hpp"
// Buffer is header-only; this TU exists so the library can refer to it via
// add_library() sources without forcing INTERFACE semantics.
namespace cfd { namespace { [[maybe_unused]] inline void touch() { Buffer b(1); (void)b; } } }
