#pragma once

#include "menu.hpp"
#include "mono_framebuffer.hpp"

// Draws whatever the menu currently shows: a list, the clock editor, a
// confirmation or a result message. The menu decides what is on screen; this
// only decides where it goes.
void DrawMenuScreen(MonoFramebuffer &canvas, const MenuView &view);

// The date and time as the editor shows it, with the field being changed
// marked out. `out` takes "2026-09-17 14:35", so at least 17 bytes.
// `fieldFirst` and `fieldLast` come back as the character positions of the
// field being edited, which the screen underlines.
void FormatEditableTime(char *out, std::size_t size, const Ds3231::DateTime &time, std::uint8_t field,
                        std::size_t &fieldFirst, std::size_t &fieldLast);
