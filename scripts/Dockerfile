ARG BASE_IMAGE
FROM ${BASE_IMAGE}

# Install SANE, GStreamer, and AppImage build dependencies
# Note: OpenCV is optional - enable with QSCAN_ENABLE_OPENCV=ON during cmake
RUN dnf install -y \
    sane-backends \
    sane-backends-devel \
    qpdf \
    qpdf-devel \
    libtiff \
    libtiff-devel \
    gstreamer1-devel \
    gstreamer1-plugins-base-devel \
    gstreamer1-plugins-base \
    gstreamer1-plugins-good \
    wget \
    file \
    patchelf \
    desktop-file-utils \
    fuse \
    fuse-libs \
    && dnf clean all

# Optional: Install OpenCV (only core and imgproc, not full suite)
# RUN dnf install -y \
#     opencv-core \
#     opencv-devel \
#     && dnf clean all

# Download appimagetool and extract it (because FUSE)
RUN cd /tmp && \
    wget -q https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage -O appimagetool.AppImage && \
    chmod +x appimagetool.AppImage && \
    ./appimagetool.AppImage --appimage-extract && \
    mv squashfs-root /opt/appimagetool && \
    ln -s /opt/appimagetool/AppRun /usr/local/bin/appimagetool && \
    rm appimagetool.AppImage

