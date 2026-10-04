; SPDX-License-Identifier: BSD-2-Clause
; Copyright (c) 2026 Thinking Developer
;
; PodumatOS — a hobby operating system for x86_64.

[bits 64]
section .text

extern irq_handler

%macro pushaq 0
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro popaq 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push 0
    push %1
    pushaq

    mov rdi, %1
    mov rbx, rsp
    and rsp, -16

    lea rax, [rel irq_handler]
    call rax

    mov rsp, rbx

    popaq
    add rsp, 16
    iretq
%endmacro

ISR_NOERRCODE 32
ISR_NOERRCODE 33
ISR_NOERRCODE 34
ISR_NOERRCODE 35
ISR_NOERRCODE 36
ISR_NOERRCODE 37
ISR_NOERRCODE 38
ISR_NOERRCODE 39
ISR_NOERRCODE 40
ISR_NOERRCODE 41
ISR_NOERRCODE 42
ISR_NOERRCODE 43
ISR_NOERRCODE 44
ISR_NOERRCODE 45
ISR_NOERRCODE 46
ISR_NOERRCODE 47

global isr_default
isr_default:
    push 0
    push 255
    pushaq

    mov rdi, 255
    mov rbx, rsp
    and rsp, -16

    lea rax, [rel irq_handler]
    call rax

    mov rsp, rbx

    popaq
    add rsp, 16
    iretq