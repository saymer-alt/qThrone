import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Смена сезонного украшения применяется сразу, без предупреждения о перезапуске соединения.")
print("- Хэллоуин и Новый год сохраняют постоянный размер и скрываются при нехватке места.")
print("- Удалён повторный пункт «Новый год» в настройках интерфейса.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
