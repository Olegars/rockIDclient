# rockIDclient

Клиент мессенджера rockID для Windows. Подключается к [rockIDserver](https://github.com/Olegars/rockIDserver) по TCP: кадр `uint32` длины (big-endian) и JSON.

Отладочный сервер слушает все интерфейсы, порт **7410**. Клиент по умолчанию подключается к `192.168.222.2:7410`. Адрес можно сменить на экране входа и в меню сервера внутри чатов.

## Сборка

Нужен Qt 6.12 (MSVC 2022, 64-bit), путь `C:\Qt_new\6.12.0\msvc2022_64`.

```powershell
cmake --preset qt6.12-msvc-release
cmake --build --preset qt6.12-msvc-release
```

Запуск из каталога сборки. Рядом с exe должны быть библиотеки Qt (`windeployqt` или `PATH` к `C:\Qt_new\6.12.0\msvc2022_64\bin`).

Токен сессии хранится в реестре (`HKCU\Software\rockID\rockIDclient`). Пароль не сохраняется.

## Что умеет

Вход, регистрация, повторный вход по токену (`auth.bind`), личные чаты и группы, контакты, поиск по `username`, история, непрочитанные, присутствие. Пуши: `message.new`, `dialog.new`, `dialog.member`, `presence`.
