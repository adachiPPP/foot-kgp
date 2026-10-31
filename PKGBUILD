# Maintainer: Your Name <you@example.com>
pkgname=foot-kgp
pkgver=1.28.0
pkgrel=1
pkgdesc="Fast, lightweight and minimalistic Wayland terminal emulator"
arch=('x86_64')
url="https://codeberg.org/dnkl/foot"
conflicts=('foot')
replaces=('foot')
depends=('fcft' 'fontconfig' 'libxkbcommon' 'pixman' 'wayland' 'libutf8proc' 'libutempter' 'libpng')
makedepends=('meson' 'ninja' 'scdoc' 'wayland-protocols')
source=()
sha256sums=()

build() {
    cd "$startdir"
    rm -rf build
    meson setup build --prefix=/usr --buildtype=release
    meson compile -C build
}

package() {
    cd "$startdir"
    meson install -C build --destdir="$pkgdir"
}