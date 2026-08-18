BUILD_CONFIG="release"

fail()
{
	echo "$1" 1>&2
	exit 1
}

BUILD_ROOT=$PWD/build
SOURCE_ROOT=$PWD
BUILD_FOLDER=$BUILD_ROOT/build-$BUILD_CONFIG
DEPLOY_FOLDER=$BUILD_ROOT/deploy-$BUILD_CONFIG
INSTALLER_FOLDER=$BUILD_ROOT/installer-$BUILD_CONFIG

if [ -n "$CI_VERSION" ]; then
  VERSION=$CI_VERSION
else
  VERSION=`cat $SOURCE_ROOT/app/version.txt`
fi

command -v qmake6 >/dev/null 2>&1 || fail "Unable to find 'qmake6' in your PATH!"
if ! command -v linuxdeploy >/dev/null 2>&1 && ! command -v linuxdeployqt >/dev/null 2>&1; then
  fail "Unable to find 'linuxdeploy' or 'linuxdeployqt' in your PATH!"
fi

echo Cleaning output directories
rm -rf $BUILD_FOLDER
rm -rf $DEPLOY_FOLDER
rm -rf $INSTALLER_FOLDER
mkdir $BUILD_ROOT
mkdir $BUILD_FOLDER
mkdir $DEPLOY_FOLDER
mkdir $INSTALLER_FOLDER

echo Configuring the project
pushd $BUILD_FOLDER
# Building with Wayland support will cause linuxdeployqt to include libwayland-client.so in the AppImage.
# Since we always use the host implementation of EGL, this can cause libEGL_mesa.so to fail to load due
# to missing symbols from the host's version of libwayland-client.so that aren't present in the older
# version of libwayland-client.so from our AppImage build environment. When this happens, EGL fails to
# work even in X11. To avoid this, we will disable Wayland support for the AppImage.
#
# We disable DRM support because linuxdeployqt doesn't bundle the appropriate libraries for Qt EGLFS.
qmake6 $SOURCE_ROOT/moonlight-qt.pro CONFIG+=disable-wayland CONFIG+=disable-libdrm PREFIX=$DEPLOY_FOLDER/usr DEFINES+=APP_IMAGE || fail "Qmake failed!"
popd

echo Compiling Moonlight in $BUILD_CONFIG configuration
pushd $BUILD_FOLDER
make -j$(nproc) $(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]') || fail "Make failed!"
popd

echo Deploying to staging directory
pushd $BUILD_FOLDER
make install || fail "Make install failed!"
popd

# We need to manually place SDL3 in our AppImage, since linuxdeployqt
# cannot see the dependency via ldd when it looks at SDL2-compat.
# Skipped when building against a plain distro SDL2 (no sdl2-compat),
# where no separate SDL3 library exists.
SDL3_ARGS=""
if [ -f /usr/local/lib/libSDL3.so.0 ]; then
  echo Staging SDL3 library
  mkdir -p $DEPLOY_FOLDER/usr/lib
  cp /usr/local/lib/libSDL3.so.0 $DEPLOY_FOLDER/usr/lib/
  SDL3_ARGS="-executable=$DEPLOY_FOLDER/usr/lib/libSDL3.so.0"
fi

echo Creating AppImage
pushd $INSTALLER_FOLDER
if command -v linuxdeploy >/dev/null 2>&1; then
  # Preferred: linuxdeploy with the qt plugin (linuxdeployqt's maintained
  # successor; linuxdeployqt's continuous build dies silently after the
  # icon stage on current runners).
  DEPLOY_LIB_ARGS=""
  if [ -f /usr/local/lib/libSDL3.so.0 ]; then
    DEPLOY_LIB_ARGS="--library /usr/local/lib/libSDL3.so.0"
  fi
  QMAKE=qmake6 QML_SOURCES_PATHS=$SOURCE_ROOT/app/gui VERSION=$VERSION \
    linuxdeploy --appdir $DEPLOY_FOLDER \
    -d $DEPLOY_FOLDER/usr/share/applications/com.moonlight_stream.Moonlight.desktop \
    -i $DEPLOY_FOLDER/usr/share/icons/hicolor/scalable/apps/moonlight.svg \
    $DEPLOY_LIB_ARGS --plugin qt --output appimage || fail "linuxdeploy failed!"
else
  # LINUXDEPLOYQT_EXTRA_ARGS lets CI pass flags like
  # -unsupported-allow-new-glibc when building on a newer base image than
  # linuxdeployqt's oldest-supported-LTS policy expects. Our AppImage only
  # targets omarchy heads (rolling glibc), so a new build base is fine.
  VERSION=$VERSION linuxdeployqt $DEPLOY_FOLDER/usr/share/applications/com.moonlight_stream.Moonlight.desktop \
    -qmake=qmake6 -qmldir=$SOURCE_ROOT/app/gui -appimage -extra-plugins=tls \
    $SDL3_ARGS $LINUXDEPLOYQT_EXTRA_ARGS || fail "linuxdeployqt failed!"
fi
popd

echo Build successful