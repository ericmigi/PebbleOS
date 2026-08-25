/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Port shadow of the generated font-key table (fw/include is first on the
// include path). fonts_get_system_font() (watchface_sandboxed/port.c) matches
// these keys to the real embedded GOTHIC faces; unembedded keys (e.g. LECO) fall
// back to GOTHIC_14.

#define FONT_KEY_GOTHIC_14 "RESOURCE_ID_GOTHIC_14"
#define FONT_KEY_GOTHIC_14_BOLD "RESOURCE_ID_GOTHIC_14_BOLD"
#define FONT_KEY_GOTHIC_18 "RESOURCE_ID_GOTHIC_18"
#define FONT_KEY_GOTHIC_18_BOLD "RESOURCE_ID_GOTHIC_18_BOLD"
#define FONT_KEY_GOTHIC_24 "RESOURCE_ID_GOTHIC_24"
#define FONT_KEY_GOTHIC_24_BOLD "RESOURCE_ID_GOTHIC_24_BOLD"
#define FONT_KEY_GOTHIC_28 "RESOURCE_ID_GOTHIC_28"
#define FONT_KEY_GOTHIC_28_BOLD "RESOURCE_ID_GOTHIC_28_BOLD"
#define FONT_KEY_GOTHIC_36 "RESOURCE_ID_GOTHIC_36"
#define FONT_KEY_GOTHIC_36_BOLD "RESOURCE_ID_GOTHIC_36_BOLD"
