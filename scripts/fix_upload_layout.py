# SPDX-License-Identifier: MIT
# Copyright (c) 2025 M5Stack Technology CO LTD
# Arduino port additions copyright (c) 2026

"""Make PlatformIO's Arduino uploader honor the upstream IDF partition map."""

Import("env")  # type: ignore[name-defined]  # Provided by SCons.


app_offset = int(env.subst("$ESP32_APP_OFFSET"), 0)
if app_offset != 0x20000:
    raise RuntimeError(
        f"Unsafe application upload offset 0x{app_offset:x}; expected ota_0 at 0x20000"
    )

flags = list(env.get("UPLOADERFLAGS", []))
patched = False
for index in range(len(flags) - 1):
    image_path = env.subst(str(flags[index + 1])).replace("\\", "/")
    if image_path.endswith("/boot_app0.bin"):
        current_offset = int(str(flags[index]), 0)
        if current_offset != 0xE000:
            raise RuntimeError(
                f"Unexpected boot_app0 upload offset 0x{current_offset:x}"
            )
        # The official table reserves 0xd000-0xefff for otadata and starts
        # phy_init at 0xf000. Arduino's default 0xe000 would overlap both.
        flags[index] = "0xd000"
        patched = True
        break

if not patched:
    raise RuntimeError("Could not locate PlatformIO boot_app0 upload image")

env.Replace(UPLOADERFLAGS=flags)
print("Upload layout verified: otadata=0xd000, ota_0=0x20000")
