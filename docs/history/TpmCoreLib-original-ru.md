> Текущее состояние: ядро выполняет Manufacture/_TPM_Init и настоящие команды
> Startup/GetCapability/SelfTest/GetRandom в изолированной SMM boot-пробе.
> Ниже ранние этапы описывают отдельные smoke-функции; ограничения этих этапов
> не означают отсутствие последующих блоков. Runtime-транспорт теперь подключён
> через частный SMM protocol для двух diagnostic TPM requests; after-EBS тест ожидается.

# TpmCoreLib: перенос Microsoft TPM в EDK II

Исходная папка `/home/anecdote/Projects/ms-tpm-20-ref/TPMCmd` использована только
для чтения. В `Vendor/` находится точная копия C-кода, заголовков и include-файлов;
`UPSTREAM_SHA256.json` содержит исходные контрольные суммы. При переносе сохранены
авторские комментарии. LICENSE.Microsoft-TCG получен из официального upstream:
https://github.com/microsoft/ms-tpm-20-ref/blob/main/LICENSE

## Состав

- `TpmCoreLib.inf`: статическая библиотека TPM и адаптеров Ossl/TpmBigNum.
- `Config/`: отдельный профиль firmware; не меняет конфигурацию Vendor.
- `Compat/`: согласование типов, макросов и CRT со средой EDK II.
- `Port/`: исправления для начального профиля и платформенный RAM-NV backend
  `TpmNv.c`, реализующий интерфейс из upstream `Platform/src/NVMem.c`.
- `EdkHeaders/`: копии заголовков CRT и OpenSSL текущего EDK II. Реализация
  криптографии линкуется из существующего CryptoPkg, не копируется и не обновляется.
  `EDK_HEADERS_SHA256.json` указывает источники заголовков и их суммы.
- `TpmCoreProbe.c`: тестовый фасад без инициализации TPM.
- `../TpmCoreProbeSmm.c`: отдельный SMM-модуль для вызова фасада.

## Что проверяется при загрузке

1. Настоящие UINT32_Marshal/Unmarshal из Microsoft-кода: big-endian и round trip.
2. Отказ Unmarshal при коротком буфере без чтения за границей.
3. Наличие SHA256 и AES в таблице возможностей Microsoft-кода.
4. SHA256("abc") с проверкой известного digest.
5. AES-128 одного блока с проверкой известного ciphertext.

Последние два теста проверяют существующий backend OpenSSL, а не выполнение
команд TPM2_Hash / TPM2_EncryptDecrypt. Полная инициализация, энтропия,
Manufacture, ExecuteCommand и ключи на этом этапе не вызываются.
Компиляция статической библиотеки не доказывает готовность этих путей:
пока финальный модуль использует только функции, достижимые из smoke test.

Ожидается:

```text
[VTPM-CORE] Isolated core smoke test starting
[VTPM-CORE] marshal=PASS bounds=PASS profile=PASS sha256=PASS aes128=PASS
[VTPM-CORE] PASS: isolated core smoke test; boot continues
```

Штатный TpmFail перехватывается на границе пробы через SetJump/LongJump, чтобы
записать ошибку и продолжить загрузку. Это не обработчик произвольных CPU faults
и не гарантия восстановления при любом сбое. `_plat__Fail` теперь реализован в Port/TpmRun.c. До инициализации используется
временная граница пробы; для initialization/Submit — собственная граница ядра.

## Начальные ограничения профиля

- SIMULATION, CRYPTO_LIB_REPORTING и ALLOW_FORCE_FAILURE_MODE отключены.
- DEBUG Microsoft-кода равен 0; DEBUG_INFO загрузочного SMM-модуля сохраняется.
- Self-tests включены; полный TPM2_SelfTest запускается в command-пробе.
- ECC и Camellia отключены, поскольку их нет в OpenSSL конфигурации этого EDK II.
  Это ограничение первой интеграции, не окончательный набор возможностей TPM.
- CertifyX509 отключён; X509_ECC.c не включён в INF данного профиля.
- Для профиля без ECC: Port/TpmBigNumThunks.c исключает только ECC wrappers;
  Port/AlgorithmTests.c выносит TestKDFa из ECC guard, сохраняя тест KDF.
- TpmMemory.c даёт функцию копирования, адрес которой можно использовать в
  hash descriptors; function-like CRT macro EDK II этого не позволяет.

RAM NV теперь проверяется отдельно. Следующий блок: надёжная энтропия,
время и инициализация, затем Startup/GetCapability/SelfTest/GetRandom через
ExecuteCommand. Перед постоянными ключами необходима реализация постоянного NV.

## Сборка

Проверено 2026-10-05: отдельный модуль и полный OVMF DEBUG/GCC5 собраны
в существующем Ubuntu 16.04 контейнере. Отдельная загрузка QEMU q35/TCG,
SMM включён, S3 отключён, 2 CPU, 2 GiB RAM, без гостевых дисков, показала
PASS всех пяти проверок и дальнейшее выполнение старых DXE/SMM-проб.
Лог: `../Tests/core-smoke.log`. Пользователь подтвердил загрузку Windows на KVM: лог
`edk2_debug_2026-10-05_23-16-50.log` содержит PASS ядра и успешный обмен после
ExitBootServices. Эта проверка предшествовала добавлению RAM NV.

Пакет уже подключён в OvmfPkgX64.dsc / .fdf под SMM_REQUIRE. Полная сборка:

```sh
build -p OvmfPkg/OvmfPkgX64.dsc -a X64 -b DEBUG -t GCC5 -D SMM_REQUIRE=TRUE
```

Сохраняйте свои дополнительные флаги сборки. Предыдущие VtpmProbeSmm/Dxe
остаются подключёнными и должны продолжать проходить их проверки.

## Второй блок: платформенная NV-память

`Port/TpmNv.c` реализует `_plat__NVEnable`, `_plat__NVDisable`,
`_plat__GetNvReadyState`, `_plat__NvMemoryRead/Write/Clear/Move`,
`_plat__NvGetChangedStatus`, `_plat__NvCommit`, `_plat__TearDown`.
Размер берётся из профиля ядра (`NV_MEMORY_SIZE`, сейчас 16384 байта).
Массив принадлежит SMM-модулю, не общей ACPI-памяти. Boot Services,
файловая система и переменные UEFI не используются.

- Первая активация заполняет NV значением 0xff; повторная активация сохраняет RAM.
- CopyMem поддерживает перекрытие областей в обе стороны.
- Проверки границ используют вычитание, защищая от переполнения offset+size.
- Нулевая длина допускается, в том числе offset == NV_MEMORY_SIZE и NULL buffer.
- Неверный диапазон/NULL при ненулевой длине вызывает штатный TpmFail.
- Commit возвращает ошибку при недоступном NV; при доступном подтверждает только
  состояние RAM. Постоянного хранения и восстановления образа здесь ещё нет.
- TearDown обнуляет весь массив volatile-записями и отключает backend.

`VtpmNvSmokeTest` — разрушительная проба до Manufacture. Она проверяет чистое
состояние, последние байты NV, сравнение изменённых данных, перенос с перекрытием,
граничные условия, отключение/повторную активацию и стирание. Проверка некорректных
диапазонов вызывает внутренние валидаторы, а не TpmFail: сам путь failure mode
в этом блоке отдельным тестом не проверяется. После пробы NV обнулено и отключено.
Нельзя вызывать эту пробу после создания настоящего состояния/ключей TPM.

Ожидаемые новые строки:

```text
[VTPM-NV] erase=PASS read-write=PASS overlap=PASS bounds=PASS lifecycle=PASS
[VTPM-NV] PASS: volatile SMM storage; no persistence; mask=0x1F
```

Это тест платформенного хранилища, а не выполнение TPM2_NV_Write/Read.
Лог отдельной загрузки этой версии: `../Tests/nv-smoke.log`.

Проверено: отдельный модуль и полный OVMF DEBUG/GCC5 успешно собраны.
QEMU q35/TCG, SMM включён, без гостевых дисков: пять NV-проверок PASS,
пять прежних проверок ядра PASS, прежние DXE/SMM-пробы PASS; ASSERT отсутствует.
Этот запуск остановлен в firmware и не проверяет NV после ExitBootServices;
новый backend не зависит от Boot Services, но его runtime-интеграция ещё впереди.

## Третий блок: источник энтропии

`Port/TpmEntropy.c` реализует `_plat__GetEntropy` через BaseLib `AsmRdRand32`.
Перед первым вызовом проверяется CPUID ECX bit 30. При отсутствии поддержки
инструкция не выполняется; backend возвращает -1. Каждое получение ограничено
10 попытками. Первый блок отбрасывается; следующие сравниваются с предыдущим.
Отказ или повторение блока фиксируется до завершения жизни SMM-модуля.
Повторная инициализация (amount=0) не снимает ошибку. Положительный вызов выдаёт
не более четырёх байт, как upstream; DRBG должен собирать частичные результаты.

Нет зависимости от Boot Services, времени, rand() или гостевой ОС. Не подключён
BaseRngLib, чей constructor в этой версии EDK вызывает ASSERT на CPU без RDRAND.
RDRAND служит начальным источником для стенда; его наличие не доказывает качество
энтропии и не означает сертификацию TPM/RNG. TPM DRBG теперь инициализируется в отдельной command-пробе.
Нужна отдельная оценка источника для выбранного физического ПК.

Boot probe проверяет инициализацию, короткий запрос с защитными байтами,
частичный результат и серию из 64 обращений. Случайные значения не выводятся.
Ожидается:

```text
[VTPM-RNG] init=PASS short=PASS partial=PASS repeat=PASS
[VTPM-RNG] PASS: RDRAND source; mask=0xF
```

CPU гостя должен предоставлять RDRAND. Без него ожидается RNG FAIL,
а диагностическая загрузка продолжится. Это отказ источника, не работающий TPM.
Дальше: часы и оставшийся platform layer, затем DRBG/Manufacture/ExecuteCommand.

Проверки: полный OVMF DEBUG/GCC5 собран; QEMU с RDRAND показал все четыре RNG
PASS, без RDRAND — ожидаемый RNG FAIL с продолжением загрузки. Прежние пробы
ядра/NV/транспорта проходят в обеих конфигурациях, ASSERT нет. Логи:
`../Tests/entropy-smoke.log`, `../Tests/entropy-no-rdrand.log`.
Воспроизведение из корня edk2: `python3 MyTpm2EMU/Tests/run-entropy-smoke.py`.
RNG-проба подтверждена пользователем; лог `edk2_debug_2026-10-05_23-37-51.log`.

## Четвёртый блок: часы TPM

`Port/TpmClock.c` реализует TimerRead, TimerWasReset/Stopped, ClockRateAdjust,
а также платформенные TimerReset/Restart. TSC калибруется один раз за 20 ms
по ACPI TimerLib. Короткий ACPI-счётчик используется только для калибровки:
при редких runtime-командах читать его как основной источник времени нельзя,
поскольку между запросами могут пройти несколько оборотов.

Время выдаётся в миллисекундах; дробные части сохраняются между чтениями.
ClockRateAdjust использует шаги и пределы upstream: divisor 30000 +/- 5000,
шаги 1/30/300. Перед изменением скорости учитывается время со старой скоростью.
Признаки reset/stopped очищаются при чтении; TimerRestart не обнуляет время.
Если TSC пошёл назад, накопленное время сохраняется, epoch обновляется,
а stopped flag сообщает о разрыве. Boot Services и время ОС не используются.

Это начальный x86 backend. Он предполагает стабильную частоту TSC между
калибровками и согласованный TSC между CPU. Suspend/resume, миграция VM,
долгосрочная точность и работа на физическом ПК пока не проверены; для выбранной
платформы нужны проверка invariant TSC, политика разрывов и доверенный источник.
Калибровка рассчитана на короткий возрастающий ACPI-счётчик OVMF.

Проба проверяет read-clear flags, продвижение времени после задержки,
монотонность серии чтений, restart без сброса, пределы скорости и накопление
дробных частей. Последние проверки используют тот же расчёт времени с заданными
интервалами; это не измерение физической точности часов. Проба сбрасывает часы
до будущего Manufacture. Ожидается:

```text
[VTPM-CLOCK] flags=PASS advance=PASS monotonic=PASS restart=PASS rate=PASS
[VTPM-CLOCK] PASS: calibrated TSC; mask=0x1F
```

Проверено: полная сборка OVMF DEBUG/GCC5, QEMU q35/TCG со свежими VARS и без
гостевых дисков. Все пять clock checks PASS (маска 0x1F), прежние пробы
core/NV/RNG/transport PASS, ASSERT нет. Лог: `../Tests/clock-smoke.log`.
Воспроизведение: `python3 MyTpm2EMU/Tests/run-clock-smoke.py` из корня edk2.
Новая clock-проба на пользовательском KVM/Windows стенде ещё не подтверждена.

## Пятый блок: PCR, сведения о платформе и сигналы

`Port/TpmPcr.c` адаптирован из reference `Platform/src/PlatformPcr.c`.
Сохранены 24 PCR, атрибуты reset/extend localities, stateSave и группы auth/policy.
Таблица reference имеет собственные расширения для PCR 20–22; это ещё не
подтверждение соответствия PC Client profile. Активные банки по умолчанию —
SHA256 и SHA384, если эти алгоритмы включены в профиле.
Инициализация статических PCR — нули; динамических (reset locality 4) — 0xff;
последний байт PCR0 получает startup locality, как в reference.
Неизвестный алгоритм, неверный PCR, короткий/NULL буфер проверяются до записи;
неизвестный алгоритм больше не может привести к записи по индексу size-1.

`Port/TpmPlatform.c` адаптирует интерфейсы VendorInfo, ExtraData, PowerPlat,
LocalityPlat, Cancel и PPPlat:

- Производитель — проектный код VTPM, vendor strings — EDK2/vTPM/EMU,
  firmware version — 1:0. Код не является зарегистрированным TCG manufacturer ID.
- Optional platform manufacture data заполняется 0xff, секретов там нет.
- WasPowerLost возвращает флаг и очищает его; доверенный VtpmPlatformPowerOn
  сбрасывает часы, locality/cancel и выставляет power-lost для будущего _TPM_Init.
- LocalityGet/Set сохраняют reference-правило: 0–4 и 32–255 допустимы,
  зарезервированные 5–31 нормализуются в 0. В runtime locality должен задаваться
  доверенным транспортом; доступ ОС к этому setter ещё не опубликован.
- IsCanceled/SetCancel/ClearCancel работают с внутренним volatile-флагом.
  Это ещё не реализация CRB cancel: доставка отмены во время команды впереди.
- PhysicalPresenceAsserted возвращает FALSE. Подтверждения физического присутствия
  из гостевой ОС или интерфейса прошивки пока нет.

ACT_SUPPORT, FW_LIMITED_SUPPORT и SVN_LIMITED_SUPPORT отключены в локальном
профиле: нет ACT tick backend и защищённых firmware/SVN secrets. Заглушки secrets
из reference VendorInfo не копируются. SVN callbacks возвращают 0;
anti-rollback и привязка к образу прошивки отсутствуют.

Проба проверяет platform info, PCR profile, значения PCR0/PCR17, отклонение
неверных запросов без изменения буфера и power/locality/cancel/PP callbacks.
Это не выполнение TPM2_PCR_Extend/Read или TPM reset/Startup. После пробы остаётся
power-on state; NV не производится и ядро не инициализируется.

```text
[VTPM-PLAT] info=PASS pcr-profile=PASS pcr-values=PASS pcr-bounds=PASS signals=PASS
[VTPM-PLAT] PASS: platform callbacks; mask=0x1F
```

Следующий блок: граница RunCommand, NVEnable/Manufacture/_TPM_Init и первые
команды через ExecuteCommand. Публикация TPM для ОС остаётся отдельным этапом.

Проверено: полный OVMF DEBUG/GCC5 собран; отдельный QEMU q35/TCG без гостевых
дисков прошёл все пять platform checks и прежние core/NV/RNG/clock/transport
проверки, ASSERT нет. Лог `../Tests/platform-smoke.log`; воспроизведение из edk2:
`python3 MyTpm2EMU/Tests/run-platform-smoke.py`. Пользовательская проверка этой
версии пока ожидается.

## Шестой блок: инициализация и команды ядра

`Port/TpmRun.c` владеет единственным экземпляром ядра в TpmCoreProbeSmm.
VtpmCoreInitialize выполняет VtpmPlatformPowerOn → NVEnable →
TPM_Manufacture(MANUF_FIRST_TIME) → _TPM_Init один раз за жизнь SMM-модуля.
Повторный вызов возвращает сохранённый результат, не стирая состояние.
Поскольку NV сейчас временное, при новой загрузке образа manufacture выполняется
заново и создаёт новые случайные hierarchy seeds. Это нужно изменить при переносе
на постоянное NV. Provisioning и пользовательские ключевые объекты не проверены.

VtpmCoreSubmit проверяет входные параметры, требует полную ёмкость
MAX_RESPONSE_SIZE (3968 байт), блокирует повторный вход и копирует запрос в
собственную память. ExecuteCommand получает фиксированный частный буфер ответа.
Полученный размер проверяется до копирования результата. Короткая ёмкость
отклоняется **до исполнения**, чтобы не потерять ответ изменившей состояние команды.
Фасад принимает только доверенные SMM-буферы: он не заменяет проверки внешних
адресов в транспорте. Run status и TPM_RC внутри ответа — разные значения.

SetJump/LongJump перехватывает TpmFail. Первый отказ во время команды повторяет
вход в ExecuteCommand, который формирует failure-mode response; второй отказ
прекращает выполнение, исключая бесконечный цикл. Инициализация при TpmFail
возвращает failure и не останавливает диагностическую загрузку. Это не перехват
произвольных CPU faults. Вызов _plat__Fail вне зарегистрированной границы остановит
выполнение: все достижимые вызовы ядра должны выполняться через фасад.

Для профиля без ECC таблица CommandDispatchData имела четыре безусловные ссылки
на отключённые marshal-функции. Port/CommandDispatcher.c — неизменённая копия
upstream C-файла, выбирающая соседний Port/CommandDispatchData.h: в нём четыре
ECC entries условны и сохраняют свои индексы. Остальной dispatcher и Vendor
не меняются. Команды ECC отключены профилем; запрос ECC_Parameters проверяется
как неподдерживаемый до доступа к marshalling slots.

Command-проба проверяет бинарные запросы и содержимое ответов:

- Startup(CLEAR): TPM_RC_SUCCESS и десятибайтовый ответ.
- GetCapability(TPM_PT_MANUFACTURER): размер, capability/count/property и VTPM ID.
- SelfTest(fullTest=YES): TPM_RC_SUCCESS; теперь выполняются self-tests ядра.
- GetRandom(32): TPM_RC_SUCCESS, длина TPM2B и размер ответа.
- Неизвестная/ECC команда: TPM_RC_COMMAND_CODE; неверная длина: TPM_RC_COMMAND_SIZE.
- Повторный Startup: TPM_RC_INITIALIZE; затем GetRandom снова успешно.
- Короткий output capacity: wrapper ARGUMENT, буфер не меняется; первая Startup
  затем успешна. Повторный Initialize не сбрасывает состояние.

Случайные байты/seeds/ключи в лог не выводятся. Проба оставляет ядро запущенным;
разрушительные NV/platform smoke checks выполняются **до** инициализации.

```text
[VTPM-CMD] init=PASS startup=PASS capability=PASS selftest=PASS random=PASS errors=PASS
[VTPM-CMD] PASS: ExecuteCommand; mask=0x3F stage=0
```

Следующий шаг: предоставить частный SMM protocol от этого модуля и подключить
его к VtpmProbeSmm. Не линковать отдельную копию TpmCoreLib в транспортный модуль:
иначе получится другой экземпляр состояния/identity. Затем реальные команды
после EBS и штатный TPM ACPI/CRB, Tpm2DeviceLib/Tcg2Dxe.

Проверено: полный OVMF DEBUG/GCC5 с SMM_REQUIRE собран. Final запуск
`python3 MyTpm2EMU/Tests/run-commands-smoke.py` проверяет два CPU-варианта,
без гостевых дисков и с временными VARS. С RDRAND все шесть command checks
PASS (0x3F, stage=0); без RDRAND ожидаемый initialization failure (stage=3),
продолжение firmware и старые транспортные пробы PASS. ASSERT нет.
Логи: `../Tests/commands-smoke.log`, `../Tests/commands-no-rdrand.log`.
Failure-mode retry во время команды пока не проверен отдельной fault injection;
отрицательный CPU-тест проверяет защищённую инициализацию.

## Седьмой блок: единственный core для транспорта

`Include/VtpmCoreSmm.h` задаёт частный SMM protocol Submit. GUID публикуется
в DEC и регистрируется через SmmInstallProtocolInterface после command smoke PASS.
Transport SMM ищет его через SmmLocateProtocol и передаёт собственные проверенные
SMRAM buffers. В VtpmProbeSmm нет TpmCoreLib: состояние/DRBG/identity принадлежит
только TpmCoreProbeSmm. Если owner не готов, packet Result=4; PING остаётся доступен.

Ограниченный mailbox допускает ровно два запроса с гарантированным ответом <=64:
GetCapability(manufacturer, count=1) и GetRandom(32). Submit получает отдельный
4096-byte scratch buffer, не маленькое внешнее поле. Для общего command API нужны
новые full-sized buffers: нельзя выполнять stateful-команду, а затем терять её
ответ из-за маленького mailbox. Текущая whitelist исключает такие команды.

DXE тестирует обе команды через SMI. В ASL добавлены 512-bit ByteAcc fields (исправлено после Windows ACPI failure),
TCMD/VTCM и вызов из _INI; проверки response tag/size/RC, manufacturer или TPM2B
length выполняются в AML. Обе diagnostic wire-операции используют общий Mutex.
Обычный iasl rebuild ASL/AML/HEX выполнен. Запуск в ОС должен подтвердить
`TPM command=0x17A/0x17B rc=0 phase=after-EBS`; этот этап не публикует TPM для TBS.

Проверено: полный OVMF DEBUG/GCC5 с SMM_REQUIRE собран; ASL скомпилирован без
ошибок/предупреждений, AML checksum/единственный patch marker проверены.
`Tests/run-runtime-transport-smoke.py` прошёл на final образе:
с RDRAND protocol установлен, оба DXE → SMI → core запроса PASS;
без RDRAND protocol не опубликован, TPM-запрос отклоняется Result=4,
PING/bounds и прежние пробы продолжают работать. ASSERT отсутствует.
Логи: `Tests/runtime-transport-smoke.log`, `Tests/runtime-no-core.log` относительно
MyTpm2EMU. Запуск в ОС для новых after-EBS команд остаётся проверкой пользователя.

## Исправление ACPI failure в Windows (2026-10-06)

Лог пользователя `edk2_debug_2026-10-06_00-10-18.log`: boot core/transport
работают; после EBS проходит PING sequence=7, но первой TPM SMI нет.
Обнаружен дефект ASL: BufferAcc использован для SystemMemory region.
По ACPI 6.6, table 19.34, допустимы ByteAcc/WordAcc/DWordAcc/QWordAcc/AnyAcc;
BufferAcc не разрешён. Исправлено на ByteAcc. Длина FieldUnit остаётся 512 бит,
поэтому данные по-прежнему имеют тип Buffer (64 bytes).

ASL/AML/HEX пересозданы; _INI сохраняет проверки PING и TPM-команд.
Windows bugcheck parameters не получены: дефект подтверждён спецификацией,
но устранение Windows BSOD пока требует повторной загрузки пользователя.
ACPICA и iasl были недостаточны для обнаружения нарушения: добавлена
`Tests/check-acpi-mailbox.py`, проверяющая region/access rules и выполнение
_INI/request field в acpiexec. Модель acpiexec не эмулирует реальный SMI.
Источник: https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf

## Восьмой блок: full-sized diagnostic transport

Windows лог `00-14-20` подтвердил исправление ByteAcc и обе реальные TPM-команды
после EBS. Новый full transport не меняет рабочую AML-пробу.

Protocol revision 2 содержит CommandCapacity и ResponseCapacity; owner получает
их из своего compile-time профиля через getters (сейчас 3968/3968).
Выделяются четыре ACPI NVS страницы, весь диапазон проверяется SMM при регистрации.
Legacy packet остаётся по offsets 0..0xA8. Full packet располагается в +0x1000:
header 40 bytes, request 4096, response 4096, total 0x2028 bytes.
Request offset относительно full packet 0x28, response 0x1028.

Full handler принимает любые command codes, включая команды с TPM ошибками.
Перед исполнением проверяет signature/version, размеры и готовность core.
Capacity меньше core maximum отклоняется до исполнения; fixed SMRAM snapshot
исключает повторное чтение изменённых ОС параметров. Ответ заполняется при BUSY,
DONE публикуется отдельной последней записью. Atomic gate защищает private packet;
после завершения snapshot стирается volatile. Transport Result=0 означает
доставленный ответ, даже если его TPM_RC не SUCCESS. Result 3 — bounds/capacity,
Result 4 — недоступный/ошибочный core.

`VtpmFullProbeDxe.c` делает девять проверок через тот же SMI, включая ответ
GetCapability(ALGS) >64 bytes, частичный GetRandom, ошибки TPM и transport,
защитные байты после packet и успешный запрос после ошибок.
Текущее ядро возвращает 48 random bytes на запрос 64: TPM2_GetRandom ограничивает
выдачу размером TPMU_HA. Сам core/DRBG от этого не считается неисправным.

Full канал пока проверяется DXE-пробой; AML обращается только к старому каналу.
Это не PTP CRB и не интерфейс для штатного Windows TPM-драйвера. Следующий шаг —
CRB layout/registers/start/completion и ACPI публикация, используя этот backend.

Full boot-проба проверена на final сборке OVMF DEBUG/GCC5/SMM_REQUIRE в QEMU:
все девять PASS, большой response=151, random response=60, TPM/transport ошибки
проверены, guard bytes сохранены, ASSERT отсутствует. Прежние проверки проходят.
Без RDRAND прежний transport возвращает ожидаемый Result=4, full тесты не запускаются.
Логи `../Tests/full-transport-smoke.log` и `../Tests/full-no-core.log`;
воспроизведение из edk2: `python3 MyTpm2EMU/Tests/run-full-transport-smoke.py`.
Новая full-проба на пользовательском Windows стенде и full-вызов после EBS
пока не подтверждены. Старые ACPI TPM-запросы after-EBS уже подтверждены логом 00-14-20.

## Девятый блок: модель регистров CRB

VtpmCrb.h использует PTP_CRB_REGISTERS из MdePkg/TpmPtp.h; compile-time assertions
проверяют ControlRequest=0x40, Start=0x4c, CommandSize=0x58, ResponseAddr=0x68,
header/data boundary=0x80. Header располагается в отдельной странице NVS+0x4000.
Все пять страниц закрепляются при SMM registration. Command/response buffers
переиспользуют full allocations в NVS+0x1028/+0x2028; callers не должны запускать
оба diagnostic interfaces одновременно.

VtpmCrbSmm.c хранит grant/ready/fatal state в SMRAM. Locality0 RequestAccess даёт
grant, Relinquish снимает его и переводит в idle. CommandReady выходит из idle,
GoIdle возвращает idle. Start при grant+ready копирует command в закрытую память,
берёт commandSize из big-endian TPM header, вызывает тот же core protocol,
записывает response и только затем очищает Start. Idle Start не исполняется.
Control request/locality control self-clear при публикации canonical header.
Register CommandSize/ResponseSize — capacity, фактическая response length в TPM header.

Command/response addresses и capacities публикуются из trusted registration/core;
данные register snapshot не могут перенаправить SMM copy. Неверный commandSize
возвращает TPM_RC_COMMAND_SIZE; core ошибки доставки устанавливают fatal status.
Cancel, выставленный перед Start, возвращает TPM_RC_CANCELED без исполнения;
прерывание уже выполняющегося core в этой модели отсутствует. Advertised interface:
CRB, locality0 only, no interrupts, no idle bypass. Vendor register IDs не назначены.

Управление происходит только на explicit diagnostic SMI (E7/52), не на обычные
RAM/MMIO stores. Для Windows-facing устройства нужно отдельно обеспечить
совместимые locality/control handshakes и ACPI start method. TPM2/MSFT0101 пока
не публикуются. Модель не является доказательством стандартной PTP совместимости.

VtpmCrbProbeDxe.c проверяет initial state, grant, ready/start/large response,
игнорирование подменённых адресов/размеров, idle, pre-execution cancel и recovery,
invalid command size и recovery, relinquish. Final QEMU run: все восемь групп PASS,
старые проверки PASS, ASSERT нет; полный DEBUG/GCC5 OVMF собран.
`../Tests/crb-smoke.log`; negative CPU без RDRAND не публикует core, legacy запрос
ожидаемо отклоняется; CRB-проба при этом не запускается (`crb-no-core.log`).
Проверка из edk2: `python3 MyTpm2EMU/Tests/run-crb-smoke.py`.
