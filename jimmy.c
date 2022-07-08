#include <stdio.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>


#define DIE_ON_ERROR(x) do { \
    int LAST_ERROR = x; \
    if (LAST_ERROR != 0) { \
        fprintf(stderr, "error %d on line %d: %s\n", LAST_ERROR, __LINE__, #x); \
        return LAST_ERROR; }} while (0)


int main(int argc, char **argv)
{
    int device_fd = open(argv[1], O_RDONLY | O_NONBLOCK);
    if (device_fd < 0) DIE_ON_ERROR(device_fd);
    struct libevdev *device;
    DIE_ON_ERROR(libevdev_new_from_fd(device_fd, &device));
    
    for (;;)
    {
        if (libevdev_has_event_pending(device))
        {
            struct input_event e;
            int read_status = libevdev_next_event(device, LIBEVDEV_READ_FLAG_NORMAL, &e);
            if (read_status < 0) DIE_ON_ERROR(read_status);

            fprintf(
                stderr,
                "type: %6s (%#6hx) code: %12s (%#6hx) value: %10d\n",
                libevdev_event_type_get_name(e.type),
                e.type,
                libevdev_event_code_get_name(e.type, e.code),
                e.code,
                e.value);
        }
    }
}
