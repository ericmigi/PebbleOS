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
#include "apps/system/music.h"
#include "apps/watch/tictoc/tictoc.h"

// A 20 KiB AppDB holds roughly 150 metadata records in production.
#define FW_MAX_INSTALLED_APPS 150
#define FW_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

typedef const PebbleProcessMd *(*FwSystemAppMdFn)(void);

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
  { -69, "TicToc", tictoc_get_app_info },
  { -98, "Kickstart" },
  { -2, "Watch Only" },
  { -7, "Settings" },
  { -3, "Music", music_app_get_info },
  { -4, "Notifications" },
  { -5, "Alarms" },
  { -6, "Watchfaces" },
  { -9, "Quick Launch" },
  { -10, "Timeline Future" },
  { -96, "Timeline Past" },
  { -54, "Launcher" },
  { -59, "Weather" },
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
