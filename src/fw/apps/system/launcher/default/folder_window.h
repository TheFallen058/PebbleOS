/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "launcher_layout.h"

//! Pushes the window listing the apps inside \a folder_id. Back returns to the launcher root and
//! selecting an app launches it exactly as the root list does.
void launcher_folder_window_push(LauncherLayout *layout, LauncherFolderId folder_id);

//! Refreshes the open folder window, if any, after the set of installed apps changed. The window
//! closes itself when its folder no longer has an installed app to show.
void launcher_folder_window_handle_apps_changed(void);
