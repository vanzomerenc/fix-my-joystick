#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>


#define DIE_ON_ERROR(x) do { \
    int LAST_ERROR = x; \
    if (LAST_ERROR != 0) { \
        fprintf(stderr, "error %d on line %d: %s\n", LAST_ERROR, __LINE__, #x); \
        return EXIT_FAILURE; }} while (0)


struct virtual_event_info
{
    unsigned short type;
    unsigned short physical_code;
    unsigned short virtual_code;
};

static int parse_event_info(char *text, struct virtual_event_info *info)
{
    int status = 0;
    char *code_name = calloc(strlen(text), 1);
    unsigned short physical_code;
    int n_parsed = sscanf(text, "%[a-zA-Z-0-9_]=%hd", code_name, &physical_code);
    int type = libevdev_event_type_from_code_name(code_name);
    int virtual_code = libevdev_event_code_from_code_name(code_name);
    
    if (n_parsed != 2 || type < 0 || virtual_code < 0)
    {
        status = -1;
        goto cleanup;
    }

    info->type = type;
    info->physical_code = physical_code;
    info->virtual_code = virtual_code;
    
cleanup:
    free(code_name);
    return status;
}


int main(int argc, char **argv)
{
    char const *real_dev = "";
    char const *virt_dev = "Virtual_Joystick";

    struct virtual_event_info *virtual_events = NULL;
    size_t n_virtual_events = 0;

    int opt;
    while ((opt = getopt(argc, argv, "n:u:")) != -1)
    {
        switch (opt)
        {
        case 'n':
            virt_dev = optarg;
            break;
        case 'u':
            n_virtual_events += 1;
            virtual_events = realloc(virtual_events, sizeof(struct virtual_event_info) * n_virtual_events);
            struct virtual_event_info *added_event = &virtual_events[n_virtual_events - 1];
            DIE_ON_ERROR(parse_event_info(optarg, added_event));
            fprintf(stderr,
                "Added event type: %6s (%#6hx) code: %12s (%#6hx) from: %12s (%#6hx) on the real device.\n",
                libevdev_event_type_get_name(added_event->type),
                added_event->type,
                libevdev_event_code_get_name(added_event->type, added_event->virtual_code),
                added_event->virtual_code,
                libevdev_event_code_get_name(added_event->type, added_event->physical_code),
                added_event->physical_code);
            break;
        default:
            fprintf(stderr, "Usage: %s [-n virt_dev] [-u virt_btn=real_btn] real_dev\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    real_dev = argv[optind];



    int device_fd = open(real_dev, O_RDONLY | O_NONBLOCK);
    if (device_fd < 0) DIE_ON_ERROR(device_fd);
    struct libevdev *device;
    DIE_ON_ERROR(libevdev_new_from_fd(device_fd, &device));
    DIE_ON_ERROR(libevdev_grab(device, LIBEVDEV_GRAB));

    struct libevdev *virtual_device_prototype = libevdev_new();
    libevdev_set_name(virtual_device_prototype, virt_dev);
    
    for (int i = 0; i < n_virtual_events; i++)
    {
        struct virtual_event_info const *info = &virtual_events[i];
        struct input_absinfo const *absinfo = NULL;
        if (info->type == EV_ABS)
        {
            absinfo = libevdev_get_abs_info(device, info->physical_code);
        }

        libevdev_enable_event_type(virtual_device_prototype, info->type);
        libevdev_enable_event_code(virtual_device_prototype, info->type, info->virtual_code, absinfo);
    }

    struct libevdev_uinput *virtual_device;
    DIE_ON_ERROR(libevdev_uinput_create_from_device(
        virtual_device_prototype,
        LIBEVDEV_UINPUT_OPEN_MANAGED,
        &virtual_device));
    
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

            if (e.type == EV_SYN) libevdev_uinput_write_event(virtual_device, e.type, e.code, e.value);
            for (int i = 0; i < n_virtual_events; i++)
            {
                struct virtual_event_info const *info = &virtual_events[i];
                if (e.type == info->type && e.code == info->physical_code)
                {
                    libevdev_uinput_write_event(virtual_device, info->type, info->virtual_code, e.value);
                }
            }
        }
    }
}
