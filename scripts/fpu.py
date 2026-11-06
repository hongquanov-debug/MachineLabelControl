"""
PlatformIO pre-script: enable the Cortex-M4F hardware FPU.

The ststm32 'stm32cube' builder only passes "-mthumb -mcpu=cortex-m4", which
compiles for a CPU without FPU. FreeRTOS' ARM_CM4F port (and fast float math
in the motion ramps) needs the FPU, otherwise the assembler rejects
vstmdb/vldmia in port.c.

The flags are added to the global environment in a *pre* script, so the HAL
library, FreeRTOS, project sources and the linker all use the same float ABI.

Option (platformio.ini):
    custom_float_abi = hard     ; hard (default) | softfp
"""
Import("env")  # noqa: F821  (provided by SCons)

abi = env.GetProjectOption("custom_float_abi", "hard")  # noqa: F821
if abi not in ("hard", "softfp"):
    raise ValueError("custom_float_abi must be 'hard' or 'softfp'")

fpu_flags = ["-mfpu=fpv4-sp-d16", "-mfloat-abi=%s" % abi]

env.Append(  # noqa: F821
    CCFLAGS=fpu_flags,
    ASFLAGS=fpu_flags,
    LINKFLAGS=fpu_flags,
)
