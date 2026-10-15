# MyTpm2EMU: план реализации для x86

Рабочая папка: `/home/anecdote/uefi/edk2/MyTpm2EMU`.
Reference для чтения: `/home/anecdote/Projects/ms-tpm-20-ref/TPMCmd`.
Цель: firmware TPM 2.0 на физическом x86 ПК. Первый стенд: QEMU q35 / OVMF X64.

## Статус и доказательства

Обозначения: [x] реализовано и проверено; [ ] ещё требуется.
Для каждой проверки указана её область: boot probe не доказывает работу TPM в ОС.

| Блок | Реализовано | Что проверено | Что остаётся |
| --- | --- | --- | --- |
| DXE → SMM | SmmCommunication, PING/PONG | Два запроса с проверкой sequence/calls | Подключение реальных TPM-команд |
| SMI после EBS | Q35 doorbell, диагностический SSDT | Вызов из ОС, ответ SMM после ExitBootServices | Штатный TPM ACPI/CRB и перенос на физический chipset |
| Общая память | ACPI NVS, адрес закреплён в SMM, копирование в SMRAM | PING, неверные длины, короткий ответ, неизвестный запрос | Полноценные TPM command/response buffers |
| Библиотека ядра | Microsoft-код, EDK CRT, Ossl/TpmBigNum, firmware profile | Smoke checks и полные self-tests через TPM2_SelfTest | Криптографические объекты/сессии/политики |
| RAM NV | 16 KiB внутри SMM, чтение/запись/стирание/перенос/commit | Пять boot checks; загрузка гостевой ОС | Постоянный NV, проверка через TPM2_NV_* |
| Энтропия | RDRAND backend, CPUID, bounded retries, sticky failure | QEMU: CPU с RDRAND и без него; подробности ниже | Интеграция с DRBG, тест отказа внутри TPM-команды |
| Часы | TSC/ACPI calibration, flags, rate adjustment | Пять boot checks PASS в QEMU | Runtime, точность, VM migration/suspend, физический ПК |
| PCR/platform callbacks | Добавлены metadata, начальные PCR, power/locality/cancel/PP | Пять boot checks PASS в QEMU | Реальные PCR-команды, lifecycle ядра, доставка cancel |
| Core lifecycle | TpmRun facade, Manufacture/_TPM_Init, ExecuteCommand | Шесть command checks PASS в QEMU | Runtime protocol, fault injection, постоянное NV |
| TPM в ОС | Диагностический ACPI-запрос реальных команд | GetCapability/GetRandom after-EBS PASS в Windows | ACPI TPM2/CRB, Windows TBS, provisioning |

Последнее подтверждение пользователя: `logs/edk2_debug_2026-10-05_23-30-24.log`.
В нём пять проверок ядра и пять NV-проверок PASS; транспортные проверки PASS;
`BUFFER sequence=5 result=0 response=4 phase=after-EBS copy=Success`.
ASSERT в этом логе отсутствует. Лог относится к версии **до добавления RDRAND**.
NV в этом запуске проверялось при загрузке SMM-модуля; after-EBS строка проверяет
диагностический транспорт, а не доступ к NV из команды TPM.

## Этап 1: передача управления

- [x] SMM-модули подключены в DSC/FDF под SMM_REQUIRE.
- [x] DXE/SmmCommunication round trip.
- [x] Q35 software SMI с фильтрацией порта/команды.
- [x] Уведомление ExitBootServices и диагностический вызов из ОС после него.
- [x] Штатный Windows TPM driver/TBS подтверждён пользователем, лог 01-32-17.

## Этап 2: транспорт и память

- [x] Старый stub исключён из DSC/FDF; неудачное выделение FED40000 больше не используется.
- [x] Общий буфер в ACPI NVS; адрес закрепляется до EBS.
- [x] Проверки заголовка, длин, sequence и capacity; запрос копируется в SMRAM.
- [ ] CRB по определениям EDK II: locality, command-ready, idle, start, cancel, completion.
- [ ] Настоящие TPM request/response buffers с подходящими размерами.
- [ ] TPM2 ACPI table, _CRS, Start Method/_DSM; отсутствие дублей штатных таблиц.
- [ ] Публиковать TPM только после успешной инициализации; откат при ошибках.

## Этап 3: ядро Microsoft и platform layer

- [x] TpmCoreLib: отдельная Vendor-копия и контрольные суммы; Simulator не включён.
- [x] Firmware profile и адаптация к OpenSSL текущего CryptoPkg.
- [x] Изолированный SMM smoke test: marshal, bounds, algorithms, SHA256, AES128.
- [x] RAM NV и его boot checks; подтверждено пользователем.
- [x] Энтропия: `Port/TpmEntropy.c`; сборка и QEMU с RDRAND/без RDRAND проверены.
      Источник не использует время/rand()/детерминированный fallback.
- [x] RNG-проба подтверждена пользователем: лог `23-37-51`, четыре PASS.
- [ ] Проверить RNG через инициализированный TPM DRBG, включая отказ источника.
- [x] Часы: монотонное время, reset/stopped flags и ClockRateAdjust; boot checks QEMU PASS.
- [x] PCR/platform metadata и manufacturer/vendor/firmware callbacks; boot checks QEMU PASS.
- [x] Platform power/locality/cancel callbacks и PP=FALSE; boot checks QEMU PASS.
- [ ] Полный lifecycle ядра, runtime cancel и physical presence interface.
- [x] Граница исполнения: SetJump/LongJump, ограниченный retry и сохранение failure response.
- [ ] Проверить retry/failure response fault injection во время команды.
- [x] Однократное Manufacture временного NV и _TPM_Init; повторный Initialize идемпотентен.
- [x] Startup, GetCapability, SelfTest, GetRandom через настоящий ExecuteCommand: boot checks PASS.
- [x] Неверные команды/длины, повторная Startup и следующая успешная команда: boot checks PASS.
- [x] Единственный экземпляр ядра подключён к SMM-транспорту через частный protocol; boot-путь проверен.

## Этап 4: состояние и интеграция

- [ ] Постоянное NV: сохранность, атомарный commit, защита seeds, ошибки/обрыв питания.
- [ ] Manufacture только нового/явно сброшенного постоянного хранилища.
- [ ] Tpm2DeviceLib для доступа firmware к тому же экземпляру ядра.
- [ ] Tcg2Dxe, PCR и журнал измерений; PEI для ранних измерений по требованиям проекта.
- [ ] CreatePrimary, Create/Load, Sign/Verify, Unseal, сессии и политики.
- [ ] TPM2_NV_* и сохранность ключей/объектов после перезапуска.
- [ ] Windows: Get-Tpm, tpm.msc, provisioning и прикладные сценарии.
- [ ] Вернуть необходимые алгоритмы, включая ECC: текущий CryptoPkg backend его отключает.

## Этап 5: физический ПК

- [ ] Выбрать конкретную платформу и способ интеграции собственного SMM-модуля.
- [ ] Согласовать SMI-порты/команду с chipset и существующими обработчиками.
- [ ] Проверить memory map, ACPI и постоянное хранилище на этой платформе.
- [ ] Проверить загрузку, ОС, перезапуски и восстановление после отказов.

## Ограничения текущего стенда

Сейчас это диагностический транспорт и изолированные пробы библиотеки,
а не работающий TPM для ОС. RAM NV теряется при выключении. Ядро инициализируется в SMM boot-пробе
и создаёт временные hierarchy seeds; постоянные ключи/provisioning не проверены. Исходные reference-файлы не меняются.
Начальный профиль отключает ECC и Camellia из-за конфигурации старого CryptoPkg.
Q35 SMI-механизм нельзя автоматически считать подходящим для физического ПК.

## Источники и проверки

- Основной reference: локальные `tpm/` и `Platform/src/` Microsoft TPMCmd.
- CRB/ACPI: `MdePkg/Include/IndustryStandard/TpmPtp.h`, `Tpm2Acpi.h`.
- SMI: `OvmfPkg/SmmControl2Dxe`, `OvmfPkg/OvmfPkgX64.dsc`.
- Пробы: `Tests/core-smoke.log`, `Tests/nv-smoke.log`.
- Подробности порта и сборки: `TpmCoreLib/README.md`.

## Проверки блока энтропии

Полный OVMF DEBUG/GCC5 с SMM_REQUIRE собран. Тест
`Tests/run-entropy-smoke.py` запускает две конфигурации QEMU без гостевых дисков,
со свежими временными VARS:

- `-cpu max`: init/short/partial/repeat PASS, итоговая маска 0xF.
- `-cpu max,-rdrand`: ожидаемый RNG FAIL (маска 0), загрузка продолжается,
  прежние проверки ядра, NV и транспорта проходят; ASSERT отсутствует.

Логи: `Tests/entropy-smoke.log`, `Tests/entropy-no-rdrand.log`.
Проверка отсутствия инструкции не является тестом отказа работающего RDRAND
или доказательством качества энтропии. Runtime RNG после EBS пока не вызван.
Часы перенесены следующим блоком; ближайший шаг — остальные _plat__ функции
и реальная инициализация.

## Текущий блок часов

Лог `logs/edk2_debug_2026-10-05_23-37-51.log` подтверждает RNG/core/NV PASS
на пользовательском стенде и успешный диагностический транспорт после EBS.
Реализация часов добавлена в `Port/TpmClock.c`.

- [x] Boot checks часов в QEMU: flags/advance/monotonic/restart/rate PASS.
- [x] Clock-проба подтверждена пользователем: лог `23-42-55`, пять PASS.
- [ ] Проверить часы при runtime-командах после EBS.
- [ ] Проверить длительные интервалы, точность, migration/suspend и TSC между CPU.
- [ ] Для физического ПК определить требования к источнику и разрывам времени.

После этого: PCR/platform metadata, питание/locality/cancel, RunCommand,
полная инициализация и TPM-команды. Остальные незакрытые пункты остаются в плане.

Полная сборка OVMF DEBUG/GCC5 с SMM_REQUIRE успешна. Тест
`python3 MyTpm2EMU/Tests/run-clock-smoke.py` проверил новую прошивку без гостевых
дисков: часы PASS (маска 0x1F), прежние core/NV/RNG/transport пробы PASS,
ASSERT отсутствует. Лог: `Tests/clock-smoke.log`.

## Текущий platform-блок

Последний проверенный пользовательский лог: `logs/edk2_debug_2026-10-05_23-42-55.log`.
Часы PASS; диагностический обмен после EBS успешен; ASSERT отсутствует.

- [x] Platform-проба QEMU: все пять проверок PASS.
- [x] Platform-проба подтверждена пользователем: лог `23-47-54`, пять PASS.
- Реализованы PCR callbacks (таблица reference, значения и проверки буфера),
  manufacturer/vendor/firmware callbacks, platform manufacture data,
  WasPowerLost, LocalityGet/Set, IsCanceled/SetCancel/ClearCancel, PP=FALSE.
- ACT/FW-limited/SVN-limited временно отключены: backend пока отсутствует;
  reference dummy firmware secrets не используются.
- [ ] ACT timers и защищённые firmware/SVN secrets, если потребуются в итоговом профиле.
- [ ] Согласовать итоговые PCR attributes с целевым PC Client profile.
- [ ] CRB cancel и проверка отмены реально выполняющейся команды.
- [ ] Реальная physical presence policy/interface.

Ближайший шаг — RunCommand boundary, NVEnable/Manufacture/_TPM_Init и
Startup/GetCapability/SelfTest/GetRandom. Затем подключение единственного
экземпляра ядра к runtime-транспорту. Permanent NV остаётся незавершённым.

Проверено: полный OVMF DEBUG/GCC5 с SMM_REQUIRE собран. Запуск
`python3 MyTpm2EMU/Tests/run-platform-smoke.py` без гостевых дисков и с временными
VARS: platform mask=0x1F, прежние core/NV/RNG/clock/transport пробы PASS,
ASSERT отсутствует. Лог: `Tests/platform-smoke.log`.

## Текущий command-блок

Реализованы VtpmCoreInitialize/VtpmCoreSubmit в `Port/TpmRun.c`, защищённая граница
TpmFail, одноразовое manufacture временного NV и реальная обработка wire commands.
Platform-проба подтверждена логом `logs/edk2_debug_2026-10-05_23-47-54.log`.

- [x] Final command-версия: QEMU с энтропией PASS; без неё — ожидаемый initialization failure и продолжение загрузки.
- [x] Command-проба подтверждена пользователем: лог `23-59-06`, шесть PASS.
- [ ] Проверить injection TpmFail **во время команды**, включая failure response retry.
- [x] Частный SMM protocol опубликован единственным owner; transport вызывает Submit.
- [x] GetCapability/GetRandom after-EBS подтверждены Windows логом `00-14-20`.
- [ ] Реальные команды после EBS; затем TPM2 ACPI/CRB и штатный драйвер ОС.

Диагностический runtime-путь теперь поддерживает две ограниченные реальные TPM-команды
через тот же экземпляр ядра; общий TPM transport для произвольных команд ещё впереди.

Final проверка выполнена `Tests/run-commands-smoke.py` после полной сборки
OVMF DEBUG/GCC5 с SMM_REQUIRE:

- `Tests/commands-smoke.log`: init/startup/capability/selftest/random/errors PASS,
  mask=0x3F, stage=0. Проверены также короткий output capacity, отключённая ECC
  команда и повторный Initialize без сброса состояния.
- `Tests/commands-no-rdrand.log`: ожидаемый initialization failure, mask=0,
  stage=3; прежние core/NV/clock/platform/transport пробы проходят,
  firmware продолжает загрузку. RNG FAIL здесь ожидается.
- Оба запуска используют свежие временные VARS и не подключают гостевые диски.
  ASSERT отсутствует. Reference и Vendor не изменены (487 SHA256 проверок).

## Runtime transport bridge (2026-10-06)

- `TpmCoreProbeSmm` после шести PASS публикует частный SMM protocol Submit.
- `VtpmProbeSmm` не линкует TpmCoreLib; находит protocol в SMM и вызывает owner.
- Существующий 64-byte mailbox принимает точные GetCapability(manufacturer,1)
  и GetRandom(32); прочие TPM-запросы пока отклоняются. Scratch response 4096 байт
  находится в SMRAM. Нет произвольных внешних указателей в Submit.
- DXE проверяет оба ответа через doorbell и общую память, не напрямую в library.
- SSDT _INI вызывает PING и обе TPM-команды; private _DSM function 2 повторяет
  TPM-пробу. Общий AML mutex сериализует доступ diagnostic methods к mailbox.
- [x] `TPM command=0x17A/0x17B rc=0 phase=after-EBS` подтверждены логом `00-14-20`.
- [ ] Полные command/response buffers, command-ready/idle/start/cancel/completion.
- [ ] TPM2 table и устройство MSFT0101, совместимый с Windows запуск команды.

## Что считается готовностью для Windows API на QEMU

Первый milestone: штатный драйвер распознал TPM2; Tbsi_GetDeviceInfo сообщает
TPM 2.0; Tbsi_Context_Create/Tbsip_Submit_Command выполняют GetCapability и GetRandom
через наш core. API reference: https://learn.microsoft.com/en-us/windows/win32/api/tbs/

Дальше: CreatePrimary/сессии/context save/load, provisioning/Get-Tpm TpmReady,
Tpm2DeviceLib/Tcg2Dxe и журнал измерений, physical presence interface по нужным
сценариям; постоянный NV и восстановление keys/seeds после выключения QEMU.
ECC backend/profile и PC Client capabilities необходимо довести до целевого набора.

Оценка: примерно 4–6 интеграционных итераций до первых успешных TBS-вызовов,
затем дополнительные итерации для NV/provisioning/keys/совместимости. Это оценка
объёма, не срок по дням и не гарантия; основной неизвестный участок — реальная
совместимость ACPI/CRB start method с Windows. Наличие всех команд в library
само по себе не доказывает Windows readiness.

Проверено: полный OVMF DEBUG/GCC5 с SMM_REQUIRE собран; ASL скомпилирован без
ошибок/предупреждений, AML checksum/единственный patch marker проверены.
`Tests/run-runtime-transport-smoke.py` прошёл на final образе:
с RDRAND protocol установлен, оба DXE → SMI → core запроса PASS;
без RDRAND protocol не опубликован, TPM-запрос отклоняется Result=4,
PING/bounds и прежние пробы продолжают работать. ASSERT отсутствует.
Логи: `Tests/runtime-transport-smoke.log`, `Tests/runtime-no-core.log` относительно
MyTpm2EMU. Запуск в ОС для новых after-EBS команд остаётся проверкой пользователя.

## Регрессия Windows ACPI, лог 00-10-18

- [x] Прочитан лог: реальный core и DXE transport PASS; PING after-EBS PASS.
  TPM command after-EBS отсутствует; Windows не загружается по сообщению пользователя.
- [x] Найдено нарушение ACPI table 19.34: BufferAcc на SystemMemory.
- [x] Исправлено на ByteAcc; сохранены 512-bit поля и TCMD/_INI.
- [x] Пересозданы AML/HEX и полная сборка OVMF DEBUG/GCC5.
- [x] Static region/access check и acpiexec _INI/request execution PASS.
- [x] Windows BSOD устранён: пользователь подтвердил загрузку, лог `00-14-20`.
- [x] Обе диагностические TPM-команды after-EBS PASS; этот этап закрыт.

`Tests/check-acpi-mailbox.py` дополнительно отлавливает ошибку, пропущенную
компилятором и permissive ACPICA. Bugcheck parameters неизвестны: invalid access
исправлен, причинная связь и отсутствие других Windows ошибок требуют retest.

## Подтверждение Windows и full transport (2026-10-06)

`logs/edk2_debug_2026-10-06_00-14-20.log`:
GetCapability 0x17A, rc=0, response=27 after-EBS;
GetRandom 0x17B, rc=0, response=44 after-EBS. ASSERT/FAIL отсутствуют.
ByteAcc исправление подтверждено успешной загрузкой Windows.

Новый этап — полный диагностический канал, ещё не CRB:

- [x] Выделение четырёх contiguous страниц ACPI NVS; весь диапазон проверяется
  и фиксируется при регистрации SMM до EBS.
- [x] Старый 64-byte ACPI channel остаётся по старым offsets; AML не менялся.
- [x] Full packet в NVS+0x1000: header 40, request 4096, response 4096 bytes.
- [x] SMM получает полную частную snapshot; READY → BUSY → DONE, результат
  записывается перед публикацией DONE; private packet обнуляется volatile.
- [x] Поддержка произвольных команд через единственный core protocol без whitelist;
  реальные command/response maxima передаются owner (сейчас 3968 bytes).
- [x] Короткая ёмкость ответа и завышенная длина отклоняются до ExecuteCommand.
- [x] Final full transport в QEMU: все девять проверок PASS.
- [x] Full boot-проба и Windows загрузка подтверждены: лог `00-25-17`, девять PASS.
- [ ] Проверить full transport после EBS (старый канал уже подтверждён).
- [ ] CRB layout/registers, locality, command-ready/idle/start/cancel/completion.
- [ ] TPM2 table/MSFT0101/start method и штатный Windows TBS.

DXE full-проба проверяет список алгоритмов >64 bytes, GetRandom(64) с допустимым
частичным ответом, TPM ошибки unknown command/incorrect size/truncated header,
transport bounds/capacity и неизменный guard за packet; нормальный запрос после
ошибок. Profile GetRandom сейчас ограничен sizeof(TPMU_HA)=48 bytes; это штатный
частичный ответ reference, не отказ генератора.

Full transport final verification: полный OVMF DEBUG/GCC5/SMM_REQUIRE собран.
`Tests/run-full-transport-smoke.py` (fresh temporary VARS, no guest disks):
все девять full checks PASS; GetCapability ALGS response=151,
GetRandom(64) response=60 (48 bytes payload), TPM_RC 0x143/0x142/0x9A проверены;
bounds tests 5/6/7 ожидаемо дают transport Result=3, защитные байты не изменены.
Прежние пробы core/NV/RNG/clock/platform/legacy transport PASS, ASSERT нет.
Negative CPU без RDRAND: core недоступен, legacy TPM-запрос Result=4 ожидаем,
firmware и PING продолжают работать; full-команды в этом варианте не запускаются.
Логи: `Tests/full-transport-smoke.log`, `Tests/full-no-core.log`.
AML region/access check и acpiexec также PASS; reference/Vendor не изменены.

## Диагностическая модель CRB (2026-10-06)

Лог `logs/edk2_debug_2026-10-06_00-25-17.log` подтверждает все девять full-проверок
и старые GetCapability/GetRandom after-EBS; ASSERT/FAIL нет.

- [x] Страница CRB в NVS+0x4000; теперь общий диапазон — 5 contiguous страниц.
- [x] Структура/offset assertions по EDK PTP_CRB_REGISTERS, header 0x80 bytes.
- [x] Locality 0 request/grant/relinquish, idle/command-ready/start/completion.
- [x] Cmd/Rsp addresses и maxima задаются SMM; внешние изменения игнорируются,
  адреса из RAM-регистров никогда не используются для копирования.
- [x] Некорректный commandSize из TPM header возвращает TPM_RC_COMMAND_SIZE.
- [x] Cancel до исполнения возвращает TPM_RC_CANCELED, после сброса флага
  следующая команда работает. Это не отмена уже выполняющейся команды.
- [x] Полная сборка OVMF DEBUG/GCC5; QEMU CRB-проба: восемь групп PASS;
  прежние smoke/full/legacy checks PASS, ASSERT нет.
- [x] Подтверждено на пользовательском Windows стенде: лог `00-36-28`,
  восемь CRB PASS и прежние GetCapability/GetRandom after-EBS. ASSERT/FAIL нет.
- [ ] Проверить CRB модель после EBS.
- [ ] Реализовать отмену выполняющейся команды/timeout policy для итогового интерфейса.

Ограничение: RAM-регистры обрабатываются только после диагностического SMI
(APM command E7, data 52). Стандартные MMIO записи не запускают SMM автоматически.
Это ещё не устройство для tpm.sys: нельзя просто публиковать TPM2 table и ожидать,
что Windows опрос locality/command-ready заработает. Ближайший этап — выбрать и
проверить совместимый ACPI start method и обработку control/locality для Windows,
затем TPM2 table/MSFT0101 и TBS. Interrupts и localities 1–4 не реализованы.

Модель находится в VtpmCrbSmm.c, проверки — VtpmCrbProbeDxe.c.
Она использует те же fixed command/response allocations, что full mailbox;
оба интерфейса вызывают единственный core. Диагностические callers обязаны
не отправлять запросы по обоим интерфейсам одновременно, поскольку buffers общие.
Рабочий ASL не менялся. Проверка: `Tests/run-crb-smoke.py`;
логи `Tests/crb-smoke.log`, `Tests/crb-no-core.log`.


## Подключение Windows: Control Area, этап 1 (2026-10-06)

Выбран ACPI StartMethod=2 вместо публикации диагностического CRB как MMIO.
Основание: Microsoft Trusted Execution Environment ACPI Profile, sections
4.3.4.3 / 4.4: стандартный _DSM UUID
6bbf6cab-5463-4714-b7cd-f0203c0368d4, revision 0, function 1.
Start обязан быстро возвращаться, выполнение команды должно быть отложено.
В текущем OVMF SmmControl2Dxe отложенные/periodic SMIs не поддержаны.

- [x] Независимые Control Area + command/response pages: shared +0x5000,
  +0x6000, +0x7000. Общий диапазон увеличен до восьми страниц.
- [x] Приём команды копирует данные в SMRAM без ExecuteCommand.
- [x] Повторный приём pending-команды отклоняется без замены snapshot.
- [x] Worker выполняет snapshot и публикует ответ перед Start=0.
- [x] Firmware-owned pointers/capacities проверяются; указатели ОС не используются.
- [x] Пробы: изменение внешнего запроса после приёма; повторный приём;
  cancel до приёма; commandSize за пределами; неверный pointer и восстановление.
- [x] Полная OVMF DEBUG/GCC5/SMM_REQUIRE сборка и QEMU smoke PASS.
  Старые core/full/CRB проверки PASS, ASSERT нет; ACPI checks PASS.
  CPU без RDRAND: ожидаемый отказ core, firmware продолжает работать.
  Логи: Tests/control-smoke.log и Tests/control-no-core.log.
- [x] Подтверждено пользователем и логом `00-45-18`: Control Area boot checks PASS.
- [x] Автоматический QEMU worker реализован и проверен в следующем этапе.
  Физический chipset требует отдельного backend.
- [x] _DSM Start + TPM2 table / MSFT0101 реализованы в следующем этапе.
- [ ] TBS: Tbsi_GetDeviceInfo / context / GetCapability / GetRandom.

Файлы: VtpmControl.h, VtpmControlSmm.c, VtpmControlProbeDxe.c.
Два диагностических SMI: data 53 — accept, 54 — execute (command E7).
ACK A5 означает принятие, A6 — отказ. Это staging backend, ещё не ACPI
Start implementation: второй SMI пока вызывает DXE-проба. Устройство MSFT0101
не публикуется; отмена уже выполняющейся команды пока не реализована.
NV остаётся volatile, ECC/PPI/provisioning и persistent keys — отдельные этапы.
Источник: https://learn.microsoft.com/en-us/windows-hardware/test/hlk/testref/trusted-execution-environment-acpi-profile


## Подключение Windows: ACPI transport (2026-10-06)

- [x] Пользователь подтвердил Control Area; лог `00-45-18`, ASSERT/FAIL нет.
- [x] QEMU q35 software-SMI timer: accept в SMM, затем timer worker через 16 ms.
  Таймер включается только для pending-команды и выключается перед исполнением.
- [x] Проба автоматического completion без ручного Execute SMI перед публикацией.
- [x] TPM2 revision 3, StartMethod=2; SSDT /_SB/VTPM, HID MSFT0101,
  стандартный Start _DSM UUID / revision 0 / functions 0–1.
- [x] _CRS: выделенные control/command/response pages и APM ports B2/B3.
  Весь shared allocation теперь EfiReservedMemoryType, ниже 4 GiB;
  это заменяет прежние восемь страниц ACPI NVS.
- [x] Проверка существующей TPM2 table: второй TPM не публикуется.
  При ошибке установки TPM2 установленная новая SSDT удаляется.
- [x] Full OVMF DEBUG/GCC5 build; QEMU TCG positive / no-RDRAND / timer-off;
  AML HID, _CRS, _DSM query / revision / Start-failure checks через ACPICA.
- [x] Подготовлена гостевая Tests/Test-VtpmTbs.ps1: device info, context,
  GetCapability manufacturer и GetRandom через tbs.dll. На хосте Windows нет,
  сама TBS-проба пока не выполнялась.
- [x] Windows загружается, Device Manager: устройство работает нормально (01-32-17).
- [x] Windows TBS device info/context/GetCapability/GetRandom PASS; команды after-EBS.
- [ ] Provisioning/PPI/MOR, persistent NV, полноценные ключи и PCR measurements.
- [ ] Cancellation/timeouts долгих команд, suspend/resume и физический chipset.

QEMU-specific детали: LPC PCI 00:1f.0, ID 2918:8086; GEN_PMCON_3[7:6]=1;
SMI_EN/SMI_STS bit 6. В QEMU SMI_STS — masked writable register, не physical
ICH9 W1C. Не переносить VtpmTimerSmm.c на физический ПК без нового backend.
OVMF SmmControl2 не менялся, negotiation broadcast не менялся. Новый QEMU
с x-smi-swsmi-timer=on требуется; на хосте версия 11.1.1, default on.
Тесты используют fresh VARS без гостевых дисков. Логи:
Tests/windows-transport-smoke.log, windows-no-core.log, windows-no-timer.log,
windows-acpi.log. При отключённом timer core жив, но TPM2/MSFT0101 не публикуется.
NV/seed/key state по-прежнему теряется при перезапуске firmware.

Источники: Microsoft Trusted Execution Environment ACPI Profile sections
4.3.4.3 / 4.4; QEMU hw/acpi/ich9_timer.c, hw/acpi/ich9.c,
include/hw/southbridge/ich9.h. Windows API: Microsoft TBS documentation.

Дополнительно: QEMU KVM / CPU host — automatic completion и publication PASS;
CPU host,-rdrand — ожидаемое отсутствие core. Логи *-kvm.log.

APM B2/B3 writes нового Start _DSM используют тот же ACPI mutex, что VPRB,
чтобы два устройства не перемешивали command/data записи. Обе SSDT вместе
проверены ACPICA. Timer и APM источники обслуживаются в одном SMI entry.


## Windows Code 12: исправление заявки ресурсов (2026-10-06)

Пользователь: TPM2/MSFT0101 published; Device Manager Code 12;
Tbsi_GetDeviceInfo возвращает 0x80284001 (TBS_E_INTERNAL_ERROR).
Лог 01-00-57: firmware timer/publication PASS, старый диагностический канал
работает after-EBS; нового Start accepted after-EBS нет. Успешная публикация
ACPI ещё не подтверждает запуск tpm.sys.

- [x] Удалена повторная заявка B2/B3 из TPM _CRS: это platform SMI ports,
  опубликованные FADT; QEMU также может объявлять их в PCI0.SMI0.
  AML OperationRegion для firmware SMI сохранён. _CRS — только 3 страницы memory.
- [x] Обновлена ACPICA проверка: только Memory32Fixed + EndTag, нет IO descriptor;
  test-only ASL передаёт все четыре _DSM arguments без прежних ACPICA warnings.
- [x] TBS script создаёт info/context structures внутри C#, без New-Object;
  по уточнению пользователя прежняя ошибка была кодом TBS, не PS syntax/type.
- [x] OVMF DEBUG/GCC5 сборка, ACPICA и QEMU KVM timer/publication smoke PASS.
- [x] Code 12 устранён; Device Manager normal и TBS PASS подтверждены пользователем.

Конфликт ресурсов — обоснованная причина для этой правки, но точный конфликтующий
Windows allocation не снят; исчезновение Code 12 пока не подтверждено.


## Windows зависания: смешанная SMI delivery (2026-10-06, прежний workaround)

**broadcast=off отменён после Windows ASSERT; текущая правка описана ниже.**

Логи 01-09-03 и 01-10-14: Windows наконец вызывает стандартный Start _DSM
после EBS и получает ответы. Во втором логе 85 completed commands after-EBS;
есть CreatePrimary/Create и управление сессиями, а не только diagnostics.
Пользователь сообщает минутные задержки загрузки и появления desktop.
TBS user probe при этом ещё не проверена.

Причина, воспроизведённая firmware-тестом: QEMU software-SMI timer вызывает
ich9_generate_smi() на first_cpu без negotiated broadcast; APM SMI поддерживает
broadcast. OVMF при negotiation broadcast устанавливает traditional rendezvous
и AP sync timeout 1000000 us. Поэтому timer SMI ждёт APs, не получившие SMI,
перед тем как firmware сможет обработать любую TPM команду.

- [x] Измерение GetRandom через deferred path перед публикацией: AP sync
  penalty входит в полное время, в отличие от измерения только внутри handler.
- [x] KVM / CPU host / 2 vCPU: broadcast=on — 2560464 us;
  broadcast=off — 17023 us. Fresh VARS, no guest disks.
- [x] В /home/anecdote/uefi/run-qemu-win11.sh добавлен
  -global ICH9-LPC.x-smi-broadcast=off. OVMF использует штатную unicast policy
  (relaxed sync); CPU/SmmControl2 implementation не менялся.
- [x] Guard: deferred GetRandom round-trip >250000 us блокирует публикацию
  TPM2/MSFT0101 с сообщением deferred SMI too slow. Это bring-up probe limit,
  не ограничение длительности RSA-команд и не runtime command timeout.
- [x] Build OVMF DEBUG/GCC5 PASS; run-smi-latency.py воспроизводит медленный
  режим и успешную публикацию только быстрого режима. Логи smi-latency-*.log.
- [x] Повторный KVM smoke с новым режимом: core/full/CRB/control PASS,
  deferred round-trip 17005 us, publication PASS; timer-off negative PASS.
  Windows AML checks и bash -n launcher PASS.
- [ ] Windows boot/desktop latency повторно проверить с новым launcher.
- [ ] TBS-проба и стоимость RSA provisioning команд в Windows.

Важно: для ручного запуска или другого launcher тоже нужен x-smi-broadcast=off.
Одной пересборки без исправления запуска недостаточно. Нет Windows guest в
host smoke: проверка ускорения Windows остаётся за пользовательским запуском.
Источник QEMU: hw/acpi/ich9_timer.c + hw/isa/lpc_ich9.c;
локально OvmfPkg/SmmControl2Dxe/SmiFeatures.c и PiSmmCpuDxeSmm/MpService.c.


## Runtime-variable ASSERT: all-CPU rendezvous (2026-10-06)

Лог 01-19-17: VariableSmmRuntimeDxe.c(183), ASSERT_EFI_ERROR Invalid Parameter.
TPM-команды продолжают завершаться на другом vCPU. PiSmmIpl Communicate при
InSmm=TRUE и EfiGoneVirtual() возвращает EFI_INVALID_PARAMETER. Relaxed sync
позволяет нормальному CPU увидеть флаг InSmm, установленный другим CPU.
Предыдущая рекомендация broadcast=off была ошибочной для asynchronous SMI
с runtime services в этой OVMF; отменена, ASSERT не подавляется.

- [x] Новый VtpmSmmCpuFeaturesLib: original OVMF copy, изменены initialization
  и RendezvousEntry. Первый CPU при q35 SWSMI посылает IPIs до AP wait.
- [x] Сохранён traditional all-CPU rendezvous и исходный CPU timeout;
  original OVMF CPU library / PiSmm CPU core sources не менялись.
- [x] DSC PiSmmCpuDxeSmm выбирает новую library; launcher broadcast=on.
- [x] Relaxed sync блокирует timer activation и TPM publication.
- [x] Снимок CPU policy при SMM driver entry до ReadyToLock: runtime не
  обращается к DXE PCD database (ранний вариант был отвергнут smoke-тестом
  из-за forbidden SMM memory access; исправлен до передачи пользователю).
- [x] Boot-проба подтверждает присутствие APs через SmmStartupThisAp.
- [x] Build OVMF DEBUG/GCC5; KVM 2 CPUs: все прежние probes PASS,
  all-CPU check PASS, timer round-trip 17430 us; ASSERT/forbidden access нет.
- [x] run-smi-latency.py: traditional 17472 us, relaxed configuration rejected.
- [x] Дополнительно KVM 4 CPUs: all-CPU check PASS, 17523 us;
  TCG 2 CPUs: all-CPU check PASS, 17189 us. Core/full/CRB/control и
  no-RDRAND negative проверки PASS; ASSERT/forbidden memory access нет.
  AML и bash syntax checks PASS, reference/Vendor 487 files unchanged.
- [x] Windows desktop подтверждён, ASSERT отсутствует в 01-32-17;
  пользователь отмечает небольшой остаточный статтер, без прежнего зависания.
- [x] TBS PASS; прежний runtime-variable ASSERT отсутствует в Windows логе.
- [ ] Отдельная проверка чтения/записи runtime variables под нагрузкой.

В новой library сохраняется лицензия исходной OVMF, save-state/SEV логика
осталась прежней. Platform hook рассчитан на QEMU q35 / first CPU index 0;
physical chipset и CPU hotplug не являются проверенным сценарием vTPM.
Путь scripts / Tests/run-windows-transport-smoke.py теперь поддерживает
VTPM_SMOKE_CPUS для проверки нескольких vCPU.


## Windows TBS milestone — подтверждён (2026-10-06)

Пользователь подтвердил загрузку Windows до рабочего стола, Device Manager:
«устройство работает нормально», Test-VtpmTbs.ps1: structVersion=1,
tpmVersion=2, tpmInterfaceType=3, tpmImpRevision=0; GetCapability PASS,
GetRandom PASS, Windows TBS PASS. Это подтверждение пользователя, вывод ps1
отдельным файлом не сохранён.

Лог edk2_debug_2026-10-06_01-32-17.log: traditional sync, все 6 vCPU собраны
в SMM, round-trip=17652 us; TPM2/MSFT0101 published; GetCapability/GetRandom
и множество других команд завершаются after-EBS. ASSERT/FAIL/forbidden access нет.
Последние TBS-команды: GetCapability response=27 и GetRandom response=44, rc=0.
Устройство стало доступно через Windows tpm.sys/TBS — этап подключения закрыт.

Дальше:
- [ ] Persistent NV и hierarchy seeds/keys между перезапусками: выбрать firmware
  storage backend, предусмотреть atomic commit/recovery и wipe semantics.
- [ ] Проверить NV Define/Write/Read/Undefine и object lifecycle/signing через TBS.
- [ ] Проверить длительность RSA-команд, cancel/timeout, остаточный SMM статтер.
- [ ] PPI/MOR, measured boot/TCG2 и Windows provisioning readiness.
- [ ] Physical chipset backend, ECC, suspend/resume и остальные профильные требования.

Полнота TPM 2.0/Windows provisioning/BitLocker из этой проверки не следуют.
NV остаётся RAM-only, ключи и seeds не переживают перезапуск firmware.
