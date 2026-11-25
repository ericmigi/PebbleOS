/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Panel-resolution detail of the system UI's text, for displays that show each framebuffer
//! pixel as 2x2 panel pixels.
//!
//! Text in a font stored at twice its resolution (FEATURE_SCALE2) is drawn into the 1-bit
//! framebuffer as usual, a pixel inked when its four panel pixels are at least half covered; each
//! pixel whose four panel pixels are not all fully covered is also recorded here, with their
//! coverage. When the
//! frame is sent, the display draws those pixels from their pattern, as sub-pixel edges or as grey
//! levels. Nothing else needs a frame at panel resolution: the record holds only text edges, in a
//! fixed pool, and pixels that do not fit stay as the framebuffer has them.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <applib/fonts/fonts_private.h>
#include <applib/graphics/gtypes.h>

typedef enum {
  //! The framebuffer as is
  TextDetailModeMono,
  //! Edge pixels at one of four levels, added when the screen is idle
  TextDetailModeGrey,
  //! Edge pixels drawn as their 2x2 panel pixels
  TextDetailModeSubpixel,
  //! Edge pixels drawn as their 2x2 panel pixels, which get one of four levels when idle
  TextDetailModeSubpixelGrey,
  TextDetailModeCount,
} TextDetailMode;

//! Starts recording the edges of a glyph drawn into @p dest.
//! @return the owner of the records to pass on, or -1 when @p dest is not on the display
int text_detail_glyph_begin(const GBitmap *dest);

//! Records the coverage of the 2x2 panel pixels of framebuffer pixel (x, y), 0 to 3 each in two
//! bits: top left in bits 0-1, top right, bottom left, bottom right in 6-7; in ink of the text
//! color (white or black). Between text_detail_glyph_begin() and text_detail_glyph_end().
void text_detail_add(int owner, int16_t x, int16_t y, uint8_t levels, bool white_ink);

//! Records @p count edges of a glyph at @p origin, each x, y (from @p origin) and levels as
//! text_detail_add() takes them, in order of y; those outside @p clip are left out.
void text_detail_add_edges(int owner, GPoint origin, const GRect *clip, const uint8_t *edges,
                           uint16_t count, bool white_ink);

//! Ends the glyph text_detail_glyph_begin() started.
void text_detail_glyph_end(int owner);

//! Forgets the detail the kernel's own windows recorded, before they render again, and the app's
//! too when they hide it.
void text_detail_kernel_frame_begin(bool app_hidden);

//! Forgets the detail the app recorded, before it renders again. A no-op for apps that cannot
//! use the system UI's fonts.
void sys_text_detail_app_frame_begin(void);

//! Which records a frame sent to the display shows
typedef struct {
  //! The app's frame is on the display one to one
  bool app;
  //! Area of the display the kernel's windows cover over the app
  GRect modal;
  //! Grey levels, for the refinement of an idle screen
  bool grey;
} TextDetailFrame;

//! Fills @p cells with the panel levels of row @p y (see struct pbl_display_detail), checking each
//! record against row @p fb_row of the frame sent, so pixels drawn over since are left alone.
//! @return whether the row has any detail; @p cells is left as it is when not
bool text_detail_row(const TextDetailFrame *frame, uint16_t y, const uint8_t *fb_row,
                     uint8_t *cells);

//! Whether the current frame has detail the grey mode would add when idle
bool text_detail_wants_grey(void);

TextDetailMode text_detail_get_mode(void);
void text_detail_set_mode(TextDetailMode mode);

#ifdef CONFIG_FRAMEBUFFER_SCALE2
// At twice the display's size (text_grey.c) the framebuffer has the text at panel resolution, and
// the records are the glyphs drawn, for the grey pass; text_detail_row() fills a row of the stored
// size with 2 bits per pixel (see struct pbl_display_detail)

//! Whether text is inked from a quarter coverage, for the grey pass to lighten (a grey mode)
bool text_detail_ink_light(void);

//! Records a glyph of a font stored with its edges, drawn in black into @p dest: its bitmap's top
//! left @p origin, its @p height and the @p clip it was drawn in, in display pixels
void text_detail_add_glyph(const GBitmap *dest, const FontInfo *font, uint32_t codepoint,
                           GPoint origin, uint8_t height, const GRect *clip);

//! Makes the next grey pass draw a fixed test pattern rather than the text's levels, until the
//! next frame
void text_detail_test(void);
#endif

//! Records in use and the most ever used, and records dropped since boot
void text_detail_get_stats(uint32_t *used, uint32_t *peak, uint32_t *dropped);
