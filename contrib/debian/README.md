
Debian
====================
This directory contains files used to package raptoreumd/raptoreum-qt
for Debian-based Linux systems. If you compile raptoreumd/raptoreum-qt yourself, there are some useful files here.

## raptoreum: URI support ##


raptoreum-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install raptoreum-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your raptoreum-qt binary to `/usr/bin`
and the `../../share/pixmaps/raptoreum128.png` to `/usr/share/pixmaps`

raptoreum-qt.protocol (KDE)

