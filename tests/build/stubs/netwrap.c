#include <stdio.h>
#include <string.h>
#include <stddef.h>
/* TEST substitution of the network state: the file /tmp/ps5tm_test_ip ("none" = no network, otherwise the IP) */
int __real_ps5tm_current_ip(char *out, size_t size);
int __wrap_ps5tm_current_ip(char *out, size_t size) {
    FILE *f = fopen("/tmp/ps5tm_test_ip", "r");
    if (!f) return __real_ps5tm_current_ip(out, size);
    char b[64] = {0};
    if (!fgets(b, sizeof b, f)) { fclose(f); return -1; }
    fclose(f);
    b[strcspn(b, "\n")] = 0;
    if (!strcmp(b, "none")) return -1;
    snprintf(out, size, "%s", b);
    return 0;
}

