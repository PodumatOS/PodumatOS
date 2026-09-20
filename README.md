<div align="center">
  <p align="center">
    <img src="assets/logo.png" width="256" alt="PodumatOS">
  </p>

  <br>
  <h1>PodumatOS</h1>
  <h4>A customizable hobby OS for x86_64. Written from scratch in C++ and NASM. Booted by Limine. Built for transparency and simplicity.</h4>

![License](https://img.shields.io/badge/BSD%202--Clause-E84A4A?style=for-the-badge&logo=freebsd&logoColor=white)
![Version](https://img.shields.io/badge/version-0.1-0898FF?style=for-the-badge)

![C++](https://img.shields.io/badge/C++-0898FF?style=for-the-badge&logo=cplusplus&logoColor=white)
![NASM](https://img.shields.io/badge/NASM-4A67E8?style=for-the-badge&logo=intel&logoColor=white)
![Limine](https://img.shields.io/badge/Limine-AA6EF0?style=for-the-badge&logo=lospec&logoColor=white)

![GitHub commit activity](https://img.shields.io/github/commit-activity/m/PodumatOS/PodumatOS?style=for-the-badge)
![GitHub stars](https://img.shields.io/github/stars/PodumatOS/PodumatOS?style=for-the-badge)
![GitHub watchers](https://img.shields.io/github/watchers/PodumatOS/PodumatOS?style=for-the-badge)
![GitHub repo size](https://img.shields.io/github/repo-size/PodumatOS/PodumatOS?style=for-the-badge)

  <p align="center">
    <img src="assets/thumbnail.png" width="700" alt="PodumatOS">
  </p>
</div>

<br>

---

## What was this operating system created for?
**PodumatOS** is designed for specific goals:

- **Complete system customization.** During installation, you choose exactly which components to include — from drivers to the package manager itself. Want to disable Bluetooth entirely? Don't install it. Prefer git over our package manager? Build the system without it. You control what runs on your machine.
- **Learning.** Licensed under BSD-2-Clause, PodumatOS code is free to study, adapt, and reuse — even in your own projects. Read the source, borrow ideas, fork the kernel, or use individual drivers in your own OS. You don't need permission; the license already grants it.
- **Legacy hardware.** Built for older machines. PodumatOS falls back gracefully: if there is no AHCI, it uses ATA PIO. If there is no USB, it uses PS/2. Minimum requirements are low — a Pentium or later with a few hundred MB of RAM is enough.
- **Transparency and control.** No telemetry. No phoning home. No hidden processes. Every line of code is open and auditable — you can read it, understand it, and verify that it does exactly what it claims. All system events are logged to the serial console (COM1), visible from boot to shutdown.
- **Built by enthusiasts, for enthusiasts.** PodumatOS is an open project, and contributions are welcome. Whether you want to write drivers, improve the shell, add filesystem support, or just report a bug — your help is appreciated. Check the [CONTRIBUTING.md](CONTRIBUTING.md) guide to get started.
