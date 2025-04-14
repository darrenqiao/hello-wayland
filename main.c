#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <linux/input-event-codes.h>

#include "cat.h"
#include "shm.h"
#include "xdg-shell-client-protocol.h"

static const int width = 128;
static const int height = 128;

static bool configured = false;
static bool running = true;

static struct wl_shm *shm = NULL;
static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *xdg_wm_base = NULL;
static struct wl_data_device_manager *data_device_manager = NULL;
static struct wl_data_device *data_device = NULL;
static struct wl_seat *seata = NULL;

static void *shm_data = NULL;
//Wayland 中的基本绘图单元，表示一个可以显示在屏幕上的矩形区域
static struct wl_surface *surface = NULL;
static struct xdg_toplevel *xdg_toplevel = NULL;

static void noop() {
	// This space intentionally left blank
}

static void xdg_wm_base_handle_ping(void *data,
		struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
	// The compositor will send us a ping event to check that we're responsive.
	// We need to send back a pong request immediately.
	xdg_wm_base_pong(xdg_wm_base, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	.ping = xdg_wm_base_handle_ping,
};

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	printf("ack configure: %d\n", serial);
	// The compositor configures our surface, acknowledge the configure event
	xdg_surface_ack_configure(xdg_surface, serial);

	if (configured) {
		// If this isn't the first configure event we've received, we already
		// have a buffer attached, so no need to do anything. Commit the
		// surface to apply the configure acknowledgement.
		wl_surface_commit(surface);
	}

	configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_close(void *data,
		struct xdg_toplevel *xdg_toplevel) {
	// Stop running if the user requests to close the toplevel
	running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = noop,
	.close = xdg_toplevel_handle_close,
};

static void pointer_handle_button(void *data, struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct wl_seat *seat = data;

	printf("mouse button: %d %d %d\n", button, state, serial);
	// If the user presses the left pointer button, start an interactive move
	// of the toplevel
	if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		printf("mouse button pressed: %d %d %d, moving xdg_toplevel\n", button, state, serial);
		xdg_toplevel_move(xdg_toplevel, seat, serial);
	}
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer,
	uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	printf("mouse move man: (%f, %f)\n",
	wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y));
}

/*
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,           // 可选 用于将多个事件组合为一个原子操作。
    .axis_source = pointer_handle_axis_source, // 可选 指定滚动事件的来源（如鼠标滚轮、触摸板等）。
    .axis_stop = pointer_handle_axis_stop,   // 可选 当滚动事件停止时触发。
    .axis_discrete = pointer_handle_axis_discrete // 可选 提供滚动事件的离散值（如鼠标滚轮的刻度）。
	*/
static const struct wl_pointer_listener pointer_listener = {
	.enter = noop,
	.leave = noop,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = noop,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
		uint32_t capabilities) {
	// If the wl_seat has the pointer capability, start listening to pointer
	// events
	// wl_seat_get_pointer() 函数用于获取与指定的 wl_seat 相关联的指针设备（如鼠标）的句柄。
	// wl_seat_get_keyboard() 函数用于获取与指定的 wl_seat 相关联的键盘设备的句柄。
	// wl_seat_get_touch() 函数用于获取与指定的 wl_seat 相关联的触摸设备的句柄。
	// wl_seat_get_focused() 函数用于获取当前焦点设备的句柄。
	// wl_seat_get_focused_surface() 函数用于获取当前焦点表面的句柄。
	// wl_seat_get_keyboard() 函数用于获取与指定的 wl_seat 相关联的键盘设备的句柄。
	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		//获取鼠标的句柄 并添加监听器
		// wl_pointer_add_listener() 函数用于将一个监听器添加到指定的 wl_pointer 对象上。
		// 这个监听器会在 wl_pointer 对象上发生事件时被调用。
		// 例如，当鼠标移动、按下或释放按钮时，wl_pointer 对象会触发相应的事件。
		// 通过 wl_pointer_add_listener() 函数将 pointer_listener 添加到 wl_pointer 对象上。
		// 这样，当 wl_pointer 对象上发生事件时，pointer_listener 中定义的回调函数就会被调用。
		// 通过 wl_pointer_get_position() 函数可以获取当前鼠标指针的位置。
		// 通过 wl_pointer_get_button() 函数可以获取当前鼠标按下的按钮。
		// 通过 wl_pointer_get_axis() 函数可以获取当前鼠标滚轮的滚动方向。
		// 通过 wl_pointer_get_focus() 函数可以获取当前鼠标焦点所在的表面。
		// 通过 wl_pointer_get_surface() 函数可以获取当前鼠标焦点所在的表面。
		// 通过 wl_pointer_get_focused() 函数可以获取当前鼠标焦点所在的表面。
		// 通过 wl_pointer_get_focused_surface() 函数可以获取当前鼠标焦点所在的表面。
		struct wl_pointer *pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(pointer, &pointer_listener, seat);
	}
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
};

struct data_offer_data {
    struct wl_data_offer *offer; // Pointer to the wl_data_offer
    char **mime_types;           // Array of MIME types
    int mime_type_count;         // Number of MIME types
};

static void data_offer_handle_offer(void *data, struct wl_data_offer *offer,
	const char *mime_type) {
struct data_offer_data *offer_data = data;

// Store the MIME type in the list
offer_data->mime_types = realloc(offer_data->mime_types,
	 sizeof(char *) * (offer_data->mime_type_count + 1));
offer_data->mime_types[offer_data->mime_type_count] = strdup(mime_type);
offer_data->mime_type_count++;

printf("Received MIME type: %s\n", mime_type);
}

static const struct wl_data_offer_listener data_offer_listener = {
.offer = data_offer_handle_offer,
};

static void handle_data_offer(void *data, struct wl_data_device *data_device,
	struct wl_data_offer *offer) {
	printf("New data offer received\n");
    // 查询并打印可用的 MIME 类型
    // Initialize the data_offer_data structure
    struct data_offer_data *offer_data = calloc(1, sizeof(struct data_offer_data));
    offer_data->offer = offer;
    offer_data->mime_types = NULL;
    offer_data->mime_type_count = 0;

    // Add the listener to the wl_data_offer
    wl_data_offer_add_listener(offer, &data_offer_listener, offer_data);
}

static void handle_selection(void *data, struct wl_data_device *data_device,
   struct wl_data_offer *offer) {
    if (offer) {
        printf("New clipboard selection available\n");
        int fd = open("/tmp/clipboard", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) {
            perror("Failed to open clipboard file");
            return;
        }
        wl_data_offer_receive(offer, "text/plain", fd);
        close(fd);
    } else {
        printf("Clipboard cleared\n");
    }
}

static const struct wl_data_device_listener data_device_listener = {
.data_offer = handle_data_offer,//当有新的数据传输（如剪贴板或拖放操作）可用时触发。表示有一个新的数据提供者（wl_data_offer）可用。
.selection = handle_selection,//当剪贴板内容发生变化时触发。
};


//按需绑定接口 供后续使用
static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		seata =
			wl_registry_bind(registry, name, &wl_seat_interface, 1);
		// wl_seat 表示一个输入设备集合（如键盘、鼠标、触摸屏等）。
		// 通过下面的函数 可以指定 wl_seat 监听器
		// 以便在 wl_seat 的能力发生变化时（如添加或移除输入设备）接收通知。
		// 例如，当一个新的输入设备连接到系统时，wl_seat 的能力会发生变化。
		// 这时，wl_seat 会通知客户端，并提供有关新设备的信息。
		// 通过 wl_seat_get_pointer() 函数获取指针设备的句柄。
		// 通过 wl_seat_get_keyboard() 函数获取键盘设备的句柄。
		// 通过 wl_seat_get_touch() 函数获取触摸设备的句柄。
		// 通过 wl_seat_get_focused() 函数获取当前焦点设备的句柄。
		// 通过 wl_seat_get_focused_surface() 函数获取当前焦点表面的句柄。
		wl_seat_add_listener(seata, &seat_listener, NULL);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, NULL);
	} else if (strcmp(interface, xdg_surface_interface.name) == 0) {
		// This is a xdg_surface, we don't need to do anything with it
	} else if (strcmp(interface, xdg_toplevel_interface.name) == 0) {
		// This is a xdg_toplevel, we don't need to do anything with it
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		// This is a wl_output, we don't need to do anything with it
	} else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
		data_device_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, 1);
		if (data_device_manager == NULL) {
			fprintf(stderr, "no wl_data_device_manager support\n");
			return ;
		}
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// Who cares
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static struct wl_buffer *create_buffer(void) {
	int stride = width * 4;
	int size = stride * height;

	// Allocate a shared memory file with the right size
	int fd = create_shm_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size);
		return NULL;
	}

	// Map the shared memory file
	shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shm_data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		close(fd);
		return NULL;
	}

	// Create a wl_buffer from our shared memory file descriptor
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
		stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);

	// Now that we've mapped the file and created the wl_buffer, we no longer
	// need to keep file descriptor opened
	close(fd);

	// Copy pixels into our shared memory file (MagickImage is from cat.h)
	memcpy(shm_data, MagickImage, size);

	return buffer;
}

int main(int argc, char *argv[]) {
	// Connect to the Wayland compositor
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to create display\n");
		return EXIT_FAILURE;
	}

	// Obtain the wl_registry and fetch the list of globals 用于从 Wayland 显示服务器获取全局对象注册表
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	//同步客户端与服务器的状态.会阻塞当前线程，直到 Wayland 显示服务器处理完客户端发送的所有请求，并将所有事件返回给客户端。
	if (wl_display_roundtrip(display) == -1) {
		return EXIT_FAILURE;
	}

	// Check that all globals we require are available
	if (shm == NULL || compositor == NULL || xdg_wm_base == NULL) {
		fprintf(stderr, "no wl_shm, wl_compositor or xdg_wm_base support\n");
		return EXIT_FAILURE;
	}
	if (seata == NULL) {
		fprintf(stderr, "no wl_seat support\n");
		return EXIT_FAILURE;
	}
	
	if (data_device_manager == NULL) {
		fprintf(stderr, "no wl_data_device_manager support\n");
		return EXIT_FAILURE;
	}
	
	// Create wl_data_device and add listener
	data_device = wl_data_device_manager_get_data_device(data_device_manager, seata);
	wl_data_device_add_listener(data_device, &data_device_listener, NULL);

	// Create a wl_surface, a xdg_surface and a xdg_toplevel
	surface = wl_compositor_create_surface(compositor);

	// 将wl_surface(绘图单元 不具备窗口管理能力) 升级为 xdg_surface(支持窗口管理功能。
	// 通过它，客户端可以接收窗口管理器的配置事件（如窗口大小、位置等），并对这些事件作出响应。)
	struct xdg_surface *xdg_surface =
		xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
	//获取顶层窗口
	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
	// 设置窗口管理器层面的监听
	// 主要用于与窗口管理器交互，接收窗口配置相关的事件。
	// E.g, 当窗口管理器配置窗口（如大小、位置）时触发，客户端需要响应并确认配置。
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	//设置顶层窗口监听
	// 主要用于处理窗口管理器发送的事件，如窗口关闭,最大，最小 请求等。
	// E.g, 当窗口管理器请求关闭窗口时触发，客户端需要响应并执行关闭操作。
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

	// Perform the initial commit and wait for the first configure event
	// 提交对wl_surface的更改，并等待窗口管理器的配置事件
	wl_surface_commit(surface);
	// 处理来自 Wayland 显示服务器的事件 直至配置完成
	while (wl_display_dispatch(display) != -1 && !configured) {
		// This space intentionally left blank
	}

	// Create a wl_buffer, attach it to the surface and commit the surface
	struct wl_buffer *buffer = create_buffer();
	if (buffer == NULL) {
		return EXIT_FAILURE;
	}

	//贴个图到 wl_surface上 提交
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);

	// Continue dispatching events until the user closes the toplevel
	while (wl_display_dispatch(display) != -1 && running) {
		// This space intentionally left blank
	}

	xdg_toplevel_destroy(xdg_toplevel);
	xdg_surface_destroy(xdg_surface);
	wl_surface_destroy(surface);
	wl_buffer_destroy(buffer);

	return EXIT_SUCCESS;
}
