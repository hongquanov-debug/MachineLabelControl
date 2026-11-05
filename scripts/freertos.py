"""
PlatformIO pre-script: build FreeRTOS kernel + CMSIS-RTOS v2 wrapper directly
from the STM32CubeF4 framework package (framework-stm32cubef4).

The stm32cube builder only compiles HAL/CMSIS, so middlewares must be added
manually. FreeRTOSConfig.h lives in the project 'include' directory.

Options (platformio.ini):
    custom_freertos_heap = heap_4    ; heap_1 .. heap_5
"""
from os.path import isdir, join

Import("env")  # noqa: F821  (provided by SCons)

platform = env.PioPlatform()  # noqa: F821
framework_dir = platform.get_package_dir("framework-stm32cubef4")
if not framework_dir or not isdir(framework_dir):
    raise RuntimeError("framework-stm32cubef4 package not found")

rtos_dir = join(framework_dir, "Middlewares", "Third_Party", "FreeRTOS", "Source")
if not isdir(rtos_dir):
    raise RuntimeError("FreeRTOS not found in %s" % rtos_dir)

heap = env.GetProjectOption("custom_freertos_heap", "heap_4")  # noqa: F821

env.Append(  # noqa: F821
    CPPPATH=[
        "$PROJECT_INCLUDE_DIR",  # FreeRTOSConfig.h
        join(rtos_dir, "include"),
        join(rtos_dir, "CMSIS_RTOS_V2"),
        join(rtos_dir, "portable", "GCC", "ARM_CM4F"),
    ]
)

env.BuildSources(  # noqa: F821
    join("$BUILD_DIR", "FreeRTOS"),
    rtos_dir,
    src_filter=[
        "-<*>",
        "+<croutine.c>",
        "+<event_groups.c>",
        "+<list.c>",
        "+<queue.c>",
        "+<stream_buffer.c>",
        "+<tasks.c>",
        "+<timers.c>",
        "+<CMSIS_RTOS_V2/cmsis_os2.c>",
        "+<portable/GCC/ARM_CM4F/port.c>",
        "+<portable/MemMang/%s.c>" % heap,
    ],
)
