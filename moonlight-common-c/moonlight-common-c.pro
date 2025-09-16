#-------------------------------------------------
#
# Project created by QtCreator 2018-05-05T17:41:00
#
#-------------------------------------------------

QT -= core gui

TARGET = moonlight-common-c
TEMPLATE = lib

# Build a static library
CONFIG += staticlib

# Disable warnings
CONFIG += warn_off

# Include global qmake defs
include(../globaldefs.pri)

win32 {
    contains(QT_ARCH, i386) {
        INCLUDEPATH += $$PWD/../libs/windows/include/x86
    }
    contains(QT_ARCH, x86_64) {
        INCLUDEPATH += $$PWD/../libs/windows/include/x64
    }
    contains(QT_ARCH, arm64) {
        INCLUDEPATH += $$PWD/../libs/windows/include/arm64
    }

    INCLUDEPATH += $$PWD/../libs/windows/include
}
macx {
    INCLUDEPATH += $$PWD/../libs/mac/include
}
unix:!macx {
    CONFIG += link_pkgconfig
    PKGCONFIG += openssl
    DEFINES += HAVE_CLOCK_GETTIME=1
}

COMMON_C_DIR = $$PWD/moonlight-common-c
ENET_DIR = $$COMMON_C_DIR/enet
RS_DIR = $$COMMON_C_DIR/reedsolomon
SOURCES += \
    $$RS_DIR/rs.c \
    $$ENET_DIR/callbacks.c \
    $$ENET_DIR/compress.c \
    $$ENET_DIR/host.c \
    $$ENET_DIR/list.c \
    $$ENET_DIR/packet.c \
    $$ENET_DIR/peer.c \
    $$ENET_DIR/protocol.c \
    $$ENET_DIR/unix.c \
    $$ENET_DIR/win32.c \
    $$COMMON_C_DIR/src_cpp/AudioStream.cpp \
    $$COMMON_C_DIR/src_cpp/ByteBuffer.cpp \
    $$COMMON_C_DIR/src_cpp/Connection.cpp \
    $$COMMON_C_DIR/src_cpp/ConnectionTester.cpp \
    $$COMMON_C_DIR/src_cpp/ControlStream.cpp \
    $$COMMON_C_DIR/src_cpp/FakeCallbacks.cpp \
    $$COMMON_C_DIR/src_cpp/InputStream.cpp \
    $$COMMON_C_DIR/src_cpp/LinkedBlockingQueue.cpp \
    $$COMMON_C_DIR/src_cpp/Misc.cpp \
    $$COMMON_C_DIR/src_cpp/Platform.cpp \
    $$COMMON_C_DIR/src_cpp/PlatformCrypto.cpp \
    $$COMMON_C_DIR/src_cpp/PlatformSockets.cpp \
    $$COMMON_C_DIR/src_cpp/RtpAudioQueue.cpp \
    $$COMMON_C_DIR/src_cpp/RtpVideoQueue.cpp \
    $$COMMON_C_DIR/src_cpp/RtspConnection.cpp \
    $$COMMON_C_DIR/src_cpp/RtspParser.cpp \
    $$COMMON_C_DIR/src_cpp/SdpGenerator.cpp \
    $$COMMON_C_DIR/src_cpp/SimpleStun.cpp \
    $$COMMON_C_DIR/src_cpp/VideoDepacketizer.cpp \
    $$COMMON_C_DIR/src_cpp/VideoStream.cpp
HEADERS += \
    $$COMMON_C_DIR/src_cpp/Limelight.h
INCLUDEPATH += \
    $$RS_DIR \
    $$ENET_DIR/include \
    $$COMMON_C_DIR/src_cpp
DEFINES += HAS_SOCKLEN_T

CONFIG(debug, debug|release) {
    # Enable asserts on debug builds
    DEFINES += LC_DEBUG
}

# Older GCC versions defaulted to GNU89
*-g++ {
    QMAKE_CFLAGS += -std=gnu99
}
