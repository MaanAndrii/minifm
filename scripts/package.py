"""Package source and separately addressed flash images; never erase NVS."""
from pathlib import Path
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parent.parent
dist = root / "dist"
dist.mkdir(exist_ok=True)
images = {
    "bootloader.bin": (root / "build/bootloader/bootloader.bin", "0x0"),
    "partition-table.bin": (root / "build/partition_table/partition-table.bin", "0x8000"),
    "amoled_radio.bin": (root / "build/amoled_radio.bin", "0x10000"),
}
application = images["amoled_radio.bin"][0]
source_files = [file for file in (root / "main").rglob("*") if file.is_file()]
if application.stat().st_mtime < max(file.stat().st_mtime for file in source_files):
    raise SystemExit("Firmware is older than source files. Complete scripts/Build.ps1 before packaging.")
manifest = {"target": "esp32s3", "flash_mode": "dio", "flash_frequency": "80m",
            "flash_size": "16MB", "idf": "5.5.5", "board_validation": "pending", "images": {}}
with zipfile.ZipFile(dist / "amoled-radio-firmware.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    for name, (file, address) in images.items():
        data = file.read_bytes()
        manifest["images"][name] = {"address": address, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        archive.writestr(name, data)
    archive.writestr("manifest.json", json.dumps(manifest, indent=2))
    archive.writestr("FLASH.txt", "ESP32-S3-Touch-AMOLED-2.16; hardware revision must be checked before flashing.\n"
                     "Physical validation has not been performed.\n"
                     "Write bootloader.bin at 0x0, partition-table.bin at 0x8000, amoled_radio.bin at 0x10000.\n"
                     "Flash mode DIO, frequency 80 MHz, size 16 MB. No full erase is required.\n"
                     "With esptool installed, from the extracted directory:\n"
                     "python -m esptool --chip esp32s3 -p COM5 -b 460800 write_flash --flash_mode dio "
                     "--flash_freq 80m --flash_size 16MB 0x0 bootloader.bin 0x8000 partition-table.bin "
                     "0x10000 amoled_radio.bin\nReplace COM5 with the actual device port.\n")
    archive.write(root / "README.md", "README.md")
    archive.write(root / "LICENSE.esp_audio_codec", "LICENSE.esp_audio_codec")

files = []
for directory in ("main", "scripts", "tests"):
    files.extend(p for p in (root / directory).rglob("*") if p.is_file() and "__pycache__" not in p.parts)
for name in ("CMakeLists.txt", "sdkconfig.defaults", "partitions.csv", "dependencies.lock", "README.md",
             "PROJECT.md", "IMPLEMENTATION_STATUS.md", ".gitignore", "LICENSE.esp_audio_codec"):
    files.append(root / name)
with zipfile.ZipFile(dist / "amoled-radio-source.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    for file in sorted(files):
        archive.write(file, "esp32-radio/" + file.relative_to(root).as_posix())
print(json.dumps(manifest, indent=2))
print("Source and firmware packages created in", dist)
