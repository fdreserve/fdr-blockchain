
Debian
====================
This directory contains files used to package fdreserved/fdreserve-qt
for Debian-based Linux systems. If you compile fdreserved/fdreserve-qt yourself, there are some useful files here.

## fdreserve: URI support ##


fdreserve-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install fdreserve-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your fdreserveqt binary to `/usr/bin`
and the `../../share/pixmaps/fdreserve128.png` to `/usr/share/pixmaps`

fdreserve-qt.protocol (KDE)
