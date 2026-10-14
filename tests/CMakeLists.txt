add_executable(r3d_test_animated_bounds animated_bounds.c)
target_link_libraries(r3d_test_animated_bounds PRIVATE r3d raylib)
target_include_directories(r3d_test_animated_bounds PRIVATE
        "${R3D_ROOT_PATH}/include"
        "${R3D_ROOT_PATH}/external/glad"
)

set(R3D_BOUNDS_READY_TESTS
        validation
        expansion
        cpu_morph
        conservative_morph
        frustum
        transformed
        shadow_frustum
        cluster_envelope
        instance_math
)

set(R3D_BOUNDS_INTEGRATION_TESTS
        model_defaults
        animated_skeleton
        animated_ik
        animated_spring
        morph_skinning
        cluster_submission
)

foreach(test_name IN LISTS R3D_BOUNDS_READY_TESTS)
    add_test(NAME bounds_${test_name} COMMAND r3d_test_animated_bounds ${test_name})
    set_tests_properties(bounds_${test_name} PROPERTIES LABELS "bounds;ready")
endforeach()

foreach(test_name IN LISTS R3D_BOUNDS_INTEGRATION_TESTS)
    add_test(NAME bounds_${test_name} COMMAND r3d_test_animated_bounds ${test_name})
    set_tests_properties(bounds_${test_name} PROPERTIES LABELS "bounds;integration")
endforeach()


# ========================================
# World / Screen Projection Tests
# ========================================

add_executable(r3d_test_projection projection.c)
target_link_libraries(r3d_test_projection PRIVATE r3d raylib)

target_include_directories(r3d_test_projection PRIVATE
        "${R3D_ROOT_PATH}/include"
        "${R3D_ROOT_PATH}/external/glad"
)

set(R3D_PROJECTION_TESTS
        perspective
        orthographic
        roundtrip
        target_sizes
        viewport
        keep_letterbox
        keep_pillarbox
        resolution_independence
        output_scaling
        clipping
        screen_rect
        transformed_rect
        near_plane_rect
        picking
        inside_box
        multiple_cameras
        invalid_input
)

foreach(test_name IN LISTS R3D_PROJECTION_TESTS)
    add_test(NAME projection_${test_name} COMMAND r3d_test_projection ${test_name})
    set_tests_properties(projection_${test_name} PROPERTIES LABELS "projection;headless")
endforeach()