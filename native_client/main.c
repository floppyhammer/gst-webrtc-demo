#include <glib.h>

#include "../src/client/client_pipeline.h"

int main(int argc, char *argv[]) {
    struct my_client_state *my_state = NULL;

    client_create(&my_state);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    g_main_loop_run(loop);

    // Cleanup
    g_main_loop_unref(loop);

    client_stop(my_state);

    return 0;
}
