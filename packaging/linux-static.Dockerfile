FROM alpine:3.23@sha256:644c1ad8fc45cc581ab1b5ea7ec1bebc9b037fd4801ad697ca124deb3f9c9332
RUN apk add --no-cache build-base cmake ninja python3 py3-capstone meson pkgconf curl tar xz \
    cairo-dev cairo-static fontconfig-dev fontconfig-static freetype-dev freetype-static \
    libpng-dev libpng-static zlib-dev zlib-static pixman-dev pixman-static \
    expat-dev expat-static brotli-dev brotli-static bzip2-dev bzip2-static \
    libx11-dev libx11-static libxext-dev libxext-static libxcb-dev libxcb-static \
    libxau-dev libxdmcp-dev libxcursor-dev libxrandr-dev libxfixes-dev libxi-dev libxi-static \
    wayland-dev wayland-static wayland-protocols libxkbcommon-dev libxkbcommon-static \
    libffi-dev dbus-dev
