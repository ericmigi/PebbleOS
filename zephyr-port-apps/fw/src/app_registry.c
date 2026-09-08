/* SPDX-License-Identifier: Apache-2.0 */

#include "app_registry.h"

#include <inttypes.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include "pbl/services/blob_db/app_db.h"
#include "process_management/pebble_process_info.h"
#include "process_management/pebble_process_md.h"

// System-app metadata providers (the real *_get_app_info()). Registering a new
// privileged built-in app = add its header here + an md_fn in s_system_apps +
// its sources to CMakeLists (see zephyr-port-notes/SYSTEM-APPS-BUILDOUT.md).
#include "apps/system/alarms/alarms.h"
#include "apps/system/music.h"
#include "apps/watch/tictoc/tictoc.h"
#include "apps/system/watchfaces.h"
const PebbleProcessMd *digital_face_get_app_info(void);  // port digital watchface
#include "apps/system/settings/settings.h"

// A 20 KiB AppDB holds roughly 150 metadata records in production.
#define FW_MAX_INSTALLED_APPS 150
#define FW_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

typedef const PebbleProcessMd *(*FwSystemAppMdFn)(void);

#ifdef FW_REAL_SHELL
#include "resource/resource_ids.auto.h"

// Launcher "Notifications" entry: show the stored notification history via the
// port's minimal notifications app (reads the real notification_storage).
extern void fw_notifications_app_main(void);
static void prv_notifications_main(void) { fw_notifications_app_main(); }

static const PebbleProcessMd *prv_notifications_md(void) {
  static const PebbleProcessMdSystem s_notifications_md = {
    .common = {
      .main_func = prv_notifications_main,
      // UUID: b2cae818-10f8-46df-ad2b-98ad2254a3c1
      .uuid = {0xb2, 0xca, 0xe8, 0x18, 0x10, 0xf8, 0x46, 0xdf,
               0xad, 0x2b, 0x98, 0xad, 0x22, 0x54, 0xa3, 0xc1},
    },
    .name = "Notifications",
    .icon_resource_id = RESOURCE_ID_NOTIFICATIONS_APP_GLANCE,
  };
  return (const PebbleProcessMd *)&s_notifications_md;
}

extern void fw_timeline_future_app_main(void);
extern void fw_timeline_past_app_main(void);

static const PebbleProcessMd *prv_timeline_future_md(void) {
  static const PebbleProcessMdSystem s_md = {
    .common = { .main_func = fw_timeline_future_app_main,
                .uuid = {0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f, 0x70, 0x81,
                         0x92, 0xa3, 0xb4, 0xc5, 0xd6, 0xe7, 0xf8, 0x10} },
    .name = "Timeline Future",
  };
  return (const PebbleProcessMd *)&s_md;
}

static const PebbleProcessMd *prv_timeline_past_md(void) {
  static const PebbleProcessMdSystem s_md = {
    .common = { .main_func = fw_timeline_past_app_main,
                .uuid = {0x2a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f, 0x70, 0x81,
                         0x92, 0xa3, 0xb4, 0xc5, 0xd6, 0xe7, 0xf8, 0x20} },
    .name = "Timeline Past",
  };
  return (const PebbleProcessMd *)&s_md;
}

// Launcher Weather glance: no weather app to launch, but enrolling the entry
// with the weather-glance UUID makes the launcher render app_glance_weather
// (which reads the port weather state) as its row.
static void prv_weather_main(void) {}
static const PebbleProcessMd *prv_weather_md(void) {
  static const PebbleProcessMdSystem s_md = {
    .common = { .main_func = prv_weather_main,
                .uuid = {0x61, 0xb2, 0x2b, 0xc8, 0x1e, 0x29, 0x46, 0x0d,
                         0xa2, 0x36, 0x3f, 0xe4, 0x09, 0xa4, 0x39, 0xff} },
    .name = "Weather",
  };
  return (const PebbleProcessMd *)&s_md;
}
#endif

typedef struct {
  AppInstallId id;
  const char *name;
  // Non-NULL => a privileged built-in system app launched via its real md.
  // NULL => a not-yet-ported entry (launcher falls back to the sandboxed PBW).
  FwSystemAppMdFn md_fn;
} FwSystemApp;

// Default-enabled normal-shell entries from system_app_registry_list.json.
// Names mirror the PebbleProcessMdSystem metadata (Golf is a resource app).
static const FwSystemApp s_system_apps[] = {
  { -1, "TicToc", tictoc_get_app_info },
  { -110, "Digital", digital_face_get_app_info },
  { -98, "Kickstart" },
  { -2, "Watch Only" },
  { -7, "Settings", settings_get_app_info },
  { -3, "Music", music_app_get_info },
#ifdef FW_REAL_SHELL
  { -4, "Notifications", prv_notifications_md },
#else
  { -4, "Notifications" },
#endif
  { -5, "Alarms", alarms_app_get_info },
  { -6, "Watchfaces", watchfaces_get_app_info },
  { -9, "Quick Launch" },
#ifdef FW_REAL_SHELL
  { -10, "Timeline Future", prv_timeline_future_md },
  { -96, "Timeline Past", prv_timeline_past_md },
#else
  { -10, "Timeline Future" },
  { -96, "Timeline Past" },
#endif
  { -54, "Launcher" },
#ifdef FW_REAL_SHELL
  { -59, "Weather", prv_weather_md },
#else
  { -59, "Weather" },
#endif
  { -95, "Workout" },
  { -62, "Battery Critical" },
  { -82, "Health" },
  { -83, "Send Text" },
  { -90, "Reminder" },
  { -92, "Quiet Time" },
  { -99, "Backlight" },
  { -94, "Motion Backlight" },
  { -93, "Airplane Mode" },
  { -97, "Sports" },
  { -32, "Timeline" },
  { -100, "Clear Notification History" },
  { -52, "Golf" },
};

static FwAppRegistryEntry
    s_entries[FW_ARRAY_SIZE(s_system_apps) + FW_MAX_INSTALLED_APPS];
static size_t s_entry_count;
static const FwAppRegistryEntry *s_launch_candidate;

static void prv_copy_name(char destination[FW_APP_NAME_SIZE + 1],
                          const char source[FW_APP_NAME_SIZE]) {
  memcpy(destination, source, FW_APP_NAME_SIZE);
  destination[FW_APP_NAME_SIZE] = '\0';
}

static void prv_add_installed_app(AppInstallId install_id, AppDBEntry *db_entry,
                                  void *context) {
  (void)context;
  if (s_entry_count == FW_ARRAY_SIZE(s_entries)) {
    return;
  }

  FwAppRegistryEntry *entry = &s_entries[s_entry_count++];
  *entry = (FwAppRegistryEntry) {
    .install_id = install_id,
    .uuid = db_entry->uuid,
    .info_flags = db_entry->info_flags,
    .installed = true,
  };
  prv_copy_name(entry->name, db_entry->name);
}

static void prv_print_uuid(const Uuid *uuid) {
  const uint8_t *bytes = (const uint8_t *)uuid;
  printk("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
         "%02x%02x%02x%02x%02x%02x",
         bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
         bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
         bytes[12], bytes[13], bytes[14], bytes[15]);
}

static const FwAppRegistryEntry *prv_pick_app(void) {
  for (size_t i = FW_ARRAY_SIZE(s_system_apps); i < s_entry_count; ++i) {
    FwAppRegistryEntry *entry = &s_entries[i];
    if ((entry->info_flags & (PROCESS_INFO_VISIBILITY_HIDDEN |
                              PROCESS_INFO_VISIBILITY_SHOWN_ON_COMMUNICATION)) == 0) {
      return entry;
    }
  }

  // TicToc is the normal shell's first built-in watchface.
  return s_entry_count == 0 ? NULL : &s_entries[0];
}

void fw_app_registry_init(void) {
  s_entry_count = 0;

  for (size_t i = 0; i < FW_ARRAY_SIZE(s_system_apps); ++i) {
    FwAppRegistryEntry *entry = &s_entries[s_entry_count++];
    *entry = (FwAppRegistryEntry) {
      .install_id = s_system_apps[i].id,
      .md = s_system_apps[i].md_fn ? s_system_apps[i].md_fn() : NULL,
    };
    strncpy(entry->name, s_system_apps[i].name, FW_APP_NAME_SIZE);
  }

  app_db_init();
  app_db_enumerate_entries(prv_add_installed_app, NULL);
  s_launch_candidate = prv_pick_app();

  printk("FW_REGISTRY_UP\n");
  printk("FW_APP_COUNT %zu\n", s_entry_count);
  for (size_t i = 0; i < s_entry_count; ++i) {
    const FwAppRegistryEntry *entry = &s_entries[i];
    if (entry->installed) {
      printk("FW_APP ");
      prv_print_uuid(&entry->uuid);
      printk(" %s\n", entry->name);
    } else {
      printk("FW_APP %" PRId32 " %s\n", entry->install_id, entry->name);
    }
  }
}

size_t fw_app_registry_count(void) {
  return s_entry_count;
}

const FwAppRegistryEntry *fw_app_registry_get(size_t index) {
  return index < s_entry_count ? &s_entries[index] : NULL;
}

const FwAppRegistryEntry *fw_launcher_pick_app(void) {
  return s_launch_candidate;
}

const FwAppRegistryEntry *fw_app_registry_find_by_id(AppInstallId install_id) {
  for (size_t i = 0; i < s_entry_count; ++i) {
    if (s_entries[i].install_id == install_id) {
      return &s_entries[i];
    }
  }
  return NULL;
}
