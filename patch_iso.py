import os

def patch_ps2_iso(iso_path, target_elf_name, new_driver_path):
    SEARCH_PATTERNS = [
        b"rom0:SMAP",
        b"SMAP.IRX"
    ]
    if len(new_driver_path) != 9:
        print("Ошибка: Длина нового пути должна быть строго 9 символов!")
        return
    new_pattern = new_driver_path.encode('ascii')
    print(f"[*] Открываем образ: {iso_path}")
    with open(iso_path, "r+b") as iso_file:
        chunk_size = 1024 * 1024
        offset = 0
        patched_count = 0
        while True:
            chunk = iso_file.read(chunk_size)
            if not chunk:
                break
            for pattern in SEARCH_PATTERNS:
                start_idx = 0
                while True:
                    idx = chunk.find(pattern, start_idx)
                    if idx == -1:
                        break
                    absolute_position = offset + idx
                    iso_file.seek(absolute_position)
                    iso_file.write(new_pattern)
                    print(f"[+] Успешно пропатчено по смещению {hex(absolute_position)}")
                    patched_count += 1
                    start_idx = idx + len(pattern)
            offset += len(chunk)
            iso_file.seek(offset)
    if patched_count > 0:
        print(f"\n[!] Успешно заменено путей: {patched_count}")
    else:
        print("\n[-] Сетевой драйвер SMAP не найден.")

if __name__ == "__main__":
    ISO_FILE = "game.iso" 
    ELF_NAME = "SLUS_XXX.XX" 
    NEW_PATH = "mc0:SMAP\x00" 
    if os.path.exists(ISO_FILE):
        patch_ps2_iso(ISO_FILE, ELF_NAME, NEW_PATH)
    else:
        print(f"Файл {ISO_FILE} не найден.")
