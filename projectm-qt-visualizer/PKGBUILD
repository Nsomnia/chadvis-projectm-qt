# Maintainer: Nsomnia (VibeChad) <nsomnia@vibechad.org> <github.com/Nsomnia>
# Because every Arch user eventually writes a PKGBUILD

pkgname=vibechad-vidz
pkgver=1.0.0
pkgrel=1
pkgdesc="Chad-tier audio visualizer with recording capabilities. I use Arch btw."
arch=('x86_64')
url="https://github.com/nsomnia/vibechad-vidz"
license=('MIT')
depends=(
    'qt6-base'
    'qt6-multimedia'
    'qt6-multimedia-ffmpeg'
    'qt6-svg'
    'ffmpeg'
    'taglib'
    'tomlplusplus'
    'spdlog'
    'fmt'
    'glew'
    'glm'
    'projectm'
)
makedepends=(
    'cmake'
    'ninja'
    'git'
)
optdepends=(
    'ttf-liberation: Default font'
    'ttf-dejavu: Alternative font'
)
source=("git+https://github.com/nsomnia/vibechad-vidz.git#tag=v${pkgver}")
sha256sums=('SKIP')

build() {
    cd "$srcdir/$pkgname"
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    ninja -C build
}

package() {
    cd "$srcdir/$pkgname"
    DESTDIR="$pkgdir" ninja -C build install
    
    # License
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    
    # Desktop file
    install -Dm644 resources/vibechad.desktop \
        "$pkgdir/usr/share/applications/vibechad.desktop"
    
    # Icon
    install -Dm644 resources/icons/vibechad.svg \
        "$pkgdir/usr/share/icons/hicolor/scalable/apps/vibechad.svg"
}
