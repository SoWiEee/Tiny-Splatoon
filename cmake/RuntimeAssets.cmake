function(tiny_stage_runtime_assets target source_dir)
    file(GLOB_RECURSE assets CONFIGURE_DEPENDS LIST_DIRECTORIES false
        RELATIVE "${source_dir}" "${source_dir}/*")
    set(stage_dir "${CMAKE_CURRENT_BINARY_DIR}/runtime-assets/${target}")
    string(REPLACE ";" "\n" manifest "${assets}")
    file(GENERATE OUTPUT "${stage_dir}/files.txt" CONTENT "${manifest}")
    set(dependencies)
    foreach(asset IN LISTS assets)
        list(APPEND dependencies "${source_dir}/${asset}")
    endforeach()
    set(stamp "${stage_dir}/$<CONFIG>/staged.stamp")
    add_custom_command(OUTPUT "${stamp}"
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${source_dir}"
            "-DDESTINATION_DIR=$<TARGET_FILE_DIR:${target}>/assets"
            "-DFILES_MANIFEST=${stage_dir}/files.txt"
            "-DOWNED_MANIFEST=${stage_dir}/$<CONFIG>/owned.txt"
            "-DSTAMP=${stamp}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageRuntimeAssets.cmake"
        DEPENDS ${dependencies} "${stage_dir}/files.txt"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageRuntimeAssets.cmake"
        COMMENT "Staging ${target} runtime assets"
        VERBATIM)
    add_custom_target(${target}-assets DEPENDS "${stamp}")
    add_dependencies(${target} ${target}-assets)
endfunction()
