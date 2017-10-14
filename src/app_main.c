#include <curl/curl.h>
#include <net_connection.h>

#include "app_main.h"
#include "uib_app_manager.h"
#include "auth.h"

/* app event callbacks */
static widget_class_h widget_app_create(void *user_data);
static void widget_app_terminate(void *user_data);
static int _on_create_cb(widget_context_h context, bundle *content, int w, int h, void *user_data);
static int _on_destroy_cb(widget_context_h context, widget_app_destroy_type_e reason, bundle *content, void *user_data);
static int _on_resume_cb(widget_context_h context, void *user_data);
static int _on_pause_cb(widget_context_h context, void *user_data);
static int _on_update_cb(widget_context_h context, bundle *content, int force, void *user_data);
static int _on_update_resize(widget_context_h context, int w, int h, void *user_data);
static void _on_low_memory_cb(app_event_info_h event_info, void *user_data);
static void _on_low_battery_cb(app_event_info_h event_info, void *user_data);
static void _on_device_orientation_cb(app_event_info_h event_info,
		void *user_data);
static void _on_language_changed_cb(app_event_info_h event_info,
		void *user_data);
static void _on_region_format_changed_cb(app_event_info_h event_info,
		void *user_data);

void nf_hw_back_cb(void* param, Evas_Object * evas_obj, void* event_info) {
	//TODO : user define code
	evas_obj = uib_views_get_instance()->get_window_obj()->app_naviframe;
	elm_naviframe_item_pop(evas_obj);
}

void win_del_request_cb(void *data, Evas_Object *obj, void *event_info) {
	ui_app_exit();
}

Eina_Bool nf_root_it_pop_cb(void* elm_win, Elm_Object_Item *it) {
	elm_win_lower(elm_win);
	return EINA_FALSE;
}

app_data *uib_app_create() {
	return calloc(1, sizeof(app_data));
}

void uib_app_destroy(app_data *user_data) {
	uib_app_manager_get_instance()->free_all_view_context();
	free(user_data);
}

int uib_app_run(app_data *user_data, int argc, char **argv) {
	widget_app_lifecycle_callback_s cbs = { 0, };

	cbs.create = widget_app_create;
	cbs.terminate = widget_app_terminate;

	int ret = widget_app_main(argc, argv, &cbs, user_data);
	if (ret != WIDGET_ERROR_NONE) {
		UIB_DLOG(DLOG_ERROR, LOG_TAG, "widget_app_main() is failed. err = %d",
				ret);
	}

	return ret;
}

static widget_class_h widget_app_create(void *user_data) {
	app_event_handler_h handlers[5] = { NULL, };

	widget_app_add_event_handler(&handlers[APP_EVENT_LOW_BATTERY],
			APP_EVENT_LOW_BATTERY, _on_low_battery_cb, user_data);
	widget_app_add_event_handler(&handlers[APP_EVENT_LOW_MEMORY],
			APP_EVENT_LOW_MEMORY, _on_low_memory_cb, user_data);
	widget_app_add_event_handler(
			&handlers[APP_EVENT_DEVICE_ORIENTATION_CHANGED],
			APP_EVENT_DEVICE_ORIENTATION_CHANGED, _on_device_orientation_cb,
			user_data);
	widget_app_add_event_handler(&handlers[APP_EVENT_LANGUAGE_CHANGED],
			APP_EVENT_LANGUAGE_CHANGED, _on_language_changed_cb, user_data);
	widget_app_add_event_handler(&handlers[APP_EVENT_REGION_FORMAT_CHANGED],
			APP_EVENT_REGION_FORMAT_CHANGED, _on_region_format_changed_cb,
			user_data);

	widget_instance_lifecycle_callback_s cbs = {
			.create = _on_create_cb,
			.destroy = _on_destroy_cb,
			.pause = _on_pause_cb,
			.resume = _on_resume_cb,
			.update = _on_update_cb,
			.resize = _on_update_resize,
	};

	return widget_app_class_create(cbs, user_data);
}

static
void widget_app_terminate(void *user_data) {
	uib_views_get_instance()->destroy_window_obj();
}

#define WAITMS(x)                               \
  struct timeval wait = { 0, (x) * 1000 };      \
  (void)select(0, NULL, NULL, NULL, &wait);


static void* send_request(void *item) {
	CURLcode res;
	CURL *http_handle;
	CURLM *multi_handle;
	int still_running; // keep number of running handles
	int repeats = 0;
	connection_h connection;
	int conn_err;
	char *proxy_address;
	char *routineName;

	eext_rotary_selector_item_part_text_set(item, "selector,sub_text", "Changing...");

	conn_err = connection_create(&connection);
	conn_err = connection_get_proxy(connection, CONNECTION_ADDRESS_FAMILY_IPV4, &proxy_address);

	curl_global_init(CURL_GLOBAL_DEFAULT);

	routineName = eext_rotary_selector_item_part_text_get(item, "selector,main_text");

	http_handle = curl_easy_init();
	if (http_handle) {
		if (conn_err == CONNECTION_ERROR_NONE && proxy_address) {
			curl_easy_setopt(http_handle, CURLOPT_PROXY, proxy_address);
		}

		curl_easy_setopt(http_handle, CURLOPT_URL, SMARTAPP_URL);

		char buf[255];

		struct curl_slist *list = NULL;
		list = curl_slist_append(list, "Content-Type: application/json");
		list = curl_slist_append(list, AUTH_HEADER);
		curl_easy_setopt(http_handle, CURLOPT_HTTPHEADER, list);
		sprintf(buf, "{\"name\":\"%s\"}", routineName);
		curl_easy_setopt(http_handle, CURLOPT_POSTFIELDS, buf);

		multi_handle = curl_multi_init();
		curl_multi_add_handle(multi_handle, http_handle);
		curl_multi_perform(multi_handle, &still_running);

		int numfds;

		do {
			CURLMcode mc;
			mc = curl_multi_wait(multi_handle, NULL, 0, 1000, &numfds);

			if (mc != CURLM_OK) {
			   fprintf(stderr, "curl_multi_wait() failed, code %d.\n", mc);
			   break;
			}

			if(!numfds) {
				repeats++; // count number of repeated zero num of file descriptors
				if(repeats > 1) {
					sleep(0.1);
				}
			}
			else
			  repeats = 0;

			curl_multi_perform(multi_handle, &still_running);
		} while(still_running);

		CURLMsg *msg = curl_multi_info_read(multi_handle, &numfds);
		if (msg->data.result == CURLE_OK) {
			eext_rotary_selector_item_part_text_set(item, "selector,sub_text", "Done!");
		} else {
			dlog_print(DLOG_ERROR, LOG_TAG, curl_easy_strerror(msg->data.result));
			eext_rotary_selector_item_part_text_set(item, "selector,sub_text", curl_easy_strerror(msg->data.result));
		}

		curl_multi_remove_handle(multi_handle, http_handle);
		curl_easy_cleanup(http_handle);
		curl_multi_cleanup(multi_handle);
	}
	curl_global_cleanup();
	connection_destroy(connection);
	dlog_print(DLOG_INFO, LOG_TAG, "Request finished!\n");

	return 0;
}

static void
_item_clicked_cb(void *data, Evas_Object *obj, void *event_info)
{
	Eext_Object_Item *item;
	const char *main_text;
	const char *sub_text;

	/* Get current seleted item object */
	item = eext_rotary_selector_selected_item_get(obj);

	/* Get set text for the item */
	main_text = eext_rotary_selector_item_part_text_get(item, "selector,main_text");
	sub_text = eext_rotary_selector_item_part_text_get(item, "selector,sub_text");

	dlog_print(DLOG_INFO, LOG_TAG, "Item Clicked!, Currently Selected \n");
	ecore_thread_run(send_request, NULL, NULL, item);
}

void
app_get_resource(const char *edj_file_in, char *edj_path_out, int edj_path_max)
{
    char *res_path = app_get_resource_path();
    if (res_path) {
        snprintf(edj_path_out, edj_path_max, "%s%s", res_path, edj_file_in);
        free(res_path);
    }
}

void
_item_create(Evas_Object *rotary_selector, char* routineName, char* iconName)
{
	Evas_Object *image;
	Eext_Object_Item * item;
	char image_path[PATH_MAX];

	item = eext_rotary_selector_item_append(rotary_selector);

	image = elm_image_add(rotary_selector);
	app_get_resource(iconName, image_path, (int)PATH_MAX);
	elm_image_file_set(image, image_path, NULL);

	eext_rotary_selector_item_part_content_set(item,
											"item,icon",
										  	 EEXT_ROTARY_SELECTOR_ITEM_STATE_NORMAL,
											 image);

	eext_rotary_selector_item_part_text_set(item, "selector,main_text", routineName);
}

static int _on_create_cb(widget_context_h context, bundle *content, int w, int h, void *user_data) {
	uib_app_manager_get_instance()->initialize(context, w, h);

	Evas_Object *parent = uib_views_get_instance()->get_window_obj()->app_naviframe;

	Evas_Object *rotary_selector;
	Elm_Object_Item *nf_it = NULL;

	rotary_selector = eext_rotary_selector_add(parent);
	eext_rotary_object_event_activated_set(rotary_selector, EINA_TRUE);

	_item_create(rotary_selector, "Bedtime", "009-sleeping-bed-silhouette.png");
	_item_create(rotary_selector, "Goodnight!", "008-moon-and-stars.png");
	_item_create(rotary_selector, "Wake-up", "010-man-waking-up-on-morning-sitting-on-bed-stretching-his-arms.png");
	_item_create(rotary_selector, "I'm Home", "003-home.png");
	_item_create(rotary_selector, "Party", "004-users-group.png");
	_item_create(rotary_selector, "TV", "007-television.png");
	_item_create(rotary_selector, "Cinema", "006-video-camera.png");
	_item_create(rotary_selector, "Cleaning", "005-vacuum-cleaner.png");
	_item_create(rotary_selector, "Out", "002-exit.png");
	_item_create(rotary_selector, "Away", "001-airplane-around-earth.png");

	evas_object_smart_callback_add(rotary_selector, "item,clicked", _item_clicked_cb, NULL);

	nf_it = elm_naviframe_item_push(parent, _("Rotary Selector"), NULL, NULL, rotary_selector, "empty");
	return WIDGET_ERROR_NONE;
}

static int _on_destroy_cb(widget_context_h context, widget_app_destroy_type_e reason, bundle *content, void *user_data) {
	return WIDGET_ERROR_NONE;
}

static int _on_resume_cb(widget_context_h context, void *user_data) {
	/* Take necessary actions when widget instance becomes visible. */
	return WIDGET_ERROR_NONE;
}

static int _on_pause_cb(widget_context_h context, void *user_data) {
	/* Take necessary actions when widget instance becomes invisible. */
	return WIDGET_ERROR_NONE;
}

static int _on_update_cb(widget_context_h context, bundle *content, int force,
		void *user_data) {
	/* Take necessary actions when widget instance should be updated. */
	return WIDGET_ERROR_NONE;
}

static int _on_update_resize(widget_context_h context, int w, int h,
		void *user_data) {
	/* Take necessary actions when the size of widget instance was changed. */
	return WIDGET_ERROR_NONE;
}

static void _on_low_battery_cb(app_event_info_h event_info, void *user_data) {
	/* Take necessary actions when the battery is low. */
}

static void _on_low_memory_cb(app_event_info_h event_info, void *user_data) {
	/* Take necessary actions when the system runs low on memory. */
}

static void _on_device_orientation_cb(app_event_info_h event_info,
		void *user_data) {
	/* deprecated APIs */
}

static void _on_language_changed_cb(app_event_info_h event_info,
		void *user_data) {
	/* Take necessary actions is called when language setting changes. */
	uib_views_get_instance()->uib_views_current_view_redraw();
}

static void _on_region_format_changed_cb(app_event_info_h event_info,
		void *user_data) {
	/* Take necessary actions when region format setting changes. */
}
