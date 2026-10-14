> Текущее состояние: Microsoft TPM core работает в SMM. Добавлены автоматический
> запуск через QEMU q35 software-SMI timer и TPM2/MSFT0101 с ACPI StartMethod=2.
> Windows tpm.sys/TBS подтверждён: устройство работает нормально,
> GetCapability и GetRandom проходят через Windows API (лог 01-32-17).
> NV и ключи пока не сохраняются между перезапусками. Последний этап описан в конце.


# История: отдельная загрузочная проверка ядра TPM

Добавлен TpmCoreProbeSmm.inf и TpmCoreLib: исходники Microsoft собраны как
статическая библиотека с адаптацией к EDK II. При загрузке отдельный SMM-модуль
проверяет marshal/unmarshal, профиль алгоритмов и backend SHA256/AES.
Полная инициализация TPM и ExecuteCommand пока не вызываются.

Ожидаемые новые строки:

```text
[VTPM-CORE] Isolated core smoke test starting
[VTPM-CORE] marshal=PASS bounds=PASS profile=PASS sha256=PASS aes128=PASS
[VTPM-CORE] PASS: isolated core smoke test; boot continues
```

Подробности, ограничения профиля и структура: [TpmCoreLib/README.md](TpmCoreLib/README.md).
Предыдущий транспорт остаётся подключённым; его проверки описаны ниже.

## Буфер запроса/ответа ОС → ACPI → SMI → SMM

В OvmfPkgX64.dsc / OvmfPkgX64.fdf вместо старого tpmdriver.inf подключены
VtpmProbeSmm.inf и VtpmProbeDxe.inf под условием SMM_REQUIRE == TRUE.
Старые исходники сохранены. Проба не публикует ACPI TPM и не реализует TPM-команды.
Добавлена отдельная диагностическая SSDT с устройством VPRB (HID VTP0001).
DXE выделяет одну страницу ACPI NVS и передаёт её адрес SMM один раз.
Адрес подставляется в SSDT OperationRegion с пересчётом контрольной суммы.
При ACPI-инициализации устройства ОС его _INI вызывает VTST: помещает PING
в общий буфер, пишет запрос в порты QEMU q35 B3/B2 и проверяет PONG в буфере.
SMM копирует фиксированный пакет в SMRAM, проверяет поля и возвращает ответ.
Из общей памяти не принимаются произвольные указатели на запрос или ответ.
Частный _DSM (revision 1, function 1) также вызывает VTST; это не TCG _DSM.

## Сборка и запуск

В существующем контейнере сборки, из /workspace после настройки EDK II:

```sh
build -p OvmfPkg/OvmfPkgX64.dsc -a X64 -b DEBUG -t GCC5 -D SMM_REQUIRE=TRUE
```

Сохраните дополнительные флаги своей рабочей сборки, если используете их.
Запускайте новую OVMF в QEMU q35 с SMM, как на предыдущем успешном запуске.
Нужно дойти до попытки загрузки ОС: проба выполняется на ReadyToBoot.
Используйте DEBUG-сборку с включённым DEBUG_INFO и выводом debugcon.

## Ожидаемый лог

```text
[VTPM-SMM] Handler registration: Success
[VTPM-DXE] ReadyToBoot probe registration: Success
[VTPM-SMM] PING sequence=1 calls=1
[VTPM-DXE] PASS: sequence=1 calls=1
[VTPM-SMM] PING sequence=2 calls=2
[VTPM-DXE] PASS: sequence=2 calls=2
[VTPM-DXE] PASS: DXE-SMM round trip verified (before ExitBootServices)
[VTPM-SMM] Shared buffer registered: 0x...
[VTPM-DXE] PASS: buffer test=1 result=0
[VTPM-DXE] PASS: buffer test=2 result=3
[VTPM-DXE] PASS: buffer test=3 result=3
[VTPM-DXE] PASS: buffer test=4 result=2
[VTPM-DXE] PASS: shared buffer and bounds checks verified
[VTPM-SMM] ExitBootServices observed
[VTPM-SMM] BUFFER sequence=5 result=0 response=4 phase=after-EBS copy=Success
[VTPM-SMM] DOORBELL calls=5 phase=after-EBS
```

Порядок двух строк регистрации может отличаться. Проверка сравнивает ответ,
номер запроса и счётчик SMM; одного успешного возврата Communicate недостаточно.
В новой прошивке не должно быть старой строки Active Stub Driver и ошибки
выделения FED40000–FED47FFF от этого драйвера.

Первая пара PING подтверждает путь через SmmCommunication до ExitBootServices.
Четыре теста буфера: корректный PING; завышенная длина; недостаточная ёмкость
ответа; неизвестное содержимое запроса. Result 3 и 2 в этих тестах ожидаются;
это диагностические коды транспорта, не TPM_RC.
ExitBootServices observed ставится по уведомлению ядра SMM, а не по таймеру.
BUFFER с result=0, response=4, copy=Success и phase=after-EBS подтверждает,
что SMM обработал запрос из общей памяти после выхода из Boot Services.
Точное число вызовов может отличаться, если ОС повторно инициализирует устройство.
Если after-EBS нет, присылайте лог: отсутствие вызова _INI не доказывает поломку SMM.
Неизвестное устройство VTP0001 без драйвера в Windows допустимо на этом этапе.
TPM ещё не должен появляться: следующий этап — интеграция ExecuteCommand
и ядра Microsoft, затем публикация настоящего TPM-транспорта.

## Изменение ACPI-кода

В MyTpm2EMU после изменения ASL пересоздайте встраиваемый массив:

```sh
iasl -tc VtpmRuntimeProbe.asl
```

VtpmRuntimeProbe.hex включён в DXE напрямую и уже обновлён. Обычная сборка EDK II
не перекомпилирует этот ASL автоматически. Порты и значения в ASL должны совпадать
с VtpmProbe.h. Порты/SMI-команда предназначены для текущего QEMU q35 стенда;
на физическом ПК механизм нужно согласовать с его прошивкой и chipset.

## Ядро и RAM NV

Дополнительный SMM-модуль `TpmCoreProbeSmm` проверяет маршалинг и криптографический
backend ядра, затем платформенное хранилище из `TpmCoreLib/Port/TpmNv.c`.
После пяти старых проверок должны появиться:

```text
[VTPM-NV] erase=PASS read-write=PASS overlap=PASS bounds=PASS lifecycle=PASS
[VTPM-NV] PASS: volatile SMM storage; no persistence; mask=0x1F
```

Проба завершает работу обнулением NV. Реальные команды TPM и ключи пока не
инициализируются. Данные будущего RAM-backend будут теряться при выключении;
постоянное хранилище нужно реализовать до использования постоянных ключей.
Следующий блок — энтропия, часы и прочие платформенные функции, затем Manufacture
и ExecuteCommand. Подробности: `TpmCoreLib/README.md`.

Источник энтропии добавлен в `TpmCoreLib/Port/TpmEntropy.c`; после NV-пробы
ожидаются `[VTPM-RNG] init=PASS short=PASS partial=PASS repeat=PASS` и
`[VTPM-RNG] PASS: RDRAND source; mask=0xF`. CPU гостя должен предоставлять RDRAND;
при его отсутствии проба сообщает FAIL и продолжает диагностическую загрузку.

Часы TPM добавлены в `TpmCoreLib/Port/TpmClock.c`. После RNG-пробы ожидаются:

```text
[VTPM-CLOCK] flags=PASS advance=PASS monotonic=PASS restart=PASS rate=PASS
[VTPM-CLOCK] PASS: calibrated TSC; mask=0x1F
```

TSC калибруется по ACPI-таймеру; runtime-вызовы не используют Boot Services.
Это backend для текущего стенда, не проверка точности часов на физическом ПК.

Следующий platform-блок добавлен в `TpmCoreLib/Port/TpmPcr.c` и `TpmPlatform.c`:
PCR-параметры/начальные значения, информация о прошивке, power/locality/cancel/PP.
После clock-пробы ожидается:

```text
[VTPM-PLAT] info=PASS pcr-profile=PASS pcr-values=PASS pcr-bounds=PASS signals=PASS
[VTPM-PLAT] PASS: platform callbacks; mask=0x1F
```

ACT и FW/SVN-limited временно отключены в локальном профиле до реализации их
backend. Настоящие TPM-команды ещё не выполняются.

## Настоящие команды ядра

После прежних проб TpmCoreProbeSmm однократно запускает RAM NV manufacture,
_TPM_Init и команды через ExecuteCommand:

```text
[VTPM-CMD] init=PASS startup=PASS capability=PASS selftest=PASS random=PASS errors=PASS
[VTPM-CMD] PASS: ExecuteCommand; mask=0x3F stage=0
```

Проверяются реальные TPM_RC, формат и содержимое ответа; ключевой материал
в лог не выводится. Ошибка initialization перехватывается и диагностическая
загрузка продолжается. На CPU без RDRAND ожидается CMD FAIL.
Постоянного NV нет: hierarchy seeds сейчас новые при каждой загрузке.
Состояние ядра после пробы сохраняется внутри SMM; runtime-интеграция впереди.
Подробности: TpmCoreLib/README.md; повторение boot-пробы:
`python3 MyTpm2EMU/Tests/run-commands-smoke.py` из edk2.

## Реальные команды через doorbell

Единственный SMM core публикует частный protocol; транспорт вызывает его без
собственной копии TpmCoreLib. До EBS DXE проверяет GetCapability/GetRandom через
общую память. После EBS диагностический VPRB._INI теперь повторяет обе команды.
Ожидаемые новые строки в Windows логе:

```text
[VTPM-CMD] SMM core protocol installation: Success
[VTPM-DXE] PASS: TPM GetCapability through shared buffer
[VTPM-DXE] PASS: TPM GetRandom through shared buffer
[VTPM-SMM] TPM command=0x17A rc=0x0 response=27 phase=after-EBS
[VTPM-SMM] TPM command=0x17B rc=0x0 response=44 phase=after-EBS
```

После двух новых boot-запросов sequence номера изменятся: первая ОС-проба
обычно начинается с 7. Это диагностическое устройство VTP0001, пока не MSFT0101.
Private _DSM revision 1 function 2 запускает обе команды; это не TCG TPM _DSM.
Result 4 означает недоступный/ошибочный core; при CPU без RDRAND такой отказ ожидается.
Mailbox остаётся 64-byte: разрешены только точные manufacturer capability и
GetRandom(32). Для Windows нужен полный transport и TPM ACPI/CRB.
ASL/AML/HEX уже обновлены; обычная пересборка использует новый встроенный HEX.

Исправление Windows ACPI failure после предыдущей сборки: в SystemMemory поле
TPM-буферов заменено BufferAcc → ByteAcc (512-bit FieldUnit остаётся Buffer).
ASL/AML/HEX уже обновлены. Пересоберите полный OVMF и повторите загрузку Windows.
Проверка: `python3 MyTpm2EMU/Tests/check-acpi-mailbox.py` из корня edk2.
Windows retest ещё требуется; ожидаемые after-EBS строки остаются прежними.

## Полный диагностический канал

Windows лог `00-14-20` подтверждает отсутствие краша и TPM-команды после EBS.
Теперь добавлен отдельный full packet: request/response по 4096 bytes в четырёх
страницах ACPI NVS. Старая ACPI-проба работает по прежним offsets.
После прежних boot-проверок ожидается:

```text
[VTPM-FULL] PASS: test=1 result=0 response=151
...
[VTPM-FULL] PASS: full buffers, TPM errors and bounds verified
```

Первая длина зависит от algorithm profile; она должна быть >64 bytes.
Тесты 5/6/7 ожидают Result=3, остальные — Result=0; TPM ошибки проверяются внутри
ответа. Full transport поддерживает произвольные команды, но ещё не является CRB.
GetRandom(64) в текущем профиле возвращает 48 bytes (response 60), это допустимо.
Новые full-запросы пока отправляются при загрузке DXE; проверка full после EBS впереди.

## Диагностическая модель CRB

Full-проба подтверждена на Windows (лог 00-25-17). Добавлены VtpmCrbSmm.c и
VtpmCrbProbeDxe.c: CRB layout, locality0, ready/idle/start/completion, закреплённые
адреса и pre-execution cancel. Теперь выделяется 5 страниц NVS; AML не менялся.
После пересборки ожидается итоговая строка:

```text
[VTPM-CRB] PASS: diagnostic register model verified
```

Восемь групп проверок прошли в QEMU. RC=0x909 (pre-execution cancel) и RC=0x142
(invalid command size) в CRB-пробе ожидаемы. Модель обрабатывает записи только при
специальном diagnostic SMI: обычные MMIO stores не запускают SMM. Стандартный
Windows TPM-драйвер пока не подключается; следующий этап — ACPI/control handshake
и публикация TPM2/MSFT0101. Тест: `Tests/run-crb-smoke.py` из edk2.


Windows connection staging (2026-10-06): VtpmControlSmm.c implements an
independent 48-byte ACPI TPM2 Control Area with separate 4 KiB command/response
buffers. Data token 0x53 accepts a private SMRAM snapshot without execution;
0x54 executes that snapshot. VtpmControlProbeDxe.c checks pending rejection,
external buffer mutation, cancellation before execution, bounds and recovery.
No MSFT0101/TPM2 publication yet: an automatic deferred worker is required for
non-blocking ACPI StartMethod=2. Diagnostic CRB is confirmed in Windows boot
log edk2_debug_2026-10-06_00-36-28.log; Control Area is a new stage to retest.


## Windows TPM transport — текущий этап

VtpmTimerSmm.c запускает worker по QEMU q35 software-SMI timer, без второго
ручного вызова. При успешной пробе VtpmWindowsDxe.c публикует TPM2 revision 3
(StartMethod=2) и SSDT VtpmWindows.asl: MSFT0101, _CRS и стандартный Start _DSM.
Command/response буферы отдельные; адреса контролируются SMM. Общие восемь
страниц теперь reserved и выделяются ниже 4 GiB. Повторная TPM2 не допускается.
Если core или timer отсутствуют, новый TPM Windows не объявляется.

После пересборки ожидаются:
```
[VTPM-WIN] PASS: automatic deferred SMI completion
[VTPM-WIN] PASS: TPM2/MSFT0101 published; StartMethod=2 control=0x...
```
В гостевой Windows проверьте Device Manager / Security devices, затем скопируйте
Tests/Test-VtpmTbs.ps1 в гостя и выполните:
```
powershell -ExecutionPolicy Bypass -File .\Test-VtpmTbs.ps1
```
Скрипт вызывает tbs.dll, проверяет TPM 2.0, manufacturer VTPM и GetRandom;
TPM-state-changing commands не отправляет. Его runtime пока не проверен в Windows.
Для логов после EBS теперь ожидаются [VTPM-ACPI] Start accepted и
[VTPM-CTRL] command=... phase=after-EBS. Старые VPRB строки отдельно подтверждают
диагностический канал и сами по себе не доказывают работу tpm.sys.

QEMU 11.1.1 имеет timer по умолчанию; x-smi-swsmi-timer должен быть on.
Для timer backend теперь требуется -global ICH9-LPC.x-smi-broadcast=on
и VtpmSmmCpuFeaturesLib в PiSmmCpuDxeSmm (DSC уже исправлен). Ранний CPU hook
отправляет SMI остальным vCPU, сохраняя traditional rendezvous. Relaxed режим
отклоняется: он вызывал гонку InSmm со службой UEFI-переменных в Windows.
run-qemu-win11.sh уже исправлен; прежняя рекомендация broadcast=off отменена. Время boot-пробы печатается как
[VTPM-WIN] deferred round-trip=... us; предел 250 ms блокирует медленную публикацию.
Таймер — QEMU-specific, физический ICH9 отличается. Эта версия предназначена
для проверки связи через TBS; persistent NV, PPI/MOR, measured boot и полноценное
provisioning ещё не готовы. После перезапуска firmware NV/keys теряются.


Code 12 follow-up: TPM _CRS no longer claims platform-owned B2/B3 SMI ports;
it contains only the allocated 3-page memory window. The AML OperationRegion
still triggers SMI. User TBS result 0x80284001 is TBS_E_INTERNAL_ERROR, not a
PowerShell parsing error. Retest Device Manager and TBS after a full guest reboot.


SMI rendezvous regression: Tests/run-smi-latency.py now checks fast traditional
delivery with early IPIs (~17 ms) and rejects relaxed delivery. The earlier
broadcast-off workaround caused a Windows runtime-variable ASSERT and is
superseded. Rebuild the firmware and use broadcast=on with the corrected launcher.


## Runtime-variable ASSERT: current correction

User log 01-19-17 contains VariableSmmRuntimeDxe.c(183) ASSERT, Invalid Parameter,
while TPM continues on another vCPU. PiSmmIpl Communicate sees the shared InSmm
flag set by another CPU and rejects a runtime virtual-mode call. Relaxed sync
was therefore inappropriate for asynchronous TPM SMIs in this OVMF version.

VtpmSmmCpuFeaturesLib is a copy of the original OVMF library with a narrow
adaptation: retain CPU-driver-owned hotplug data at SMBASE initialization;
on CPU index 0, for active q35 software-timer SMI, send SMI IPIs to other valid
APIC IDs before the standard CPU rendezvous. Original save-state/SEV code remains.
DSC uses the new library; the original OVMF source is untouched. Runtime CPU sync
policy is sampled at driver initialization before ReadyToLock, not read from the
DXE PCD database inside runtime SMM. The boot timer probe verifies AP presence
via SmmStartupThisAp and prints:
```
[VTPM-SMI] CPU sync snapshot: traditional
[VTPM-SMI] PASS: all 2 CPUs in SMM before timer worker
[VTPM-WIN] deferred round-trip=... us
```
Rebuild plus full guest restart required. Do not use x-smi-broadcast=off anymore.
Windows boot without ASSERT still needs a guest retest. TBS/provisioning remain
separate checks; persistent NV is not added by this correction.


## Windows TBS confirmed — 2026-10-06

User confirmed normal Device Manager status and successful Test-VtpmTbs.ps1:
structVersion=1, tpmVersion=2, tpmInterfaceType=3, tpmImpRevision=0,
GetCapability PASS, GetRandom PASS, Windows TBS PASS. Firmware log 01-32-17
confirms six vCPUs in traditional SMM rendezvous, 17652 us boot probe and
successful after-EBS commands without ASSERT/FAIL. Desktop boots; a small
residual stutter was reported. Windows transport milestone is complete.
Next priority: persistent NV/seeds/keys, object/NV lifecycle checks, followed
by provisioning interfaces and measured boot. Current NV remains volatile.
# Tpm20-uefiport
