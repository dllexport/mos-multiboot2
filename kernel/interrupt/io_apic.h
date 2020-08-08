#pragma once

#include <std/stdint.h>
#include <std/singleton.h>
#include <memory/heap.h>
#include <memory/mapping.h>
#include <memory/physical_page.h>

#define IOAPICID 0x00
#define IOAPICVER 0x01
#define IOAPICARB 0x02
#define IOAPICREDTBL(n) (0x10 + 2 * n) // lower-32bits (add +1 for upper 32-bits)

static void write_ioapic_register(const uint64_t apic_base, const uint8_t offset, const uint32_t val)
{
        /* tell IOREGSEL where we want to write to */
        *(volatile uint32_t *)(apic_base) = offset;
        /* write the value to IOWIN */
        *(volatile uint32_t *)(apic_base + 0x10) = val;
}

static uint32_t read_ioapic_register(const uint64_t apic_base, const uint8_t offset)
{
        /* tell IOREGSEL where we want to read from */
        *(volatile uint32_t *)(apic_base) = offset;
        /* return the data from IOWIN */
        return *(volatile uint32_t *)(apic_base + 0x10);
}

/* @class IOAPIC
 *
 * A sample driver code which control an IOAPIC. It handles one IOAPIC and exposes
 * some functions. It is totally representational, .i.e you should add locking support
 * link all IOAPIC classes in a data structure and much more. Here we are just showing
 * what & how your'e gonna handle this in C++.
 *
 * You could also note that IOAPIC registers "may" cross a page boundary. So maybe you
 * may need to map the physical-base to a double-page (means allocate twice the amount
 * of memory from vmm).
 */
class IOAPIC : public Singleton<IOAPIC>
{
public:
        enum DeliveryMode
        {
                EDGE = 0,
                LEVEL = 1,
        };

        enum DestinationMode
        {
                PHYSICAL = 0,
                LOGICAL = 1
        };

        union RedirectionEntry {
                struct
                {
                        uint64_t vector : 8;
                        uint64_t delvMode : 3;
                        uint64_t destMode : 1;
                        uint64_t delvStatus : 1;
                        uint64_t pinPolarity : 1;
                        uint64_t remoteIRR : 1;
                        uint64_t triggerMode : 1;
                        uint64_t mask : 1;
                        uint64_t reserved : 39;
                        uint64_t destination : 8;
                };
                struct
                {
                        uint32_t lowerDword;
                        uint32_t upperDword;
                };
        };

        uint8_t id() { return (apicId); }
        uint8_t ver() { return (apicVer); }
        uint8_t redirectionEntries() { return (redirEntryCnt); }
        uint64_t globalInterruptBase() { return (globalIntrBase); }

        void Init(uint64_t io_apic_paddr, uint64_t apicId, uint64_t gsib)
        {
                this->physRegs = io_apic_paddr;
                this->virtAddr = (uint64_t)Phy_To_Virt(io_apic_paddr);

                apicId = (read(IOAPICID) >> 24) & 0xF0;
                apicVer = read(IOAPICVER); // cast to uint8_t (uint8_t) hides upper bits

                //< max. redir entry is given IOAPICVER[16:24]
                redirEntryCnt = (read(IOAPICVER) >> 16) + 1; // cast to uint8_t occuring ok!
                globalIntrBase = gsib;

                
                RedirectionEntry keyboard = {0};
                keyboard.vector = 33;
                writeRedirEntry(1, &keyboard);
        }

        /*
         * Bit of assignment here - implement this on your own. Use the lowerDword & upperDword
         * fields of RedirectionEntry using
         *                                 ent.lowerDword = read(entNo);
         *                                 ent.upperDword = read(entNo);
         *                                 return (ent);
         *
         * Be sure to check that entNo < redirectionEntries()
         *
         * @param entNo - entry no. for which redir-entry is required
         * @return entry associated with entry no.
         */
        RedirectionEntry getRedirEntry(uint8_t entNo)
        {
                if (entNo >= redirectionEntries())
                        panic("entNo < redirectionEntries");
                auto low = read(entNo);
                auto high = read(entNo + 1);
                auto full = (high << 32) | low;
                return *(RedirectionEntry*)&full;
        }

        /*
         * Bit of assignment here - implement this on your own. Use the lowerDword & upperDword
         * fields of RedirectionEntry using
         *                               write(entNo, ent->lowerDword);
         *                               write(entNo, ent->upperDword);
         *
         * Be sure to check that entNo < redirectionEntries()
         *
         * @param entNo - entry no. for which redir-entry is required
         * @param entry - ptr to entry to write
         */
        void writeRedirEntry(uint8_t entNo, RedirectionEntry *entry)
        {
                if (entNo >= redirectionEntries())
                        panic("entNo < redirectionEntries");
                        
                write(0x10 + entNo * 2, entry->lowerDword);
                write(0x10 + entNo * 2 + 1, entry->upperDword);
        }

        void EOI()
        {
                *(uint32_t volatile *)(virtAddr + 0x40) = 0;
        }

private:
        /*
         * This field contains the physical-base address for the IOAPIC
         * can be found using an IOAPIC-entry in the ACPI 2.0 MADT.
         */
        uint64_t physRegs;

        /*
         * Holds the base address of the registers in virtual memory. This
         * address is non-cacheable (see paging).
         */
        uint64_t virtAddr;

        /*
         * Software has complete control over the apic-id. Also, hardware
         * won't automatically change its apic-id so we could cache it here.
         */
        uint8_t apicId;

        /*
         * Hardware-version of the apic, mainly for display purpose. ToDo: specify
         * more purposes.
         */
        uint8_t apicVer;

        /*
         * Although entries for current IOAPIC is 24, it may change. To retain
         * compatibility make sure you use this.
         */
        uint8_t redirEntryCnt;

        /*
         * The first IRQ which this IOAPIC handles. This is only found in the
         * IOAPIC entry of the ACPI 2.0 MADT. It isn't found in the IOAPIC
         * registers.
         */
        uint64_t globalIntrBase;

        /*
         * Reads the data present in the register at offset regOff.
         *
         * @param regOff - the register's offset which is being read
         * @return the data present in the register associated with that offset
         */
        uint32_t read(uint8_t regOff)
        {
                *(uint32_t volatile *)virtAddr = regOff;
                return *(uint32_t volatile *)(virtAddr + 0x10);
        }

        /*
         * Writes the data into the register associated. 
         *
         * @param regOff - the register's offset which is being written
         * @param data - dword to write to the register
         */
        void write(uint8_t regOff, uint32_t data)
        {
                *(uint32_t volatile *)virtAddr = regOff;
                *(uint32_t volatile *)(virtAddr + 0x10) = data;
        }
};