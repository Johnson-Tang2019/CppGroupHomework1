cmake_minimum_required(VERSION 3.16)
project(RemoteSensingQtStarter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Concurrent Network)
set(QT_PACKAGE Qt6)

find_package(GDAL QUIET)

# MSYS2/MinGW 的 OpenCV 不能与 MSVC 链接；MSVC 需使用 vcpkg 等 MSVC 版 OpenCV。
if(MSVC)
    if(DEFINED OpenCV_DIR AND OpenCV_DIR MATCHES "[Mm]sys64|[Mm]ingw64")
        unset(OpenCV_DIR CACHE)
    endif()
    find_package(OpenCV QUIET COMPONENTS core imgproc features2d calib3d imgcodecs ml)
    if(OpenCV_FOUND AND OpenCV_DIR MATCHES "[Mm]sys64|[Mm]ingw64")
        message(STATUS "Ignoring MSYS2/MinGW OpenCV for MSVC build")
        set(OpenCV_FOUND FALSE)
    endif()
else()
    find_package(OpenCV REQUIRED COMPONENTS core imgproc features2d calib3d imgcodecs ml)
endif()

set(RS_HAVE_OPENCV FALSE)
if(OpenCV_FOUND)
    set(RS_HAVE_OPENCV TRUE)
elseif(MSVC)
    message(WARNING
        "OpenCV not found for MSVC. Image processing algorithms are disabled. "
        "Use build_msys2_ucrt.ps1 for the full MSYS2 build.")
endif()

add_executable(RemoteSensingQtStarter WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/SplashScreen.cpp
    src/Translation.cpp
    src/TranslationExtended.cpp
    src/AppTheme.cpp
    src/SettingsDialog.cpp
    src/Algorithms.cpp
    src/ExtendedAlgorithms.cpp
    src/RemoteSensingIndices.cpp
    src/RasterIO.cpp
    src/RasterRenderDialog.cpp
    src/Panorama360Widget.cpp
    src/SwipeCompareWidget.cpp
    src/Scene3DWidget.cpp
    src/pcmesh/PcMeshReconstruction.cpp
    include/rs/DataObject.h
    include/rs/Geometry.h
    include/rs/LayerManager.h
    resources/app.rc
    resources/resources.qrc
    include/rs/ProcessingAlgorithm.h
    include/rs/Algorithms.h
    include/rs/ExtendedAlgorithms.h
    include/rs/RemoteSensingIndices.h
    include/rs/RasterIO.h
    include/rs/RasterRenderDialog.h
    include/rs/MainWindow.h
    include/rs/SplashScreen.h
    include/rs/Translation.h
    include/rs/TranslationExtended.h
    include/rs/AppTheme.h
    include/rs/SettingsDialog.h
    include/rs/Panorama360Widget.h
    include/rs/SwipeCompareWidget.h
    include/rs/Scene3DWidget.h    #增加
    include/rs/pcmesh/PcMeshReconstruction.h
)

# Windows EXE 图标：将 .ico 复制到构建目录，确保 RC 编译器能找到
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

target_include_directories(RemoteSensingQtStarter PRIVATE include)
target_link_libraries(RemoteSensingQtStarter PRIVATE ${QT_PACKAGE}::Widgets ${QT_PACKAGE}::OpenGL ${QT_PACKAGE}::OpenGLWidgets ${QT_PACKAGE}::Concurrent ${QT_PACKAGE}::Network opengl32)

# 编译完成后自动部署运行所需 DLL 和 Qt 插件。
# - TARGET_RUNTIME_DLLS 复制直接链接库的 DLL（Qt/GDAL/OpenCV 等，CMake 3.21+ 支持）
# - windeployqt 复制 Qt 平台插件、imageformats/qjpeg 等插件，否则 JPG/PNG 可能无法读取
if(WIN32)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
        add_custom_command(TARGET RemoteSensingQtStarter POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:RemoteSensingQtStarter>
                    $<TARGET_FILE_DIR:RemoteSensingQtStarter>
            COMMAND_EXPAND_LISTS
            COMMENT "Copying runtime DLLs for RemoteSensingQtStarter")
    endif()

    get_target_property(QT_QMAKE_EXECUTABLE ${QT_PACKAGE}::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${QT_BIN_DIR}")

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET RemoteSensingQtStarter POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    $<$<CONFIG:Debug>:--debug>
                    $<$<NOT:$<CONFIG:Debug>>:--release>
                    --no-translations
                    "$<TARGET_FILE:RemoteSensingQtStarter>"
            COMMENT "Deploying Qt runtime and plugins with windeployqt")
    else()
        message(WARNING "windeployqt not found. Qt image/platform plugins will not be copied automatically.")
    endif()
endif()

if(RS_HAVE_OPENCV)
    target_compile_definitions(RemoteSensingQtStarter PRIVATE RS_WITH_OPENCV=1)
    if(TARGET opencv_core)
        target_link_libraries(RemoteSensingQtStarter PRIVATE
            opencv_core
            opencv_imgproc
            opencv_features2d
            opencv_calib3d
            opencv_imgcodecs
            opencv_ml)
    else()
        target_link_libraries(RemoteSensingQtStarter PRIVATE ${OpenCV_LIBS})
    endif()
    target_include_directories(RemoteSensingQtStarter SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()

if(GDAL_FOUND)
    target_compile_definitions(RemoteSensingQtStarter PRIVATE RS_WITH_GDAL=1)
    target_link_libraries(RemoteSensingQtStarter PRIVATE GDAL::GDAL)
endif()

if(MSVC)
    target_compile_options(RemoteSensingQtStarter PRIVATE /W4 /permissive- /utf-8)
endif()

add_executable(mesh_test tools/mesh_test.cpp src/pcmesh/PcMeshReconstruction.cpp)
target_include_directories(mesh_test PRIVATE include)
target_link_libraries(mesh_test PRIVATE ${QT_PACKAGE}::Core ${QT_PACKAGE}::Gui)

