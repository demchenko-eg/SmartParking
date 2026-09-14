cmake_minimum_required(VERSION 3.20)
project(SmartParking CXX)

set(CMAKE_CXX_STANDARD 17)

# Знаходимо OpenCV
find_package(OpenCV REQUIRED)

# Вказуємо шляхи до бібліотек та заголовків Homebrew
link_directories(/opt/homebrew/lib)
include_directories(/opt/homebrew/include)

add_executable(SmartParking main.cpp)

# Лінкуємо явно
target_link_libraries(SmartParking PRIVATE
        ${OpenCV_LIBS}
        tesseract
)