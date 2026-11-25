# SPDX-License-Identifier: GPL-2.0-or-later
qlie_game=${QLIE_GAME:-whj2}
qlie_switch_vulkan_default=1
qlie_nro_publisher="QLIE Switch Runtime contributors"
qlie_nro_title_id=
case "$qlie_game" in
  whj1)
    qlie_profile_flags=-DQLIE_PROFILE_WHJ1
    qlie_binary=whj1-switch-runtime
    qlie_nro_title="美少女万華鏡 -呪われし伝說の少女-"
    qlie_nro_version=1.0.0
    ;;
  whj2)
    qlie_profile_flags=
    qlie_binary=qlie-switch-runtime
    qlie_nro_title="QLIE Switch Runtime (Development)"
    qlie_nro_version=0.0.2
    ;;
  whj25|whj2.5)
    qlie_game=whj25
    qlie_profile_flags=-DQLIE_PROFILE_WHJ25
    qlie_binary=whj25-switch-runtime
    qlie_nro_title="美少女万華鏡 -かつて少女だった君へ-"
    qlie_nro_version=1.0.0
    ;;
  whj3)
    qlie_profile_flags=-DQLIE_PROFILE_WHJ3
    qlie_binary=whj3-switch-runtime
    qlie_nro_title="美少女万華鏡 -神が造りたもうた少女たち-"
    qlie_nro_version=1.0.0
    ;;
  whj4)
    qlie_profile_flags=-DQLIE_PROFILE_WHJ4
    qlie_binary=whj4-switch-runtime
    qlie_nro_title="美少女万華鏡 -罪と罰の少女-"
    qlie_nro_version=0.1.0
    ;;
  whj5)
    qlie_profile_flags=-DQLIE_PROFILE_WHJ5
    qlie_binary=whj5-switch-runtime
    qlie_nro_title="美少女万華鏡 -理與迷宮的少女-"
    qlie_nro_version=1.0.0
    ;;
  whj6)
    qlie_profile_flags=-DQLIE_PROFILE_WHJ6
    qlie_binary=whj6-switch-runtime
    qlie_nro_title="美少女万華鏡異聞 雪女"
    qlie_nro_version=1.0.1
    qlie_nro_publisher="ωstar"
    qlie_nro_title_id=01007F8E5C160000
    qlie_switch_vulkan_default=0
    ;;
  *)
    echo "QLIE_GAME must be whj1, whj2, whj25, whj3, whj4, whj5, or whj6" >&2
    exit 2
    ;;
esac
