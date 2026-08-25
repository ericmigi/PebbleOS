/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Port shadow of the imaging (album-art fetch) service header. The real header
// (PEBBLEOS_ROOT/include/pbl/services/imaging.h) declares a comm-session hook
// typed on PebbleCommSessionEvent, which the port event stub does not model. The
// Music app only uses the two query/request calls below; the port stub
// (app_service_stubs.c) reports "unsupported", so no art is ever requested.
// ponytail: no phone image-fetch in the port; wire the real service + comm event
// when album art matters.

#include <stdbool.h>
#include <stdint.h>

#include "pbl/services/imaging_endpoint_types.h"
#include "pbl/util/uuid.h"

struct GBitmap;
typedef void (*ImagingReceivedHandler)(uint8_t token, struct GBitmap *bitmap);

void imaging_register_handler(ImagingImageType image_type, ImagingReceivedHandler handler);

bool imaging_is_type_supported(ImagingImageType image_type);

bool imaging_request_album_art(uint8_t token, ImagingFormat format, uint16_t width, uint16_t height,
                               const char *title, const char *artist);
bool imaging_request_notification_image(uint8_t token, ImagingFormat format, uint16_t width,
                                        uint16_t height, const Uuid *item_id);
