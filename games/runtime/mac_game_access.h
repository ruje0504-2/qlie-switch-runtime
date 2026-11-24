// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int whj2_mac_can_read_game(const char *path,const char *keyPath);
/* Caller frees the chosen path. The selected directory grant lives until exit. */
char *whj2_mac_choose_game(const char *initial,const char *keyPath);
#ifdef __cplusplus
}
#endif
