/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "app_glance.h"

#include "applib/ui/kino/kino_reel.h"
#include "pbl/services/process_management/launcher_folder_storage.h"

//! Glance for a launcher folder row. Folders have no UUID and no app glance slices, so unlike
//! app glances this one is not cached per app by \ref LauncherAppGlanceService. The launcher
//! menu layer keeps a single instance and retargets it at the folder it is about to draw.
//! @param fallback_icon Icon to draw for folders; owned by the caller.
LauncherAppGlance *launcher_app_glance_folder_create(const KinoReel *fallback_icon);

//! Points the glance at the folder that is about to be drawn.
//! @param member_count Number of installed apps currently shown inside the folder.
void launcher_app_glance_folder_set_folder(LauncherAppGlance *glance,
                                           const LauncherFolderRecord *folder,
                                           uint16_t member_count);
