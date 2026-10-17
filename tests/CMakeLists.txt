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

# ========================================
# Shader Diagnostics Tests (OpenGL)
# ========================================

add_executable(r3d_test_shader_diagnostics shader_diagnostics.c)
target_link_libraries(r3d_test_shader_diagnostics PRIVATE r3d raylib)
target_include_directories(r3d_test_shader_diagnostics PRIVATE
        "${R3D_ROOT_PATH}/include"
)

option(R3D_TESTS_GPU "Register tests requiring an OpenGL context" OFF)

if(R3D_TESTS_GPU)
    set(R3D_SHADER_DIAGNOSTIC_TESTS
            valid
            invalid_vertex
            invalid_fragment
    )

    foreach(test_name IN LISTS R3D_SHADER_DIAGNOSTIC_TESTS)
        add_test(
                NAME shader_${test_name}
                COMMAND r3d_test_shader_diagnostics ${test_name}
        )
        set_tests_properties(shader_${test_name} PROPERTIES
                LABELS "shader;gpu"
        )
    endforeach()
endif()


# ========================================
# Manifold Native Integration Test
# ========================================

if(R3D_SUPPORT_MANIFOLD)
    add_executable(r3d_test_manifold manifold_smoke.c)
    target_link_libraries(r3d_test_manifold PRIVATE r3d manifold::manifoldc)

    add_test(NAME manifold_native COMMAND r3d_test_manifold)
    set_tests_properties(manifold_native PROPERTIES LABELS "manifold;headless")

    add_executable(r3d_test_text_mesh text_mesh_smoke.c)

    target_link_libraries(r3d_test_text_mesh PRIVATE
            r3d
            manifold::manifoldc
            raylib
    )

    target_include_directories(r3d_test_text_mesh PRIVATE
            "${R3D_ROOT_PATH}/include"
    )

    target_compile_definitions(r3d_test_text_mesh PRIVATE
            RESOURCES_PATH="${R3D_ROOT_PATH}/examples/resources/"
    )

    add_test(
            NAME manifold_text_mesh
            COMMAND r3d_test_text_mesh
    )

    set_tests_properties(manifold_text_mesh PROPERTIES
            LABELS "manifold;text;headless"
    )

    # Standalone CPU benchmark.
    # Not registered with CTest: benchmarking should be
    # explicitly requested, not part of ordinary CI.
    add_executable(
            r3d_benchmark_text_mesh
            text_mesh_benchmark.c
    )

    target_link_libraries(r3d_benchmark_text_mesh PRIVATE
            r3d
            raylib
            manifold::manifoldc
    )

    target_include_directories(r3d_benchmark_text_mesh PRIVATE
            "${R3D_ROOT_PATH}/include"
    )

    target_compile_definitions(r3d_benchmark_text_mesh PRIVATE
            RESOURCES_PATH="${R3D_ROOT_PATH}/examples/resources/"
    )
endif()

# ========================================
# Matrix Composition Helpers
# ========================================

add_executable(r3d_test_matrix_helpers matrix_helpers.c)

target_link_libraries(r3d_test_matrix_helpers PRIVATE
        r3d
        raylib
)

target_include_directories(r3d_test_matrix_helpers PRIVATE
        "${R3D_ROOT_PATH}/include"
)

add_test(
        NAME matrix_helpers
        COMMAND r3d_test_matrix_helpers
)

set_tests_properties(matrix_helpers PROPERTIES
        LABELS "matrix;headless"
)