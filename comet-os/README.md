# Comet OS 1.0 "Perseid"

A from-scratch 32-bit desktop OS: NASM + C kernel, GRUB (multiboot) boot, 32-bpp framebuffer compositor,
Windows-11-inspired (but not copied) shell, 13 apps, live-to-disk installer. Output: `comet-os.iso`.

## Build (Debian/Ubuntu/WSL)
    ./build.sh            # installs gcc-multilib nasm grub-pc-bin xorriso mtools if needed, builds comet-os.iso
    ./tools/vbox-create.sh   # creates + boots a VirtualBox VM with the right settings

Manual VM settings: Type Other/Unknown (32-bit), 512 MB+ RAM, Graphics Controller **VBoxVGA**, VRAM 64-128 MB,
Pointing device **PS/2 Mouse**, **IDE** controller with a blank disk on primary master and the ISO on secondary master.

## Directory
    comet-os/
      build.sh  Makefile  linker.ld  README.md
      grub/grub.cfg            GRUB menu (multiboot + 1280x800x32 request)
      tools/vbox-create.sh     VirtualBox VM creator
      src/boot.asm             multiboot header, stack, entry, gdt_flush      (NASM)
      src/isr.asm              48 interrupt stubs + common handler           (NASM)
      src/kernel.h             shared types, port I/O, every module's API
      src/kernel.c             heap, libc bits, GDT/IDT/PIC/PIT, RTC, PCI, CPUID, kmain
      src/gfx.c  src/font.h    framebuffer, AA shapes, blur/acrylic/mica, wallpaper, cursor, icons, BGA mode switching
      src/input.c              PS/2 keyboard (Win key, Alt+Tab...), PS/2 mouse with scroll wheel
      src/fs.c                 RAM filesystem, tar create/list/extract, CIM+BMP images, sample content
      src/disk.c               ATA PIO + ATAPI drivers, live-to-disk installer, saved settings
      src/wm.c                 window manager, compositor, taskbar, Start, flyouts, snap, desktops, task view
      src/apps.c               Terminal, Files, Notes, System Monitor, Task Manager, Store, Photos, Comet Rush,
                               Settings, Comet Setup, Calculator, Paint, Snake

## Shortcuts
Win = Start (type to search) | Win+D show desktop | Win+Tab Task View / virtual desktops | Ctrl+Win+Left/Right switch desktop |
Win+Arrows snap/maximize | Alt+Tab | Alt+F4 | Ctrl+Shift+Esc Task Manager | Win+A quick settings | Win+W widgets | Win+N calendar |
Win+E Files | Win+T Terminal | Win+I Settings | Win+1..9 taskbar apps. Hover the maximize button for snap layouts; drag to an edge to snap.

## Installing to disk
Boot the ISO, open **Comet Setup** (Start menu), confirm. It copies the live image (ATAPI read -> ATA PIO write) to the
IDE disk, which then boots through GRUB on its own; remove the ISO afterwards. Theme/accent/resolution persist on the installed disk.

## Honest limitations
* **VirtualBox Guest Additions cannot run here.** They are Linux/Windows/Solaris kernel modules. Instead Comet OS programs the
  Bochs VBE (BGA) registers that VBoxVGA emulates: resolution can be switched live (Settings > Display, `resolution WxH`), and
  every layout is computed from screen size. It does not auto-follow VM window resizing and has no seamless/shared-folder/clipboard features.
* **Input is PS/2 only** (keyboard + wheel mouse). Set the VM pointing device to PS/2 Mouse; click once to capture it (Host key releases).
  No USB stack, so no USB tablet/absolute pointer.
* No audio and no network drivers: the volume slider and network tile are status-only (the NIC is detected over PCI and named).
* Memory is flat (no paging, no user mode); "processes" are app windows with real per-window CPU accounting via rdtsc.
* Files live in RAM (reset on reboot); the installer copies the OS, not user data. Notes saved to disk are not yet persisted.
* **This was compiled and linked in a sandbox without NASM/GRUB/VirtualBox, so it has not been booted.** Expect to fix a few
  first-boot issues; if it black-screens, try the QEMU target (`make run-qemu`) and `-vga std` first.

## Meteorite (native app) and startup apps
`Meteorite` is a Comet OS rewrite of the Meteorite Kilo dashboard (Overview, System, Productivity, Search, Utilities, Settings).
It opens at every boot. Settings > "Open at startup" (inside Meteorite) picks which apps launch at boot; the list is saved on an installed disk.
Not ported (no equivalent in Comet OS): AI assistant, music/WinRT media, OneDrive backup, PowerShell terminal, Windows launchers.
Brave, a VPN and a real antivirus cannot run on this kernel (no network stack, no browser engine).
