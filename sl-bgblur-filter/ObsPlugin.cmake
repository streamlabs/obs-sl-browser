## -- OBS Plugin

# Directory where this .cmake file lives
set(_this_dir "${CMAKE_CURRENT_LIST_DIR}")

# Make it easy to include relative cmake files
include("${_this_dir}/cmake/FetchOnnxruntime.cmake")
include("${_this_dir}/cmake/FetchOpenCV.cmake")

add_library(sl-bgblur-filter MODULE)
add_library(OBS::sl-bgblur-filter ALIAS sl-bgblur-filter)

target_link_libraries(sl-bgblur-filter PRIVATE OBS::libobs)
target_link_libraries(sl-bgblur-filter PRIVATE Ort OpenCV)

target_link_options(sl-bgblur-filter PRIVATE 
  "/IGNORE:4099" # Ignore PDB warnings
)

target_sources(sl-bgblur-filter PRIVATE
	"${_this_dir}/sl-bgblur-filter.cpp"
	"${_this_dir}/BgBlur.cpp"
	"${_this_dir}/BgBlurGraphics.cpp"
	"${_this_dir}/FilterData.cpp"
)

add_custom_command(TARGET sl-bgblur-filter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Ort::DirectML>"
        $<TARGET_FILE_DIR:sl-bgblur-filter>
)

add_custom_command(TARGET sl-bgblur-filter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
       "${_this_dir}/data/models"
       $<TARGET_FILE_DIR:sl-bgblur-filter>/models
)

add_custom_command(TARGET sl-bgblur-filter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
       "${_this_dir}/data/effects"
       $<TARGET_FILE_DIR:sl-bgblur-filter>/effects
)
