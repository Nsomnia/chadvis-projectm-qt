# Maintainer: Nsomnia (ChadVis) <github.com/Nsomnia>
# Qt6 projectM v4 visualizer

pkgname=chadvis-projectm-qt
pkgver=1.0.0
pkgrel=1
pkgdesc="Qt6 projectM v4 visualizer with modern C++20 and video recording"
arch=('x86_64')
url="https://github.com/Nsomnia/chadvis-projectm-qt"
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
source=("git+https://github.com/Nsomnia/chadvis-projectm-qt.git#tag=v${pkgver}")
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
}
