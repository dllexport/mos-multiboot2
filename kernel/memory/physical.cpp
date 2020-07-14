#include "physical.h"
#include <std/kstring.h>
#include <std/printk.h>
#include <std/debug.h>
#include <std/new.h>
#include "flags.h"
#include "zone.h"

void PhysicalMemory::Add(multiboot_mmap_entry *mmap)
{
    uint64_t start = PAGE_4K_ALIGN(mmap->addr);
    uint64_t end = ((mmap->addr + mmap->len) >> PAGE_4K_SHIFT) << PAGE_4K_SHIFT;
    printk("zone start: %p end: %p\n", start, end);
    if (start == 0x0) return;
    if (end <= start)
    {
        printk("mmap end <= start, drop\n");
        return;
    }
    this->zones[this->zones_count] = new ((void*)this->ZONE_VIRTUAL_START) Zone(mmap);
    this->zones_count += 1;
}
