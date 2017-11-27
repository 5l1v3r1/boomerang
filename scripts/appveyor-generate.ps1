
$QT_VERSION="5.9.2"

if ($env:APPVEYOR_BUILD_WORKER_IMAGE -eq "Visual Studio 2017") {
    $CMAKE_GENERATOR_NAME = "Visual Studio 15 2017 Win64"
    $QT_BASE_DIR = "C:\Qt\$QT_VERSION\msvc2017_64"
}
if ($env:APPVEYOR_BUILD_WORKER_IMAGE -eq "Visual Studio 2015") {
    $CMAKE_GENERATOR_NAME = "Visual Studio 14 2015 Win64"
    $QT_BASE_DIR = "C:\Qt\$QT_VERSION\msvc2015_64"
}

$QT_CORE_DIR    = "$QT_BASE_DIR\lib\cmake\Qt5Core"
$QT_GUI_DIR     = "$QT_BASE_DIR\lib\cmake\Qt5Gui"
$QT_TEST_DIR    = "$QT_BASE_DIR\lib\cmake\Qt5Test"
$QT_WIDGETS_DIR = "$QT_BASE_DIR\lib\cmake\Qt5Widgets"
$QT_XML_DIR     = "$QT_BASE_DIR\lib\cmake\Qt5Xml"

$CMAKE_PREFIX_PATH="$QT_CORE_DIR;$QT_GUI_DIR;$QT_TEST_DIR;$QT_WIDGETS_DIR;$QT_XML_DIR"

echo "CMAKE_PREFIX_PATH = $CMAKE_PREFIX_PATH"
echo ""

# Build Visual Studio solution
cmake -G "$CMAKE_GENERATOR_NAME" -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" -DBOOMERANG_BUILD_TESTS=ON ..
