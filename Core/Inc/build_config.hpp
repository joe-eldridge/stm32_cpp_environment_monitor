#pragma once

// CubeMX's generated CMake defines DEBUG for the Debug preset only.
#ifdef DEBUG
inline constexpr bool kDebugBuild = true;
#else
inline constexpr bool kDebugBuild = false;
#endif
