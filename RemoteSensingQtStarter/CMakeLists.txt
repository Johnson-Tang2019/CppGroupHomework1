cmake_minimum_required(VERSION 3.16)
project(RemoteSensingQtStarter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Concurrent)
set(QT_PACKAGE Qt6)

find_package(OpenCV QUIET COMPONENTS core imgproc features2d calib3d)
find_package(GDAL QUIET)

find_package(OpenCV REQUIRED)

add_executable(RemoteSensingQtStarter WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/Algorithms.cpp
    src/RasterIO.cpp
    src/RasterRenderDialog.cpp
    src/Scene3DWidget.cpp
    include/rs/DataObject.h
    include/rs/Geometry.h
    include/rs/LayerManager.h
    resources/app.rc
    include/rs/ProcessingAlgorithm.h
    include/rs/Algorithms.h
    include/rs/RasterIO.h
    include/rs/RasterRenderDialog.h
    include/rs/MainWindow.h
    include/rs/Scene3DWidget.h    #增加
)

# Windows EXE 图标：将 .ico 复制到构建目录，确保 RC 编译器能找到
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(RemoteSensingQtStarter PRIVATE ${OpenCV_LIBS})

target_include_directories(RemoteSensingQtStarter PRIVATE include)
target_link_libraries(RemoteSensingQtStarter PRIVATE ${QT_PACKAGE}::Widgets ${QT_PACKAGE}::OpenGL ${QT_PACKAGE}::OpenGLWidgets ${QT_PACKAGE}::Concurrent opengl32)

if(OpenCV_FOUND)
    target_compile_definitions(RemoteSensingQtStarter PRIVATE RS_WITH_OPENCV=1)
    target_link_libraries(RemoteSensingQtStarter PRIVATE ${OpenCV_LIBS})
    # 将 OpenCV 头文件添加为 SYSTEM 类型，避免其干扰 MSVC 标准库搜索路径
    target_include_directories(RemoteSensingQtStarter SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()

if(GDAL_FOUND)
    target_compile_definitions(RemoteSensingQtStarter PRIVATE RS_WITH_GDAL=1)
    target_link_libraries(RemoteSensingQtStarter PRIVATE GDAL::GDAL)
endif()

if(MSVC)
    target_compile_options(RemoteSensingQtStarter PRIVATE /W4 /permissive- /utf-8)
endif()

