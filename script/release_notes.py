import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Повреждённая база статистики и её служебные файлы сохраняются при восстановлении.")
print("- Сборка транспортов использует проверенные резервные исходники, если оригинальный репозиторий недоступен.")
print("- Добавлен пакет для Arch Linux x86_64. Установка: `sudo pacman -U ./qThrone-1.1.3-arch-x86_64.pkg.tar.zst`.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
