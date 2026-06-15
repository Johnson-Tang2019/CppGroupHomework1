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
    src/Algorithms.cpp
    src/ExtendedAlgorithms.cpp
    src/RemoteSensingIndices.cpp
    src/RasterIO.cpp
    src/RasterRenderDialog.cpp
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
    include/rs/Scene3DWidget.h    #增加
    include/rs/pcmesh/PcMeshReconstruction.h
)

# Windows EXE 图标：将 .ico 复制到构建目录，确保 RC 编译器能找到
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

target_include_directories(RemoteSensingQtStarter PRIVATE include)
target_link_libraries(RemoteSensingQtStarter PRIVATE ${QT_PACKAGE}::Widgets ${QT_PACKAGE}::OpenGL ${QT_PACKAGE}::OpenGLWidgets ${QT_PACKAGE}::Concurrent ${QT_PACKAGE}::Network opengl32)

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

