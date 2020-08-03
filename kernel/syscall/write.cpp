#include <std/stdint.h>
#include <std/printk.h>

extern "C" ssize_t sys_write(int fd, void *buf, size_t count) {
    int i = 0;
    while (i < count) {
        Kernel::VGA::console_putc_color(((int8_t*)buf)[i++]);
    }
    return 0;
}