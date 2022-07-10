#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>


#define log(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)

#define DIE(fmt, ...) do { \
    log("%s: " fmt, argv[0], ##__VA_ARGS__); \
    return EXIT_FAILURE; } while (0)

#define DIE_WITH_USAGE(fmt, ...) do { \
    log("%s: " fmt, argv[0], ##__VA_ARGS__); \
    log("Usage: %s [-n virt_dev_name] /dev/input/device-to-wrap [virt_btn=real_btn]...", argv[0]); \
    return EXIT_FAILURE; } while (0)

#define DIE_ON_ERROR(expr, fmt, ...) do { \
    int last_error = expr; \
    if (last_error != 0) { \
        log("%s: " fmt " (error %d)", argv[0], ##__VA_ARGS__, last_error); \
        return last_error; }} while (0)


static char const **add_arg(size_t *, char const ***);


struct mapping
{
    unsigned short type;
    unsigned short real_code;
    unsigned short virt_code;
};

static struct mapping *add_mapping(size_t *, struct mapping **);
static int parse_mapping(char const *, struct mapping *);
static void log_mapping(struct mapping const *);


static void on_fatal_signal(int);
static void global_cleanup(void);


// For axis events (type EV_ABS), the kernel does some preprocessing
// which changes the events sent to userspace.
// When we create an axis on the virtual device, it goes through all
// the same preprocessing. To avoid applying the same processing twice,
// we need to change some parameters on the corresponding axis on the
// real device. We need to save what these parameters were,
// so we can restore them when we exit.

struct old_absinfo
{
    struct libevdev *device;
    unsigned short code;
    struct input_absinfo info;
};

static struct old_absinfo *add_old_absinfo(size_t *, struct old_absinfo **);

static size_t n_absinfo_to_restore = 0;
static struct old_absinfo *absinfo_to_restore = NULL;




int
main(int argc, char **argv)
{
    // Disable handled signals temporarily //
    // These signals are inconvenient to handle during initialization.
    // We will re-enable them before starting the event loop.

    sigset_t handled_signals;
    sigset_t old_signal_mask;
    sigemptyset(&handled_signals);
    sigaddset(&handled_signals, SIGINT);
    sigaddset(&handled_signals, SIGTERM);
    sigaddset(&handled_signals, SIGHUP);
    DIE_ON_ERROR(sigprocmask(SIG_BLOCK, &handled_signals, &old_signal_mask), "Failed to set signal mask.");


    // Set up signal and exit handlers //

    signal(SIGINT, &on_fatal_signal);
    signal(SIGTERM, &on_fatal_signal);
    signal(SIGHUP, &on_fatal_signal);
    atexit(&global_cleanup);


    // Parse arguments //

    opterr = 0;

    char const *real_dev_path = "";
    char const *virt_dev_name = "An Unnammed Virtual Device";

    char const **mapping_args = NULL;
    size_t n_mapping_args = 0;

    int opt;
    while ((opt = getopt(argc, argv, "hn:")) != -1)
    {
        switch (opt)
        {
        case 'n':
            virt_dev_name = optarg;
            break;
        case 'h':
            DIE_WITH_USAGE("Called with option '-h'. Printing help.");
        default:
            DIE_WITH_USAGE("Unrecognized option '-%c'.", optopt);
        }
    }
    if (optind >= argc) DIE_WITH_USAGE("Real device path was not specified.");
    real_dev_path = argv[optind++];
    
    for (int i = optind; i < argc; i++)
    {
        *add_arg(&n_mapping_args, &mapping_args) = argv[i];
    }


    // Parse mappings //

    struct mapping *mapped = NULL;
    size_t n_mapped = 0;

    for (int i = 0; i < n_mapping_args; i++)
    {
        struct mapping *added = add_mapping(&n_mapped, &mapped);
        if (parse_mapping(mapping_args[i], added) != 0)
        {
            DIE_WITH_USAGE("Could not parse mapping '%s'.", mapping_args[i]);
        }
        log_mapping(added);
    }


    // Initialize wrapped device //

    int real_dev_fd = open(real_dev_path, O_RDONLY | O_NONBLOCK);
    if (real_dev_fd < 0) DIE_ON_ERROR(real_dev_fd, "Failed to open real device.");
    struct libevdev *real_dev;
    DIE_ON_ERROR(libevdev_new_from_fd(real_dev_fd, &real_dev), "Failed to initialize real device after opening.");
    DIE_ON_ERROR(libevdev_grab(real_dev, LIBEVDEV_GRAB), "Failed to grab real device after opening.");
    log("Grabbed real device '%s'.", real_dev_path);


    // Initialize uinput device //

    struct libevdev *virt_dev_proto = libevdev_new();
    libevdev_set_name(virt_dev_proto, virt_dev_name);
    
    for (int i = 0; i < n_mapped; i++)
    {
        struct input_absinfo const *absinfo = libevdev_get_abs_info(real_dev, mapped[i].real_code);
        libevdev_enable_event_type(virt_dev_proto, mapped[i].type);
        libevdev_enable_event_code(virt_dev_proto, mapped[i].type, mapped[i].virt_code, absinfo);

        if (mapped[i].type == EV_ABS)
        {
            // Store some information about this axis of the wrapped device,
            // then modify this axis on the wrapped device so it behaves correctly on the virtual device.
            // The stored information will be used to undo changes to the wrapped device on exit.

            struct old_absinfo *restore = add_old_absinfo(&n_absinfo_to_restore, &absinfo_to_restore);
            restore->device = real_dev;
            restore->code = mapped[i].real_code;
            restore->info = *absinfo;

            libevdev_set_abs_fuzz(real_dev, mapped[i].real_code, 0);
            libevdev_set_abs_flat(real_dev, mapped[i].real_code, 0);
            struct input_absinfo const *modified_absinfo = libevdev_get_abs_info(real_dev, mapped[i].real_code);
            libevdev_kernel_set_abs_info(real_dev, mapped[i].real_code, modified_absinfo);
        }
    }

    struct libevdev_uinput *virt_dev;
    DIE_ON_ERROR(libevdev_uinput_create_from_device(
        virt_dev_proto, LIBEVDEV_UINPUT_OPEN_MANAGED, &virt_dev), "Failed to create uinput device.");
    log("Created uinput device '%s'.", libevdev_uinput_get_devnode(virt_dev));


    // Initialization is done. It's safe to receive signals now //

    DIE_ON_ERROR(sigprocmask(SIG_SETMASK, &old_signal_mask, NULL), "Failed to restore signal mask.");


    // Process events //
    
    log("Listening for events...");
    for (;;)
    {
        if (libevdev_has_event_pending(real_dev))
        {
            struct input_event e;
            int read_status = libevdev_next_event(real_dev, LIBEVDEV_READ_FLAG_NORMAL, &e);
            if (read_status < 0) DIE_ON_ERROR(read_status, "Error reading event from real device.");

            if (e.type == EV_SYN) libevdev_uinput_write_event(virt_dev, e.type, e.code, e.value);
            else for (int i = 0; i < n_mapped; i++)
            {
                if (e.type == mapped[i].type && e.code == mapped[i].real_code)
                {
                    libevdev_uinput_write_event(virt_dev, mapped[i].type, mapped[i].virt_code, e.value);
                }
            }
        }
    }
}



#define ADD(n, items) \
    *n += 1; \
    *items = reallocarray(*items, *n, sizeof(**items)); \
    return &(*items)[*n - 1]

static char const **
add_arg(size_t *n, char const ***args) { ADD(n, args); }

static struct mapping *
add_mapping(size_t *n, struct mapping **mapped) { ADD(n, mapped); }

static struct old_absinfo *
add_old_absinfo(size_t *n, struct old_absinfo **items) { ADD(n, items); }


static int
parse_mapping(char const *text, struct mapping *mapping)
{
    int status = 0;
    char *code_name = calloc(strlen(text), 1);
    unsigned short real_code;
    char discard;
    int n_parsed = sscanf(text, "%[a-zA-Z-0-9_]=%hi%c", code_name, &real_code, &discard);
    int type = libevdev_event_type_from_code_name(code_name);
    int virt_code = libevdev_event_code_from_code_name(code_name);
    
    if (n_parsed != 2 || type < 0 || virt_code < 0)
    {
        status = -1;
        goto cleanup;
    }

    mapping->type = type;
    mapping->real_code = real_code;
    mapping->virt_code = virt_code;
    
cleanup:
    free(code_name);
    return status;
}


static void
log_mapping(struct mapping const *mapping)
{
    log(
        "Added event mapping type: %6s (%#4hd) code: %12s (%#4hd) from: %12s (%#4hd) on the real device.",
        libevdev_event_type_get_name(mapping->type),
        mapping->type,
        libevdev_event_code_get_name(mapping->type, mapping->virt_code),
        mapping->virt_code,
        libevdev_event_code_get_name(mapping->type, mapping->real_code),
        mapping->real_code);
}


// from "Handlers that Terminate the Process" in the GNU libc manual.
static void
on_fatal_signal(int sig)
{
    static volatile sig_atomic_t fatal_error_in_progress = 0;
    if (fatal_error_in_progress) raise(sig);
    fatal_error_in_progress = 1;

    log("Received signal %d", sig);

    global_cleanup();

    signal(sig, SIG_DFL);
    raise(sig);
}


static void
global_cleanup(void)
{
    for (int i = 0; i < n_absinfo_to_restore; i++)
    {
        struct old_absinfo *restore = &absinfo_to_restore[i];
        int last_error = libevdev_kernel_set_abs_info(
            restore->device,
            restore->code,
            &restore->info);
        if (last_error != 0) log("Failed to restore axis settings for axis %hd", restore->code);
    }
}
