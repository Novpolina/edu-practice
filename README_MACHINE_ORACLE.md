# Machine Oracle: syscall -> SBI -> M-mode CSR

Я реализовала небольшую цепочку, которая проходит из userspace до OpenSBI в M-mode и возвращает значение обратно в приложение, добавила новый Linux syscall `machine_oracle`, новый SBI extension в OpenSBI и userspace-приложение `/opt/machine_oracle`. В OpenSBI читается machine-mode CSR `CSR_MVENDORID`, а приложение печатает значение, которое пришло обратно через SBI и syscall.

Приложение само не читает machine CSR. Оно просит ядро, ядро делает SBI-вызов, а реальное чтение происходит в OpenSBI, где есть M-mode.

## Как работает цепочка

```text
/opt/machine_oracle
    -> syscall(__NR_machine_oracle)
    -> sys_machine_oracle()
    -> sbi_ecall(0x0900CAFE, 0)
    -> OpenSBI Machine Oracle extension
    -> csr_read(CSR_MVENDORID)
    -> return value
    -> printf()
```

## Что изменено в коде

### OpenSBI

- `opensbi/include/sbi/sbi_ecall_interface.h` - добавила ID расширения `SBI_EXT_MACHINE_ORACLE = 0x0900CAFE` и function IDs для чтения CSR.
- `opensbi/lib/sbi/sbi_ecall_machine_oracle.c` - реализовала обработчик SBI extension; function ID `0` читает `CSR_MVENDORID`.
- `opensbi/lib/sbi/objects.mk` - подключила новый обработчик к сборке OpenSBI и к списку ecall extensions.

### Linux kernel

- `linux/scripts/syscall.tbl` - зарегистрировала syscall `machine_oracle` с номером `471`.
- `linux/arch/riscv/include/asm/sbi.h` - добавила Linux-константы для `0x0900CAFE` и function ID `0`.
- `linux/arch/riscv/kernel/sys_riscv.c` - реализовала `SYSCALL_DEFINE0(machine_oracle)`, который вызывает `sbi_ecall()`.

### userspace/rootfs

- `demo_races/machine_oracle.c` - добавила userspace-приложение, которое вызывает `syscall(__NR_machine_oracle)` и печатает результат.
- `6_demos.sh` - добавила сборку бинарника в `rootfs_overlay/opt/machine_oracle`, чтобы после пересборки rootfs он оказался в `/opt/machine_oracle`.

### scripts/docs

- `5_opensbi.sh` - оставила новый файл OpenSBI handler вне удаления через `git clean`
- `demo-machine-oracle.log` - лог runtime-проверки в QEMU.

## Технические детали

- syscall name: `machine_oracle`
- syscall number: `471`
- SBI extension ID: `0x0900CAFE`
- function ID: `0`
- function name: `SBI_EXT_MACHINE_ORACLE_READ_MVENDORID`
- CSR: `CSR_MVENDORID`
- userspace path: `/opt/machine_oracle`

## Как пересобрать

Для обычной сборки проект после изменений запускается из корня репозитория:

```sh
./5_opensbi.sh
./3_linux_prepare.sh
./4_linux.sh
./6_demos.sh
./7_rootfs.sh
```
## Как запустить

```sh
./8_run.sh
```

Этот скрипт запускает QEMU с OpenSBI, ядром Linux и initramfs/rootfs из `output/`.

## Демонстрация работы

Внутри QEMU я показываю:

```sh
root
uname -a
ls -lh /opt/machine_oracle
/opt/machine_oracle
```

Linux загрузился, бинарник существует, приложение запускается и печатает `mvendorid`, полученный из M-mode.

## Проверка в runtime

Я проверила запуск внутри QEMU и сохранила лог в `demo-machine-oracle.log`.

Ключевой фрагмент лога:

```text
OpenSBI v1.8
Linux (none) 6.19.0-dirty #1 SMP PREEMPT Thu May 21 03:26:01 MSK 2026 riscv64
# ls -lh /opt/machine_oracle
-rwxr-xr-x 1 root root 556712 /opt/machine_oracle
# /opt/machine_oracle
Machine Oracle says:
mvendorid from M-mode = 0x0000000000000000
machine_oracle exit status: 0
RUNTIME CHECK PASSED
```

По этому логу видно, что:

- OpenSBI загрузился.
- Linux загрузился.
- `/opt/machine_oracle` есть внутри гостевой системы.
- Приложение запустилось.
- Syscall не вернул `ENOSYS`.
- SBI-вызов не упал.
- Значение `mvendorid` вернулось в userspace.
- Приложение завершилось с `exit status: 0`.