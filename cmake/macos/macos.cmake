find_library(SECURITY_FRAMEWORK Security)
set(PLATFORM_SOURCES include/sys/macos/MacOS.h src/sys/macos/MacOS.cpp src/sys/macos/AutoRun.cpp src/sys/macos/UrlScheme.cpp)
set(PLATFORM_LIBRARIES ${SECURITY_FRAMEWORK})