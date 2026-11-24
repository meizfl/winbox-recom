#!/usr/bin/env python3
"""Bind SDL 3.4.20 to static libdbus instead of dlopen (musl static ELF)."""
import sys
from pathlib import Path
root = Path(sys.argv[1])
p = root / 'src/core/linux/SDL_dbus.c'
s = p.read_text()
start = s.index('// we never link directly to libdbus.')
end = s.index('static char *inhibit_handle', start)
s = s[:start] + 'static bool dbus_handle = false;\n' + s[end:]
start = s.index('SDL_ELF_NOTE_DLOPEN(')
end = s.index('static bool LoadDBUSSyms', start)
s = s[:start] + s[end:]
s = s.replace('(TYPE)SDL_LoadFunction(dbus_handle, #y)', '(TYPE)y')
start = s.index('static void UnloadDBUSLibrary(void)')
end = s.index('static SDL_InitState dbus_init;', start)
s = s[:start] + '''static void UnloadDBUSLibrary(void) { dbus_handle = false; }
static bool LoadDBUSLibrary(void) { dbus_handle = LoadDBUSSyms(); return dbus_handle; }

''' + s[end:]
s = s.replace('dbus_handle = NULL;', 'dbus_handle = false;')
assert 'SDL_LoadObject(dbus_library)' not in s
p.write_text(s)
p = root / 'cmake/sdlchecks.cmake'
s = p.read_text()
# The software Wayland path needs EGL types, but calls no libEGL functions.
# Keep libwayland-egl (wl_egl_window_*); do not require the Mesa EGL runtime.
old = 'set(WAYLAND_PKG_CONFIG_SPEC "wayland-client>=1.18" wayland-egl wayland-cursor egl "xkbcommon>=0.5.0")'
assert old in s
s = s.replace(old, old.replace('wayland-cursor egl ', 'wayland-cursor '))
p.write_text(s)
p = root / 'CMakeLists.txt'
s = p.read_text().replace('set(HAVE_DBUS TRUE)', 'set(HAVE_DBUS TRUE)\n          sdl_link_dependency(dbus LIBS ${DBUS_STATIC_LINK_LIBRARIES} PKG_CONFIG_SPECS dbus-1)')
# pkg_search_module provides library names, not STATIC_LINK_LIBRARIES.
s = s.replace('${DBUS_STATIC_LINK_LIBRARIES}', '${DBUS_STATIC_LIBRARIES}')
p.write_text(s)
