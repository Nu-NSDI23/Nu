# Build the app binary
add_executable(main main.cc)

# Link the DataFrame library to the app binary
target_link_libraries(main DataFrame)
