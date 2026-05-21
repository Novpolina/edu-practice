# Page Tree Oracle: визуализация пользовательских page tables

Второе задание через procfs внутри Linux kernel. Я добавила файл `/proc/page_tree_oracle`: в него можно записать PID процесса, а при чтении ядро показывает дерево валидных пользовательских page table entries для этого процесса. Обход идёт по VMA и уровням `PGD -> P4D -> PUD -> PMD -> PTE`, а в строках PTE видны virtual address, PFN и RISC-V флаги.

Для демонстрации добавлено userspace-приложение `/opt/page_tree_target`.

## Как работает цепочка

```text
/opt/page_tree_target
    -> process allocates/touches heap and mmap pages
    -> echo <pid> > /proc/page_tree_oracle
    -> cat /proc/page_tree_oracle
    -> page_tree_oracle_show()
    -> find_get_pid() / pid_task()
    -> get_task_mm()
    -> mmap_read_lock(mm)
    -> VMA_ITERATOR + for_each_vma()
    -> pgd_offset()
    -> p4d_offset()
    -> pud_offset()
    -> pmd_offset()
    -> pte_offset_map_lock()
    -> ptep_get()
    -> seq_file output
```

## Что изменено в коде

### Linux kernel

- `linux/fs/proc/page_tree_oracle.c` - новый procfs-визуализатор: хранит выбранный PID, находит `task/mm`, обходит VMA и печатает present page table entries.
- `linux/fs/proc/Makefile` - подключает `page_tree_oracle.o` к сборке procfs при `CONFIG_MMU`.

### userspace/rootfs

- `demo_races/page_tree_target.c` - программа-цель для демо: выделяет heap, делает anonymous `mmap`, трогает страницы, печатает PID и спит.
- `6_demos.sh` - собирает `demo_races/page_tree_target.c` в `rootfs_overlay/opt/page_tree_target`;

### docs/runtime
- `demo-page-tree-oracle.log` - лог runtime-проверки в QEMU.

## Технические детали

- procfs-файл: `/proc/page_tree_oracle`
- userspace target: `/opt/page_tree_target`
- ввод PID: `echo <pid> > /proc/page_tree_oracle`
- чтение дерева: `cat /proc/page_tree_oracle`
- лимит вывода: `PAGE_TREE_ORACLE_MAX_PTES = 1024`
- VMA traversal: `VMA_ITERATOR`, `for_each_vma`
- locking: `mmap_read_lock(mm)` / `mmap_read_unlock(mm)`
- task/mm: `find_get_pid`, `pid_task`, `get_task_mm`, `mmput`
- page table walk: `pgd_offset`, `p4d_offset`, `pud_offset`, `pmd_offset`, `pte_offset_map_lock`, `ptep_get`
- RISC-V flags: `V|R|W|X|U|A|D|G`

Я печатаю только present PTE. Для huge/leaf PMD/PUD сделана отдельная строка leaf, но обычный демо-процесс в QEMU показывает классические PTE.

## Почему это делается в kernel, а не через pagemap

`/proc/<pid>/pagemap` показывает итоговую информацию по страницам, но для этого задания нужно увидеть именно структуру дерева таблиц страниц: `PGD -> P4D -> PUD -> PMD -> PTE`. Поэтому я добавила kernel-side обход page table helpers.

## Как пересобрать

Для полного окружения edu-practice:

```sh
./3_linux_prepare.sh
./4_linux.sh
./6_demos.sh
./7_rootfs.sh
```

Если OpenSBI тоже нужно пересобрать

```sh
./5_opensbi.sh
```

## Как запустить

```sh
./8_run.sh
```

## Работа внутри QEMU

В полноценном rootfs сценарий такой:

```sh
root
ls -lh /proc/page_tree_oracle
ls -lh /opt/page_tree_target
/opt/page_tree_target &
echo <pid> > /proc/page_tree_oracle
cat /proc/page_tree_oracle
```

`<pid>` я беру из строки `target pid: ...`, которую печатает `/opt/page_tree_target`.

## Какой вывод ожидается

Формат не обязан быть побайтно одинаковым, но в выводе должны быть PID, имя процесса, VMA ranges и уровни `PGD/P4D/PUD/PMD/PTE`:

```text
Page Tree Oracle
pid: 60
comm: page_tree_targe
mm: ff60000002748000
page size: 4096
pte output limit: 1024

VMA 0000000000010000-0000000000085000 r-xp /opt/page_tree_target
PGD[000] present val=0x0000000020996c01
  P4D[000] present val=0x0000000020867001
    PUD[000] present val=0x0000000020867401
      PMD[000] present val=0x0000000020867801
        PTE[010] VA 0x0000000000010000 PFN 0x820fa flags=V|R|X|U|A raw=0x000000002083e85b
        PTE[011] VA 0x0000000000011000 PFN 0x820fb flags=V|R|X|U|A raw=0x000000002083ec5b

VMA 0000000000091000-00000000000b3000 rw-p [heap]
PGD[000] present val=0x0000000020996c01
  P4D[000] present val=0x0000000020867001
    PUD[000] present val=0x0000000020867401
      PMD[000] present val=0x0000000020867801
        PTE[091] VA 0x0000000000091000 PFN 0x8831a flags=V|R|W|U|A|D raw=0x00000000220c68d7
```

## Проверка в runtime

Я запустила QEMU и получила лог `demo-page-tree-oracle.log`. Проверка выполнялась с системным `/usr/bin/qemu-system-riscv64` и минимальным initramfs, потому что в локальном дереве не было полного buildroot SDK/rootfs. Сама цепочка `target process -> /proc/page_tree_oracle -> kernel page table walk -> ASCII output` проверена внутри QEMU.

Ключевой фрагмент:

```text
OpenSBI v1.8
Linux (none) 6.19.0-dirty #2 SMP PREEMPT Thu May 21 04:53:51 MSK 2026 riscv64
# ls -lh /proc/page_tree_oracle
-rw-r--r-- 1 root root 0 /proc/page_tree_oracle
# ls -lh /opt/page_tree_target
-rwxrwxr-x 1 root root 598728 /opt/page_tree_target
# /opt/page_tree_target &
started page_tree_target pid: 60
target pid: 60
page_tree_target: heap=0x92680 mmap=0x7fff8d190000 checksum=1058
# echo 60 > /proc/page_tree_oracle
# cat /proc/page_tree_oracle
Page Tree Oracle
pid: 60
comm: page_tree_targe

VMA 0000000000010000-0000000000085000 r-xp /opt/page_tree_target
PGD[000] present val=0x0000000020996c01
  P4D[000] present val=0x0000000020867001
    PUD[000] present val=0x0000000020867401
      PMD[000] present val=0x0000000020867801
        PTE[010] VA 0x0000000000010000 PFN 0x820fa flags=V|R|X|U|A raw=0x000000002083e85b

VMA 00007fffe4afe000-00007fffe4b1f000 rw-p [stack]
PGD[000] present val=0x0000000020996c01
  P4D[0ff] present val=0x0000000020865c01
    PUD[1ff] present val=0x0000000020866401
      PMD[125] present val=0x0000000020866801
        PTE[11d] VA 0x00007fffe4b1d000 PFN 0x88318 flags=V|R|W|U|A|D raw=0x00000000220c60d7
page_tree_oracle exit status: 0
RUNTIME CHECK PASSED
```

По runtime-логу видно, что:

- OpenSBI загрузился.
- Linux загрузился с новым ядром.
- `/proc/page_tree_oracle` существует.
- `/opt/page_tree_target` существует.
- target-процесс запустился и напечатал PID.
- PID записался в procfs-файл.
- `cat /proc/page_tree_oracle` показал VMA и уровни `PGD/P4D/PUD/PMD/PTE`.
- В строках PTE есть VA, PFN, flags и raw PTE value.
- Kernel panic/oops в логе нет.

