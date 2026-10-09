// SPDX-License-Identifier: BSD-2-Clause-Patent
// Copyright (c) 2026 Thinking Developer
//
// PodumatOS — a hobby operating system for x86_64.

#ifndef XHCI_HW_HPP
#define XHCI_HW_HPP

#include <cstdint>

namespace xhci {

//  Capability Registers (offset from MMIO base)
constexpr uint32_t CAP_CAPLENGTH   = 0x00;
constexpr uint32_t CAP_HCIVERSION  = 0x02;
constexpr uint32_t CAP_HCSPARAMS1  = 0x04;
constexpr uint32_t CAP_HCSPARAMS2  = 0x08;
constexpr uint32_t CAP_HCSPARAMS3  = 0x0C;
constexpr uint32_t CAP_HCCPARAMS1  = 0x10;
constexpr uint32_t CAP_DBOFF       = 0x14;
constexpr uint32_t CAP_RTSOFF      = 0x18;
constexpr uint32_t CAP_HCCPARAMS2  = 0x1C;

inline uint32_t HCS1_MAX_SLOTS (uint32_t v) { return  v         & 0xFF;   }
inline uint32_t HCS1_MAX_INTRS (uint32_t v) { return (v >>  8)  & 0x7FF;  }
inline uint32_t HCS1_MAX_PORTS (uint32_t v) { return (v >> 24)  & 0xFF;   }

inline uint32_t HCS2_ERST_MAX  (uint32_t v) { return (v >>  4)  & 0xF;    }
inline uint32_t HCS2_MAX_SP    (uint32_t v) { return ((((v >> 21) & 0x1F) << 5) | ((v >> 27) & 0x1F)); }

inline uint32_t HCC1_AC64      (uint32_t v) { return  v         & 0x1;    }
inline uint32_t HCC1_CSZ       (uint32_t v) { return (v >>  2)  & 0x1;    }
inline uint32_t HCC1_XECP      (uint32_t v) { return (v >> 16)  & 0xFFFF; }

//  Operational Registers (offset from op_base)
constexpr uint32_t OP_USBCMD   = 0x00;
constexpr uint32_t OP_USBSTS   = 0x04;
constexpr uint32_t OP_PAGESIZE = 0x08;
constexpr uint32_t OP_DNCTRL   = 0x14;
constexpr uint32_t OP_CRCR     = 0x18;
constexpr uint32_t OP_DCBAAP   = 0x30;
constexpr uint32_t OP_CONFIG   = 0x38;
constexpr uint32_t OP_PORTS    = 0x400;

constexpr uint32_t CMD_RUN     = 1u << 0;
constexpr uint32_t CMD_HCRST   = 1u << 1;
constexpr uint32_t CMD_INTE    = 1u << 2;
constexpr uint32_t CMD_HSEE    = 1u << 3;

constexpr uint32_t STS_HCH     = 1u << 0;
constexpr uint32_t STS_HSE     = 1u << 2;
constexpr uint32_t STS_EINT    = 1u << 3;
constexpr uint32_t STS_PCD     = 1u << 4;
constexpr uint32_t STS_CNR     = 1u << 11;
constexpr uint32_t STS_HCE     = 1u << 12;

constexpr uint64_t CRCR_RCS    = 1ull << 0;
constexpr uint64_t CRCR_CS     = 1ull << 1;
constexpr uint64_t CRCR_CA     = 1ull << 2;
constexpr uint64_t CRCR_CRR    = 1ull << 3;

//  Port Register Set (offset from PORTSC base, stride 0x10)
constexpr uint32_t PORTSC      = 0x00;
constexpr uint32_t PORTPMSC    = 0x04;
constexpr uint32_t PORTLI      = 0x08;

constexpr uint32_t PS_CCS      = 1u << 0;
constexpr uint32_t PS_PED      = 1u << 1;
constexpr uint32_t PS_OCA      = 1u << 3;
constexpr uint32_t PS_PR       = 1u << 4;
constexpr uint32_t PS_PLS_MASK = 0xFu << 5;
constexpr uint32_t PS_PP       = 1u << 9;
constexpr uint32_t PS_SPEED_SHIFT = 10;
constexpr uint32_t PS_SPEED_MASK  = 0xFu << 10;
constexpr uint32_t PS_LWS      = 1u << 16;
constexpr uint32_t PS_CSC      = 1u << 17;
constexpr uint32_t PS_PEC      = 1u << 18;
constexpr uint32_t PS_WRC      = 1u << 19;
constexpr uint32_t PS_OCC      = 1u << 20;
constexpr uint32_t PS_PRC      = 1u << 21;
constexpr uint32_t PS_PLC      = 1u << 22;
constexpr uint32_t PS_CEC      = 1u << 23;
constexpr uint32_t PS_WPR      = 1u << 30;

constexpr uint32_t PS_CHANGE_BITS = PS_CSC | PS_PEC | PS_WRC | PS_OCC | PS_PRC | PS_PLC | PS_CEC;
constexpr uint32_t PS_W1C_BITS    = PS_PED | PS_CSC | PS_PEC | PS_WRC | PS_OCC | PS_PRC | PS_PLC | PS_CEC;

constexpr uint32_t PLS_U0         = 0;
constexpr uint32_t PLS_U3         = 3;
constexpr uint32_t PLS_DISABLED   = 4;
constexpr uint32_t PLS_RXDETECT   = 5;
constexpr uint32_t PLS_POLLING    = 7;
constexpr uint32_t PLS_RECOVERY   = 8;
constexpr uint32_t PLS_COMPLIANCE = 10;

//  Runtime Registers
constexpr uint32_t RT_MFINDEX = 0x00;
constexpr uint32_t RT_IR0     = 0x20;
constexpr uint32_t RT_IRSIZE  = 0x20;

constexpr uint32_t IR_IMAN    = 0x00;
constexpr uint32_t IR_IMOD    = 0x04;
constexpr uint32_t IR_ERSTSZ  = 0x08;
constexpr uint32_t IR_ERSTBA  = 0x10;
constexpr uint32_t IR_ERDP    = 0x18;

constexpr uint32_t IMAN_IP    = 1u << 0;
constexpr uint32_t IMAN_IE    = 1u << 1;
constexpr uint64_t ERDP_EHB   = 1ull << 3;

//  TRB Types
constexpr uint32_t TRB_NORMAL       = 1;
constexpr uint32_t TRB_SETUP        = 2;
constexpr uint32_t TRB_DATA         = 3;
constexpr uint32_t TRB_STATUS       = 4;
constexpr uint32_t TRB_ISOCH        = 5;
constexpr uint32_t TRB_LINK         = 6;
constexpr uint32_t TRB_EVENT_DATA   = 7;
constexpr uint32_t TRB_TR_NOOP      = 8;
constexpr uint32_t TRB_ENABLE_SLOT  = 9;
constexpr uint32_t TRB_DISABLE_SLOT = 10;
constexpr uint32_t TRB_ADDRESS_DEV  = 11;
constexpr uint32_t TRB_CONFIG_EP    = 12;
constexpr uint32_t TRB_EVAL_CTX     = 13;
constexpr uint32_t TRB_RESET_EP     = 14;
constexpr uint32_t TRB_STOP_EP      = 15;
constexpr uint32_t TRB_SET_TR_DEQ   = 16;
constexpr uint32_t TRB_RESET_DEV    = 17;
constexpr uint32_t TRB_NOOP_CMD     = 23;
constexpr uint32_t TRB_TRANSFER     = 32;
constexpr uint32_t TRB_CMD_COMPLETE = 33;
constexpr uint32_t TRB_PORT_STATUS  = 34;
constexpr uint32_t TRB_HC_EVENT     = 37;

//  TRB Flag Bits
constexpr uint32_t TRB_CYCLE     = 1u << 0;
constexpr uint32_t TRB_TC        = 1u << 1;   // Link: Toggle Cycle
constexpr uint32_t TRB_ENT       = 1u << 1;   // Event Data: Evaluate Next TRB
constexpr uint32_t TRB_ISP       = 1u << 2;
constexpr uint32_t TRB_NS        = 1u << 3;
constexpr uint32_t TRB_CHAIN     = 1u << 4;
constexpr uint32_t TRB_IOC       = 1u << 5;
constexpr uint32_t TRB_IDT       = 1u << 6;
constexpr uint32_t TRB_BEI       = 1u << 9;
constexpr uint32_t TRB_BSR       = 1u << 9;   // Address Device: Block Set Address
constexpr uint32_t TRB_DC        = 1u << 9;   // Configure EP: Deconfigure
constexpr uint32_t TRB_DIR_IN    = 1u << 16;
constexpr uint32_t TRB_TRT_MASK  = 3u << 16;
constexpr uint32_t TRB_TRT_OUT   = 2u << 16;
constexpr uint32_t TRB_TRT_IN    = 3u << 16;

inline uint32_t TRB_TYPE(uint32_t t)       { return (t & 0x3F) << 10; }
inline uint32_t TRB_TYPE_GET(uint32_t f)   { return (f >> 10) & 0x3F; }
inline uint32_t TRB_SLOT(uint32_t s)       { return (s & 0xFF) << 24; }
inline uint32_t TRB_SLOT_GET(uint32_t f)   { return (f >> 24) & 0xFF; }
inline uint32_t TRB_EP(uint32_t e)         { return (e & 0x1F) << 16; }
inline uint32_t TRB_EP_GET(uint32_t f)     { return (f >> 16) & 0x1F; }
inline uint32_t TRB_LEN(uint32_t n)        { return n & 0x1FFFF; }
inline uint32_t TRB_LEN_GET(uint32_t s)    { return s & 0x1FFFF; }
inline uint32_t TRB_TD_SIZE(uint32_t n)    { return (n & 0x1F) << 17; }
inline uint32_t TRB_IRQ(uint32_t n)        { return (n & 0x3FF) << 22; }
inline uint32_t TRB_COMP_GET(uint32_t s)   { return (s >> 24) & 0xFF; }
inline uint32_t TRB_REM_GET(uint32_t s)    { return s & 0xFFFFFF; }

//  Completion Codes
constexpr uint32_t COMP_INVALID           = 0;
constexpr uint32_t COMP_SUCCESS           = 1;
constexpr uint32_t COMP_DATA_BUFFER_ERR   = 2;
constexpr uint32_t COMP_BABBLE            = 3;
constexpr uint32_t COMP_USB_TRANSACTION   = 4;
constexpr uint32_t COMP_TRB_ERR           = 5;
constexpr uint32_t COMP_STALL             = 6;
constexpr uint32_t COMP_RESOURCE          = 7;
constexpr uint32_t COMP_BANDWIDTH         = 8;
constexpr uint32_t COMP_NO_SLOTS          = 9;
constexpr uint32_t COMP_INVALID_STREAM    = 10;
constexpr uint32_t COMP_SLOT_NOT_ENABLED  = 11;
constexpr uint32_t COMP_EP_NOT_ENABLED    = 12;
constexpr uint32_t COMP_SHORT_PACKET      = 13;
constexpr uint32_t COMP_RING_UNDERRUN     = 14;
constexpr uint32_t COMP_RING_OVERRUN      = 15;
constexpr uint32_t COMP_VF_RING_FULL      = 16;
constexpr uint32_t COMP_PARAMETER         = 17;
constexpr uint32_t COMP_BANDWIDTH_OVERRUN = 18;
constexpr uint32_t COMP_CONTEXT_STATE     = 19;
constexpr uint32_t COMP_NO_PING           = 20;
constexpr uint32_t COMP_EVENT_RING_FULL   = 21;
constexpr uint32_t COMP_INCOMPATIBLE_DEV  = 22;
constexpr uint32_t COMP_MISSED_SERVICE    = 23;
constexpr uint32_t COMP_CMD_RING_STOPPED  = 24;
constexpr uint32_t COMP_CMD_ABORTED       = 25;
constexpr uint32_t COMP_STOPPED           = 26;
constexpr uint32_t COMP_STOPPED_LEN_INV   = 27;
constexpr uint32_t COMP_STOPPED_SHORT     = 28;


//  TRB structure (16 bytes, aligned)
struct __attribute__((packed, aligned(16))) Trb {
    uint64_t param;
    uint32_t status;
    uint32_t flags;
};

//  Event Ring Segment Table Entry (16 bytes, 64-aligned)
struct __attribute__((packed, aligned(64))) ErstEntry {
    uint64_t rs_addr;
    uint32_t rs_size;
    uint32_t rsvdz;
};

//  Slot Context field bits (dword offsets within 32-byte ctx)
inline uint32_t SLOT0_ROUTE(uint32_t r)     { return r & 0xFFFFF; }
inline uint32_t SLOT0_SPEED(uint32_t s)     { return (s & 0xF) << 20; }
inline uint32_t SLOT0_MTT                    () { return 1u << 25; }
inline uint32_t SLOT0_HUB                    () { return 1u << 26; }
inline uint32_t SLOT0_ENTRIES(uint32_t n)   { return (n & 0x1F) << 27; }
inline uint32_t SLOT0_ENTRIES_GET(uint32_t v){ return (v >> 27) & 0x1F; }

inline uint32_t SLOT1_MAX_EXIT_LAT(uint32_t v) { return v & 0xFFFF; }
inline uint32_t SLOT1_RH_PORT(uint32_t p)      { return (p & 0xFF) << 16; }
inline uint32_t SLOT1_NUM_PORTS(uint32_t n)    { return (n & 0xFF) << 24; }

inline uint32_t SLOT2_TT_SLOT(uint32_t s)      { return s & 0xFF; }
inline uint32_t SLOT2_TT_PORT(uint32_t p)      { return (p & 0xFF) << 8; }
inline uint32_t SLOT2_TT_TIME(uint32_t t)      { return (t & 0x3) << 16; }
inline uint32_t SLOT2_IRQ_TARGET(uint32_t t)   { return (t & 0x7F) << 22; }

inline uint32_t SLOT3_ADDR(uint32_t a)         { return a & 0xFF; }
inline uint32_t SLOT3_ADDR_GET(uint32_t v)     { return v & 0xFF; }
inline uint32_t SLOT3_STATE(uint32_t s)        { return (s & 0x1F) << 27; }
inline uint32_t SLOT3_STATE_GET(uint32_t v)    { return (v >> 27) & 0x1F; }

constexpr uint32_t SLOT_STATE_ENABLED    = 0;
constexpr uint32_t SLOT_STATE_DEFAULT    = 1;
constexpr uint32_t SLOT_STATE_ADDRESSED  = 2;
constexpr uint32_t SLOT_STATE_CONFIGURED = 3;

//  Endpoint Context field bits
inline uint32_t EP0_STATE(uint32_t s)          { return s & 0x7; }
inline uint32_t EP0_STATE_GET(uint32_t v)      { return v & 0x7; }
inline uint32_t EP0_INTERVAL(uint32_t i)       { return (i & 0xFF) << 16; }

inline uint32_t EP1_CERR(uint32_t c)           { return (c & 0x3) << 1; }
inline uint32_t EP1_EPTYPE(uint32_t t)         { return (t & 0x7) << 3; }
inline uint32_t EP1_HID                         () { return 1u << 7; }
inline uint32_t EP1_MAXBURST(uint32_t b)       { return (b & 0xFF) << 8; }
inline uint32_t EP1_MPS(uint32_t m)            { return (m & 0xFFFF) << 16; }

constexpr uint64_t EP2_DCS = 1ull << 0;

inline uint32_t EP4_AVG_TRB(uint32_t a)        { return a & 0xFFFF; }
inline uint32_t EP4_MAX_ESIT(uint32_t e)       { return (e & 0xFFFF) << 16; }

constexpr uint32_t EP_TYPE_ISOC_OUT = 1;
constexpr uint32_t EP_TYPE_BULK_OUT = 2;
constexpr uint32_t EP_TYPE_INT_OUT  = 3;
constexpr uint32_t EP_TYPE_CTRL     = 4;
constexpr uint32_t EP_TYPE_ISOC_IN  = 5;
constexpr uint32_t EP_TYPE_BULK_IN  = 6;
constexpr uint32_t EP_TYPE_INT_IN   = 7;

constexpr uint32_t EP_STATE_DISABLED = 0;
constexpr uint32_t EP_STATE_RUNNING  = 1;
constexpr uint32_t EP_STATE_HALTED   = 2;
constexpr uint32_t EP_STATE_STOPPED  = 3;
constexpr uint32_t EP_STATE_ERROR    = 4;

//  Input Control Context flags
constexpr uint32_t ICTRL_ADD_SLOT = 1u << 0;
constexpr uint32_t ICTRL_ADD_EP0  = 1u << 1;
inline uint32_t ICTRL_ADD_EP(uint32_t n) { return 1u << (n + 1); }

//  Extended Capabilities
inline uint32_t XECP_ID(uint32_t v)    { return v & 0xFF; }
inline uint32_t XECP_NEXT(uint32_t v)  { return (v >> 8) & 0xFF; }
inline uint32_t XECP_PTR(uint32_t hcc) { return ((hcc >> 16) & 0xFFFF) << 2; }

constexpr uint32_t XECP_LEGSUP_ID = 0x01;
constexpr uint32_t XECP_PROTO_ID  = 0x02;

constexpr uint32_t LEGSUP_BIOS_OWNED = 1u << 16;
constexpr uint32_t LEGSUP_OS_OWNED   = 1u << 24;
constexpr uint32_t LEGSUP_LEGCTLSTS_OFF = 0x04;

constexpr uint32_t PROTO_MAJOR_GET(uint32_t v) { return (v >> 24) & 0xFF; }
constexpr uint32_t PROTO_OFFSET_GET(uint32_t v) { return v & 0xFF; }
constexpr uint32_t PROTO_COUNT_GET(uint32_t v)  { return (v >> 8) & 0xFF; }

//  Constants
constexpr uint32_t MAX_SLOTS_HW     = 255;
constexpr uint32_t MAX_INTRS_HW     = 1024;
constexpr uint32_t MAX_PORTS_HW     = 255;
constexpr uint32_t MAX_EP_CTX       = 31;

constexpr uint32_t RING_TRBS        = 256;   // per ring segment
constexpr uint32_t USABLE_TRBS      = RING_TRBS - 1; // last is Link TRB
constexpr uint32_t CTX_SIZE_32      = 32;
constexpr uint32_t CTX_SIZE_64      = 64;

// Timeouts (busy-wait iterations)
constexpr uint32_t TMO_RESET_US     = 1000000;
constexpr uint32_t TMO_CMD_US       = 5000000;
constexpr uint32_t TMO_XFER_US      = 5000000;
constexpr uint32_t TMO_PORT_RESET   = 500000; // us

// Layout helpers for context arrays (CSZ-dependent)
inline uint32_t ctx_off(uint32_t index, uint32_t stride) { return index * stride; }

// Input Context indices
constexpr uint32_t IC_CTRL   = 0;
constexpr uint32_t IC_SLOT   = 1;
constexpr uint32_t IC_EP0    = 2;
inline uint32_t IC_EPn(uint32_t dci) { return dci + 1; } // dci 1 => index 2 (EP0)

// Device Context indices (no input control ctx)
constexpr uint32_t DC_SLOT   = 0;
constexpr uint32_t DC_EP0    = 1;
inline uint32_t DC_EPn(uint32_t dci) { return dci; }

}

#endif
