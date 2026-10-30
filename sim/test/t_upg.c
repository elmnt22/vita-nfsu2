/* SetUpgradePart 0x61bc80 through shop_upgrade_model: each car's ENGINE
   (slot 0xd) and FRONT_BRAKE (0x1b) models at levels 0..3.
   usage: t_upg [dataroot] [car...] */
#include <stdio.h>
#include <string.h>
#include "shop.h"
void garage_tt_request(int what, int dir) { (void)what; (void)dir; }   /* main.c, not linked */

int main(int argc, char **argv)
{
    const char *root = argc > 1 ? argv[1] : "E:/NFS Underground 2 Vita/simroot/ux0/data/OpenUG2";
    static const char *const def[] = { "MIATA", "CIVIC", "350Z", "SKYLINE", "HUMMER", "RX8", "GTO", "ESCALADE" };
    int n = argc > 2 ? argc - 2 : (int)(sizeof def / sizeof def[0]);
    for (int i = 0; i < n; i++) {
        const char *car = argc > 2 ? argv[i + 2] : def[i];
        for (int slot = 0xd; slot <= 0x1c; slot += 0x1b - 0xd) {
            if (slot > 0x1b) break;
            printf("%-10s %s:", car, slot == 0xd ? "engine" : "brake ");
            for (int l = 0; l < 4; l++) {
                char m[64] = "";
                printf("  %d=%s", l, shop_upgrade_model(root, car, slot, l, m, sizeof m) ? m : "-");
            }
            printf("\n");
        }
    }
    return 0;
}
