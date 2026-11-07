import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print(f"# Rawline {version['version']}")
print()
print("- Добавлен протокол MRTT: профили и ссылки mrtt://, привязка MAX, развёртывание сервера на VPS и управление им из клиента.")
print("- Добавлен FreeTurn: подключение, развёртывание сервера и выдача клиентов, звонки можно создать из привязанного аккаунта VK.")
print("- WS и gRPC для Trojan и VLESS теперь работают на Xray, это исправляет профили с панелей вроде 3x-ui 3.11. Учитывается параметр authority.")
print("- В профилях qWDTT, CSQTT и WDTT Plus можно выбрать хеши из созданных в клиенте.")
print("- Добавлен импорт зашифрованных ссылок happ://crypt и incy://crypt1 и подписок Happ с зашифрованным ответом, а также свои профили маршрутизации Happ и Incy без подписки.")
print("- CottenDNS: ссылки без настроек получают рабочие значения, движок обновлён. Резолверы DNS-туннеля можно задать строками рядом со ссылкой.")
print("- Обновлены OpenFlux до 0.17.1 и MasterDNS, импорт профилей Soar из SlipNet 2.5.6.")
print("- HTTPS-прокси по IP без SNI теперь подключается.")
print("- Проверка задержки по умолчанию идёт через HTTPS.")
print("- Вход VK находит больше браузеров: Chrome и Edge в нестандартных папках, Chromium, Brave и Яндекс Браузер.")
print()
print(f"Основа: Throne {version['rawline_version']}, коммит [{version['rawline_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['rawline_commit']}).")
