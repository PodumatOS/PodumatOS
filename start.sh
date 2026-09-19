#!/bin/bash
set -e

cd "$(dirname "$0")"

# iso check
if [ ! -f "PodumatOS-x86_64.iso" ]; then
    echo "[ERROR] PodumatOS-x86_64.iso not found!"
    echo "        Run './build.sh build' first."
    exit 1
fi

# creating img
if [ ! -f "disk.img" ]; then
    echo "[INFO] Creating disk.img (64 MB)..."
    qemu-img create -f raw disk.img 64M
fi

# menu
echo ""
echo "================================"
echo "  PodumatOS — QEMU Launch"
echo "================================"
echo ""
echo "  Select platform:"
echo ""
echo "   1) Full Legacy  - i440fx, ATA PIO, PS/2"
echo "                     (Legacy: BIOS, IDE, keyboard PS/2)"
echo ""
echo "   2) Newer        - q35, AHCI, USB 2.0 (EHCI + usb-kbd)"
echo "                     (not XHCI)"
echo ""
echo "   3) Now          - q35, NVMe, USB 3.0 (XHCI + usb-kbd)"
echo "														  "
echo ""
echo "   0) Exit"
echo ""

read -p "  Choose [1-3]: " CHOICE
echo ""

case "$CHOICE" in
    1)
        echo "  Platform: Full Legacy (i440fx + ATA PIO + PS/2)"
        echo "  ISO:      PodumatOS-x86_64.iso"
        echo "  Disk:     disk.img (64 MB, IDE)"
        echo ""
        echo "  Exit QEMU: Ctrl+A, X"
        echo ""

        qemu-system-x86_64 \
            -name "PodumatOS Legacy" \
            -m 512M \
            -smp 1 \
            -machine pc \
            -cpu qemu64 \
            \
            -cdrom PodumatOS-x86_64.iso \
            -boot d \
            \
            -drive file=disk.img,format=raw,if=ide,index=0,media=disk \
            \
            -vga std \
            -display gtk \
            \
            -serial stdio \
            -rtc base=localtime
        ;;

    2)
        echo "  Platform: Newer (q35 + AHCI + EHCI)"
        echo "  ISO:      PodumatOS-x86_64.iso"
        echo "  Disk:     disk.img (64 MB, AHCI)"
        echo "  USB:      EHCI + usb-kbd"
        echo ""
        echo "  Exit QEMU: Ctrl+A, X"
        echo ""

        qemu-system-x86_64 \
            -name "PodumatOS Newer" \
            -m 512M \
            -smp 1 \
            -machine q35 \
            -cpu qemu64 \
            \
            -cdrom PodumatOS-x86_64.iso \
            -boot d \
            \
            -device ahci,id=ahci0 \
            -drive file=disk.img,format=raw,if=none,id=drv0,media=disk \
            -device ide-hd,drive=drv0,bus=ahci0.0 \
            \
            -device usb-ehci,id=ehci0 \
            -device usb-kbd,bus=ehci0.0,port=1 \
            \
            -vga std \
            -display gtk \
            \
            -serial stdio \
            -rtc base=localtime
        ;;

    3)
        echo "  Platform: Now (q35 + NVMe + XHCI)"
        echo "  ISO:      PodumatOS-x86_64.iso"
        echo "  Disk:     disk.img (64 MB, NVMe)"
        echo "  USB:      XHCI + usb-kbd"
        echo ""
        echo "  NOTE: NVMe и XHCI драйверы ещё не реализованы."
        echo "        Система должна загрузиться, но disk/usb не найдутся."
        echo ""
        echo "  Exit QEMU: Ctrl+A, X"
        echo ""

        qemu-system-x86_64 \
            -name "PodumatOS Now" \
            -m 512M \
            -smp 1 \
            -machine q35 \
            -cpu qemu64 \
            \
            -cdrom PodumatOS-x86_64.iso \
            -boot d \
            \
			-device nvme,serial=podumat,id=nvme0 \
			-drive file=disk.img,format=raw,if=none,id=drv0 \
			-device nvme-ns,drive=drv0,bus=nvme0
            \
            -device qemu-xhci,id=xhci0 \
            -device usb-kbd,bus=xhci0.0,port=1 \
            \
            -vga std \
            -display gtk \
            \
            -serial stdio \
            -rtc base=localtime
        ;;

    0)
        echo "  Bye."
        exit 0
        ;;

    *)
        echo "  [ERROR] Invalid choice: '$CHOICE'"
        exit 1
        ;;
esac