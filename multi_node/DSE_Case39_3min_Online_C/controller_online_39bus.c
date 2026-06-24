/**
 * controller_online_39bus.c — Online UKF for 39-Bus 10-Generator (streaming)
 */
#include "ukf_core_39.h"

int main(int argc, char **argv) {
    FILE *input = stdin;
    if (argc > 1) {
        input = fopen(argv[1], "r");
        if (!input) { fprintf(stderr, "ERROR: cannot open %s\n", argv[1]); return 1; }
        fprintf(stderr, "[Init] Reading from file: %s\n", argv[1]);
    } else {
        fprintf(stderr, "[Init] Reading from stdin (pipe). Ctrl+D to end.\n");
    }

    SystemParams sp;
    if (load_system_params(&sp, "system_params_39bus.bin") != 0) {
        fprintf(stderr, "ERROR: system_params_39bus.bin not found!\n");
        fprintf(stderr, "Run: python3 convert_params_39bus.py first.\n");
        return 1;
    }
    fprintf(stderr, "[Init] Loaded system_params_39bus.bin (n=%d, s=%d, nm=%d)\n", sp.n, sp.s, sp.nm);

    UKFState st;
    ukf_init(&sp, &st);
    fprintf(stderr, "[Init] UKF initialized. Processing measurements one-by-one...\n\n");

    printf("# time,delta1,delta2,delta3,delta4,delta5,delta6,delta7,delta8,delta9,delta10,omega1,omega2,omega3,omega4,omega5,omega6,omega7,omega8,omega9,omega10,RMSE\n");
    fflush(stdout);

    char line[8192];
    int count = 0;

    while (fgets(line, sizeof(line), input)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (strncmp(line, "time", 4) == 0 || strncmp(line, "step", 4) == 0) continue;
        if (strncmp(line, "QUIT", 4) == 0 || strncmp(line, "EXIT", 4) == 0) break;

        char *tok = strtok(line, ",");
        double z_k[NM];
        int has_ts = 0;
        double k_time = count / (double)sp.fs;

        char *endptr;
        double first_val = strtod(tok, &endptr);
        if (endptr != tok && *endptr == '\0' && first_val < 10000.0) {
            has_ts = 1;
            k_time = first_val;
            tok = strtok(NULL, ",");
        }

        for (int j = 0; j < NM && tok; j++) {
            z_k[j] = atof(tok);
            tok = strtok(NULL, ",");
        }

        double x_out[NS];
        double rmse_val;
        if (ukf_step(&sp, &st, z_k, k_time, x_out, &rmse_val) != 0) {
            fprintf(stderr, "ERROR: UKF step failed at count=%d\n", count);
            continue;
        }

        printf("%.6f,%.8f,%.8f,%.8f,%.8f,%.8f\n",
               k_time, x_out[0], x_out[1], x_out[2], x_out[3], x_out[4], x_out[5], x_out[6], x_out[7], x_out[8], x_out[9], x_out[10], x_out[11], x_out[12], x_out[13], x_out[14], x_out[15], x_out[16], x_out[17], x_out[18], x_out[19], rmse_val);
        fflush(stdout);

        count++;
        if (count % 10000 == 0)
            fprintf(stderr, "[Stream] Processed %d measurements (t=%.1fs)\n", count, k_time);
    }

    fprintf(stderr, "\n[Done] Processed %d measurements total.\n", count);
    return 0;
}
