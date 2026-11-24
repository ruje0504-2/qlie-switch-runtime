# SPDX-License-Identifier: GPL-2.0-or-later
qlie_game=${QLIE_GAME:-whj2}
case "$qlie_game" in
  whj2)
    qlie_profile_flags=
    qlie_binary=qlie-switch-runtime
    qlie_nro_title="QLIE Switch Runtime (Development)"
    qlie_nro_version=0.0.2
    ;;
  whj4)
    qlie_profile_flags=-DQLIE_PROFILE_WHJ4
    qlie_binary=whj4-switch-runtime
    qlie_nro_title="美少女万華鏡 -罪と罰の少女-"
    qlie_nro_version=0.1.0
    ;;
  *)
    echo "QLIE_GAME must be whj2 or whj4" >&2
    exit 2
    ;;
esac
