
#include "esp_wifi.h"          




void set_as_sta();


//I could have just called connect and handled the disconnected event, but this seemed a bit simpler
//It's blocking, so it keeps everything linear
bool an_ap_already_exists();



void csi_handler(void *ctx, wifi_csi_info_t *data);


void enable_csi_rx();

