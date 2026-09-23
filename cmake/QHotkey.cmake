add_library(qhotkey STATIC 3rdparty/QHotkey/qhotkey.cpp)
add_library(QHotkey::QHotkey ALIAS qhotkey)

# include()d into top-level scope: directory-scope AUTOMOC/PIC would leak onto myproto.
set_target_properties(qhotkey PROPERTIES
        AUTOMOC ON
        POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(qhotkey PUBLIC Qt6::Core Qt6::Gui)

if(APPLE)
    find_library(CARBON_LIBRARY Carbon)
    mark_as_advanced(CARBON_LIBRARY)

    target_sources(qhotkey PRIVATE 3rdparty/QHotkey/qhotkey_mac.cpp)
    target_link_libraries(qhotkey PRIVATE ${CARBON_LIBRARY})
elseif(WIN32)
    target_sources(qhotkey PRIVATE 3rdparty/QHotkey/qhotkey_win.cpp)
else()
    find_package(X11 REQUIRED)

    target_sources(qhotkey PRIVATE 3rdparty/QHotkey/qhotkey_x11.cpp)
    target_include_directories(qhotkey PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(qhotkey PRIVATE ${X11_LIBRARIES})
endif()

target_include_directories(qhotkey PUBLIC 3rdparty/QHotkey)
