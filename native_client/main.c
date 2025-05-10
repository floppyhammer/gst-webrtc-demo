#include "../src/client/client_pipeline.h"
#include "../src/utils/platform.h"

int main(int argc, char *argv[]) {
    setenv("GST_TRACERS", "latency(flags=element)", 1);

    create_client(argc, argv);
}
