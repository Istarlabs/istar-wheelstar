# SPDX-FileCopyrightText: 2026 Pascal Klose
# SPDX-License-Identifier: GPL-3.0-or-later

# The full path: a Plasma session started by systemd does not have ~/.local/bin in its PATH, so a bare
# `Exec=istar-wheelstar` is not found when installed there.
configure_file(net.kurokyuu.wheelstar.desktop.in net.kurokyuu.wheelstar.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/net.kurokyuu.wheelstar.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES net.kurokyuu.wheelstar.svg DESTINATION ${KDE_INSTALL_FULL_ICONDIR}/hicolor/scalable/apps)

# Named after the application, which is how KNotification finds its events.
install(FILES istar-wheelstar.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})

# Not into /etc: the program offers to copy it there itself, through pkexec, when the wheel is not writable.
install(FILES udev/99-logitech-wheel-perms.rules DESTINATION ${KDE_INSTALL_DATADIR}/istar-wheelstar/udev)
