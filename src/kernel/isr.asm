; SPDX-License-Identifier: BSD-2-Clause
; Copyright (c) 2026 Thinking Developer
;
; PodumatOS — a hobby operating system for x86_64.


[bits 64]
section .text

extern irq_handler

%macro SERIAL_CHAR 1
    push rax
    push rdx
    mov al, %1
    mov dx, 0x3F8
    out dx, al
    pop rdx
    pop rax
%endmacro

%macro SERIAL_HEX 1
    push rax
    push rdx
    push rbx
    mov bl, %1
    mov al, bl
    shr al, 4
    cmp al, 10
    jb %%lo1
    add al, 'A' - 10
    jmp %%out1
%%lo1:
    add al, '0'
%%out1:
    mov dx, 0x3F8
    out dx, al
    mov al, bl
    and al, 0x0F
    cmp al, 10
    jb %%lo2
    add al, 'A' - 10
    jmp %%out2
%%lo2:
    add al, '0'
%%out2:
    out dx, al
    pop rbx
    pop rdx
    pop rax
%endmacro

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
    SERIAL_CHAR 'I'
    SERIAL_HEX %1
    SERIAL_CHAR 0x0A

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

global isr_default
isr_default:
    SERIAL_CHAR 'I'
    SERIAL_HEX 0xFF
    SERIAL_CHAR 0x0A

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

ISR_NOERRCODE 32
ISR_NOERRCODE 33