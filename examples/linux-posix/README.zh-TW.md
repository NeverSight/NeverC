**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux POSIX API 範例

演示使用 NeverC 交叉編譯到 Linux 的 POSIX 系統程式設計：pthreads、mmap、pipe 和訊號處理。

NeverC 在 `runtime/linux/` 中內建了 Linux sysroot（Ubuntu 22.04，glibc 2.35），單次呼叫即可完成預處理、編譯、最佳化（auto-LTO）以及透過內建連結器進行連結。

## 建置

```bash
cd examples/linux-posix
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## 手動建置

```bash
neverc --target=x86_64-linux-gnu -Wall -lpthread -o posix-demo main.c
```

## 執行

```bash
chmod +x posix-demo
./posix-demo
```

## 功能說明

- **pthreads**：建立 4 個工作執行緒
- **mmap**：分配匿名記憶體頁面
- **pipe**：透過 Unix 管道傳送訊息
- **signals**：安裝 `SIGUSR1` 處理程式
