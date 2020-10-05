#!/bin/sh
# Meson install script for gaeguli-tc-helper. Sets net_admin capability.
tc_helper_install_path="$1"

tc_helper="$MESON_INSTALL_DESTDIR_PREFIX/$tc_helper_install_path"

echo "Calling $setcap cap_net_admin+ep $tc_helper"
/sbin/setcap cap_net_admin+ep "$tc_helper" || true

