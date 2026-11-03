# Noclip Death Sound — аддон для Mega Hack v9

Проигрывает звук смерти, когда ты врезаешься во что-то с включённым ноуклипом.

- Geometry Dash **2.2081** (Steam, Windows)
- Mega Hack **v9.1.3**
- Geode **5.6.1 или новее** (Mega Hack v9 сам работает через Geode)

## Как это работает

Ноуклип Mega Hack не мешает игре вызывать `PlayLayer::destroyPlayer`, он только не даёт
игроку умереть. Аддон ставит свой хук на эту функцию раньше Mega Hack и после вызова
смотрит: если игрок задел препятствие, но остался жив, значит это «смерть» в ноуклипе,
и аддон играет звук. Касание длится несколько тиков физики подряд, поэтому звук
играет один раз на каждое касание, а не каждый кадр.

При настоящей смерти (ноуклип выключен) аддон ничего не делает, звук играет сама игра.

## Настройки

В меню Mega Hack (Tab) появится вкладка **Noclip Death Sound**:

| Опция | Что делает |
| --- | --- |
| Death Sound | включить/выключить |
| Volume | громкость (0–2) |
| Use Game SFX Volume | учитывать ползунок SFX в настройках игры |
| Cooldown (sec) | минимальная пауза между звуками |
| Player 1 / Player 2 | для какого игрока в дуале играть звук |
| Sound File | путь к своему звуку (.ogg/.mp3/.wav); пусто = стандартный `explode_11.ogg` |
| Test Sound | проиграть звук |
| Open Geode Settings | окно настроек Geode с выбором файла через проводник |

Эти же настройки есть в Geode (Geode → Noclip Death Sound → ⚙). Если вкладка в Mega Hack
не появилась, аддон всё равно работает, настраивай его через Geode.

## Сборка

### Через GitHub (без установки компилятора)

1. Создай новый репозиторий на GitHub и загрузи туда содержимое этой папки.
2. Открой **Actions → Build Geode Mod → Run workflow** (или просто сделай push).
3. Когда сборка закончится, скачай артефакт `NoclipDeathSound`. В нём будет
   `vasyaodnokl.noclip-death-sound.geode`.

### Локально (Windows)

Нужны Visual Studio 2022 (C++), CMake и [Geode CLI](https://docs.geode-sdk.org/getting-started/geode-cli).

```bat
geode sdk install
geode sdk install-binaries
geode build
```

Готовый файл появится в `build/`.

## Установка

Перетащи `vasyaodnokl.noclip-death-sound.geode` в
`Geometry Dash/geode/mods/` (или Geode → кнопка «папка» → mods) и перезапусти игру.
Внутри `.geode` лежит сама DLL. Голую DLL отдельно подкидывать не нужно, Geode
загружает мод только в виде `.geode`.

## Файлы

- `src/main.cpp` — хук, логика звука, вкладка Mega Hack
- `src/mh/` — обёртка над API расширений Mega Hack (`hackpro::extensions`),
  взята из [slgman/Mega-Hack-Extensions](https://github.com/slgman/Mega-Hack-Extensions)
- `mod.json` — метаданные и настройки Geode
