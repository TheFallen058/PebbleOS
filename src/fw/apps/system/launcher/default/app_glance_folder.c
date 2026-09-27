/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "app_glance_folder.h"

#include "app_glance_structured.h"

#include "kernel/pbl_malloc.h"
#include "resource/resource_ids.auto.h"
#include "pbl/services/i18n/i18n.h"
#include "pbl/services/timeline/attribute.h"
#include "system/passert.h"
#include "pbl/kernel/compiler.h"
#include "pbl/util/struct.h"

#include <stdio.h>
#include <string.h>

#define FOLDER_GLANCE_SUBTITLE_SIZE 24

typedef struct LauncherAppGlanceFolder {
  char title[LAUNCHER_FOLDER_NAME_BUFFER_SIZE];
  char subtitle[FOLDER_GLANCE_SUBTITLE_SIZE];
  //! The folder icon, or the caller's fallback if it could not be loaded.
  const KinoReel *icon;
  //! Whether \ref icon is ours to destroy.
  bool owns_icon;
  //! Resolved once; i18n_get() must not be called repeatedly for the same string.
  const char *singular_format;
  const char *plural_format;
} LauncherAppGlanceFolder;

static KinoReel *prv_get_icon(LauncherAppGlanceStructured *structured_glance) {
  LauncherAppGlanceFolder *folder_glance =
      launcher_app_glance_structured_get_data(structured_glance);
  return (KinoReel *)NULL_SAFE_FIELD_ACCESS(folder_glance, icon, NULL);
}

static const char *prv_get_title(LauncherAppGlanceStructured *structured_glance) {
  LauncherAppGlanceFolder *folder_glance =
      launcher_app_glance_structured_get_data(structured_glance);
  return NULL_SAFE_FIELD_ACCESS(folder_glance, title, NULL);
}

static void prv_folder_glance_subtitle_dynamic_text_node_update(
    PBL_UNUSED GContext *ctx, PBL_UNUSED GTextNode *node, PBL_UNUSED const GRect *box,
    PBL_UNUSED const GTextNodeDrawConfig *config, PBL_UNUSED bool render, char *buffer,
    size_t buffer_size, void *user_data) {
  LauncherAppGlanceStructured *structured_glance = user_data;
  LauncherAppGlanceFolder *folder_glance =
      launcher_app_glance_structured_get_data(structured_glance);
  if (folder_glance) {
    strncpy(buffer, folder_glance->subtitle, buffer_size);
    buffer[buffer_size - 1] = '\0';
  }
}

static GTextNode *prv_create_subtitle_node(LauncherAppGlanceStructured *structured_glance) {
  return launcher_app_glance_structured_create_subtitle_text_node(
      structured_glance, prv_folder_glance_subtitle_dynamic_text_node_update);
}

static void prv_destructor(LauncherAppGlanceStructured *structured_glance) {
  LauncherAppGlanceFolder *folder_glance =
      launcher_app_glance_structured_get_data(structured_glance);
  if (folder_glance && folder_glance->owns_icon) {
    kino_reel_destroy((KinoReel *)folder_glance->icon);
  }
  i18n_free_all(structured_glance);
  app_free(folder_glance);
}

static const LauncherAppGlanceStructuredImpl s_folder_structured_glance_impl = {
  .get_icon = prv_get_icon,
  .get_title = prv_get_title,
  .create_subtitle_node = prv_create_subtitle_node,
  .destructor = prv_destructor,
};

LauncherAppGlance *launcher_app_glance_folder_create(const KinoReel *fallback_icon) {
  LauncherAppGlanceFolder *folder_glance = app_zalloc_check(sizeof(*folder_glance));

  folder_glance->icon = kino_reel_create_with_resource(RESOURCE_ID_MENU_LAYER_GENERIC_FOLDER_ICON);
  folder_glance->owns_icon = (folder_glance->icon != NULL);
  if (!folder_glance->icon) {
    folder_glance->icon = fallback_icon;
  }

  // A folder is not an app: it has no UUID of its own and never shows app glance slices.
  const Uuid uuid = UUID_SYSTEM;
  const bool should_consider_slices = false;
  LauncherAppGlanceStructured *structured_glance = launcher_app_glance_structured_create(
      &uuid, &s_folder_structured_glance_impl, should_consider_slices, folder_glance);
  PBL_ASSERTN(structured_glance);

  /// Subtitle of a launcher folder holding a single app.
  folder_glance->singular_format = i18n_get("%u app", structured_glance);
  /// Subtitle of a launcher folder, showing how many apps it holds.
  folder_glance->plural_format = i18n_get("%u apps", structured_glance);

  return &structured_glance->glance;
}

void launcher_app_glance_folder_set_folder(LauncherAppGlance *glance,
                                           const LauncherFolderRecord *folder,
                                           uint16_t member_count) {
  if (!glance || !folder) {
    return;
  }

  LauncherAppGlanceStructured *structured_glance = (LauncherAppGlanceStructured *)glance;
  LauncherAppGlanceFolder *folder_glance =
      launcher_app_glance_structured_get_data(structured_glance);

  strncpy(folder_glance->title, folder->name, sizeof(folder_glance->title));
  folder_glance->title[sizeof(folder_glance->title) - 1] = '\0';

  const char *format = (member_count == 1) ? folder_glance->singular_format
                                           : folder_glance->plural_format;
  snprintf(folder_glance->subtitle, sizeof(folder_glance->subtitle), format, member_count);
}
