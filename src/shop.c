/* shop.c — the Car Shop screens. See shop.h for what comes from where. */
#include "shop.h"
#include "audio.h"
#include "music.h"
#include "options.h"
#include "feng.h"
#include "perfparts.h"
#include "vinyl.h"
#include "n2sim.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __vita__
#define SHOP_CACHE "ux0:data/OpenUG2/cache/shop_v22.bin"
#else
#define SHOP_CACHE "cache/shop_v22.bin"
#endif

/* ------------------------------------------------------------ game tables */

/* Packages (FRONTEND/FrontB.lzc). */
enum { PK_CATEGORY, PK_PERFORMANCE, PK_PARTS, PK_RIMBRAND, PK_RIMS, PK_SPLASH, PK_MAIN, PK_LOADING,
       PK_QR_MODE, PK_QR_TRACK, PK_QR_OPTIONS, PK_QR_CAR, PK_OPT_MAIN, PK_OPTIONS, PK_CTL, PK_JUKE, PK_CREDITS, PK_PAINT, PK_PERF_PKG, PK_PERF_BUY,
       PK_VLAYER, PK_DECAL, PK_DECALOV, PK_DLG_S, PK_DLG_M, PK_DLG_L, PK_CHUD, PK_CHUDOV, PK_REP, PK_COUNT };
static const char *const k_pk_name[PK_COUNT] = {
    "UI_ChooseCustomizeCategory.fng",       /* Car Shop, Body/Graphics/Specialties */
    "UI_ChoosePerformanceCategory.fng",     /* Performance Shop */
    "UI_GenericParts_Browser.fng",          /* part lists */
    "UI_ChooseRimBrand.fng",                /* rim brands */
    "UI_Rims_Browser.fng",                  /* a brand's rim styles */
    "UG_LS_Splash.fng",                     /* title: "Please press START to begin" */
    "UI_Main.fng",                          /* main menu */
    "PC_Loading.fng",                       /* boot loading screen (GLOBAL/GLOBALB.BUN) */
    "UI_QRModeSelect.fng",                  /* Quick Race: the race modes */
    "UI_QRTrackSelect.fng",                 /* ... the mode's tracks */
    "UI_QRModeOptions.fng",                 /* ... the race settings */
    "UI_QRCarSelect.fng",                   /* ... the car */
    "UI_OptionsMain.fng",                   /* Options: the categories */
    "UI_Options.fng",                       /* ... a category's rows */
    "UI_Options_PC_Controller.fng",         /* ... Controls */
    "EA_TRAX_Jukebox.fng",                  /* ... EA Trax */
    "Credits.fng",                          /* ... Credits */
    "UI_Paint.fng",                         /* Graphics Shop: Paint */
    "UI_ChoosePerformancePackage.fng",      /* Performance Shop: a category's packages */
    "UI_BuyPerformanceParts.fng",           /* ... its individual parts */
    "UI_ChooseVinylLayer.fng",              /* Graphics Shop: Vinyls, the four layers */
    "UI_DecalMain.fng",                     /* Graphics Shop: Decals, the locations and slots */
    "UI_DecalsOverlay.fng",                 /* ... a slot's decals over it */
    "GenericDialog_SMALL.fng",              /* the shop's questions and messages (0x558020) */
    "GenericDialog_MED.fng",
    "GenericDialog_LARGE.fng",
    "UI_ChooseCustomHUD.fng",               /* Car Specialties: Custom Gauges, the gauge preview + colours */
    "UI_CustomHUDOverlay.fng",              /* ... the styles' list over it */
    "UI_Menu_Asset_Reputation.fng",         /* the Visual Rating box (0x557ec0) */
};

/* One scroller entry: language hash, icon TPK key, what it opens. Both hashes
   are the immediates SPEED2.EXE pushes when it builds the menu. */
typedef struct { uint32_t str, tex; int group; } ShopEntry;

/* Car Shop (fn ~0x520d60): Car Lot only exists in the career. */
enum { CAT_BODY, CAT_PERFORMANCE, CAT_GRAPHICS, CAT_SPECIALTIES, CAT_COUNT };
static const ShopEntry k_car_shop[CAT_COUNT] = {
    { 0xa92b4ae8u, 0xb802f10eu, 0 },   /* Change Body Parts           SHOP_ICON_BODY */
    { 0xeca6b0ccu, 0xc56d3032u, 0 },   /* Change Performance Parts    SHOP_ICON_PERFORMANCE */
    { 0xc6089ca7u, 0xdc99aafeu, 0 },   /* Change Paint / Vinyls       SHOP_ICON_GRAPHIC */
    { 0xc10d7979u, 0x3f40e75fu, 0 },   /* Change Interiors / Car Specialties SHOP_ICON_INTERIORS */
};

/* Part groups of the GLOBALB car part database (Nikki PartUnderground2). */
enum { G_FRONT_BUMPER = 1, G_REAR_BUMPER = 2, G_LEFT_MIRROR = 30 /* WING_MIRROR */, G_WIDE_BODY = 6,
       G_ROOF = 7, G_HOOD = 9, G_SKIRT = 11, G_SPOILER = 12, G_HEADLIGHT = 14,
       G_BRAKELIGHT = 15, G_EXHAUST = 16, G_WHEEL = 28 };
#define G_CARBON 0x33   /* opens the Carbon Fiber submenu; never a list */
#define G_CF_KIT 0x5d     /* Carbon Fiber: KIT_CARBON (convert / revert the body kit) */
#define G_CF_TRUNK 0x60   /* Carbon Fiber: TRUNK_CARBON (convert / revert the trunk) */

/* Body Shop (fn ~0x545b60, 0x545700(slot, tex, str) per entry). */
static const ShopEntry k_body_shop[] = {
    { 0x739c4a1bu, 0x739c4a1bu, G_FRONT_BUMPER },  /* Front Bumpers */
    { 0x015c767cu, 0x015c767cu, G_REAR_BUMPER },   /* Rear Bumpers */
    { 0x4e6c2af9u, 0x4e6c2af9u, G_SKIRT },         /* Side Skirts */
    { 0xbb034ea6u, 0xbb034ea6u, G_SPOILER },       /* Spoilers */
    { 0x028f7092u, 0x028f7092u, G_HOOD },          /* Hoods */
    { 0x79165861u, 0x79165861u, G_ROOF },          /* Roof Scoops */
    { 0x3dc6a111u, 0xf69ac384u, G_HEADLIGHT },     /* Headlights */
    { 0x663437a9u, 0x2cbb2d1cu, G_BRAKELIGHT },    /* Taillights */
    { 0x0dd35467u, 0xc83de19au, G_LEFT_MIRROR },   /* Side Mirrors */
    { 0x2aced725u, 0xa434a006u, G_EXHAUST },       /* Exhaust Tips */
    { 0x0294d2a3u, 0x0294d2a3u, G_WHEEL },         /* Rims */
    { 0x24911e04u, 0x24911e04u, G_CARBON },        /* Carbon Fiber */
    { 0x945e9bc5u, 0x945e9bc5u, G_WIDE_BODY },     /* Wide Body Kits */
};
#define N_BODY (int)(sizeof k_body_shop / sizeof k_body_shop[0])

/* Performance Shop (0x5304f0): label, icon, the parts category (0x530400). */
static const ShopEntry k_perf_shop[] = {
    { 0x026eea5eu, 0xc15c94e6u, 3 }, { 0xa7884765u, 0x502abeedu, 7 },   /* Engine, ECU */
    { 0x73ad2f72u, 0x01a29ffau, 1 }, { 0x9c34c75fu, 0x178475e7u, 5 },   /* Transmission, Suspension */
    { 0xa7886f38u, 0x9701bde4u, 2 }, { 0xabca6aefu, 0x06e8e477u, 8 },   /* Nitrous Oxide, Tires */
    { 0xaa8959adu, 0xbaa23a28u, 6 }, { 0x758d5e3cu, 0xea57d2c4u, 0 },   /* Brakes, Weight Reduction */
    { 0xabd0ff14u, 0x06ef789cu, 4 },                                     /* Turbo */
};
/* a category's packages (0x556e60): label, icon, type -- 0 Choose
   Individual Parts, 1..3 the package level (PERFORMANCE_PACKAGE_%d /
   PERFORMANCE_ICON_PACKAGE_%d), PP_STOCK Return To Stock */
#define PP_STOCK 4
static const ShopEntry k_perf_pkg[5] = {
    { 0xdf3fd1d6u, 0xc7d3f6a7u, 0 },
    { 0xb75ba92cu, 0xb8c8c0d4u, 1 }, { 0xb75ba92du, 0xb8c8c0d5u, 2 }, { 0xb75ba92eu, 0xb8c8c0d6u, 3 },
    { 0xed55c635u, 0x6a93275fu, PP_STOCK },
};
/* Graphics Shop (~0x548200). */
static const ShopEntry k_graphics_shop[] = {
    { 0x6a4c46a6u, 0x550681a4u, 0 }, { 0x78deea1cu, 0x55778e5au, 0 },   /* Paint, Vinyls */
    { 0x4e933e83u, 0xda1dae54u, 0 }, { 0xd132a6feu, 0x911a4edeu, 0 },   /* Decals, Install Setup (career: Purchase Setup 0x920dbb02) */
};
/* Decals (UI_DecalMain, 0x5530f0 / tree 0x5469f0): what each level of the
   location menu holds. group: a decal slot (car slot 0x61 + loc*8 + n,
   opens the decals) or the next level (0x1000x a location, 0x2000x a
   quarter panel's layout). Every entry's icon and label as 0x5469f0 pushes
   them; the hood (0x10005) is not on the PC menu. */
#define DL_ROOT 0
static const ShopEntry k_decal_root[] = {
    { 0x52ded91du, 0x52ded91du, 0x69 },    { 0xac7937b4u, 0xac7937b4u, 0x71 },      /* Windshield, Rear Window */
    { 0xda88b711u, 0xda88b711u, 0x10001 }, { 0xc9a967c4u, 0xc9a967c4u, 0x10002 },  /* Left / Right Door */
    { 0x2c710c4du, 0x2c710c4du, 0x10003 }, { 0xffa7d360u, 0xffa7d360u, 0x10004 },  /* Left / Right Quarter Panel */
};
static const ShopEntry k_decal_ldoor[] = {   /* "Slot 1".."Slot 6", DECAL_LEFTDOOR_SLOTn */
    { 0x89bb9ceau, 0xfe957f48u, 0x79 }, { 0x89bb9cebu, 0xfe957f49u, 0x7a }, { 0x89bb9cecu, 0xfe957f4au, 0x7b },
    { 0x89bb9cedu, 0xfe957f4bu, 0x7c }, { 0x89bb9ceeu, 0xfe957f4cu, 0x7d }, { 0x89bb9cefu, 0xfe957f4du, 0x7e },
};
static const ShopEntry k_decal_rdoor[] = {
    { 0x89bb9ceau, 0x2e40eadbu, 0x81 }, { 0x89bb9cebu, 0x2e40eadcu, 0x82 }, { 0x89bb9cecu, 0x2e40eaddu, 0x83 },
    { 0x89bb9cedu, 0x2e40eadeu, 0x84 }, { 0x89bb9ceeu, 0x2e40eadfu, 0x85 }, { 0x89bb9cefu, 0x2e40eae0u, 0x86 },
};
static const ShopEntry k_decal_lq[] = { { 0xb348f286u, 0x13ae389cu, 0x20006 }, { 0xb348f287u, 0x13ae389bu, 0x20007 } };
static const ShopEntry k_decal_rq[] = { { 0xb348f286u, 0x5ced1be2u, 0x20008 }, { 0xb348f287u, 0x5ced1be1u, 0x20009 } };
static const ShopEntry k_decal_lq1[] = { { 0x89bb9ceau, 0x13ae389cu, 0x89 } };
static const ShopEntry k_decal_lq2[] = { { 0x89bb9ceau, 0xc813d5bbu, 0x89 }, { 0x89bb9cebu, 0xc813d5bcu, 0x8a } };
static const ShopEntry k_decal_rq1[] = { { 0x89bb9ceau, 0x5ced1be2u, 0x91 } };
static const ShopEntry k_decal_rq2[] = { { 0x89bb9ceau, 0x392f21c1u, 0x91 }, { 0x89bb9cebu, 0x392f21c2u, 0x92 } };
static const struct { int id; const ShopEntry *e; int n; } k_decal_level[] = {
    { DL_ROOT, k_decal_root, 6 }, { 0x10001, k_decal_ldoor, 6 }, { 0x10002, k_decal_rdoor, 6 },
    { 0x10003, k_decal_lq, 2 }, { 0x10004, k_decal_rq, 2 },
    { 0x20006, k_decal_lq1, 1 }, { 0x20007, k_decal_lq2, 2 }, { 0x20008, k_decal_rq1, 1 }, { 0x20009, k_decal_rq2, 2 },
};

/* Paint Shop categories (UI_Paint, 0x53bd50): label, icon, car slot id. */
static const ShopEntry k_paint_cat[N2_PAINT_COUNT] = {
    { 0xef5b4a75u, 0xa3b76154u, 0x3f }, { 0xdda5b898u, 0xa8d7e6edu, 0x45 },   /* Base Paint, Spoiler */
    { 0xef644b90u, 0xd2241205u, 0x4b }, { 0x4d90f415u, 0xdf5c2bcau, 0x4c },   /* Roof Scoop, Mirrors */
    { 0x0f030942u, 0xd223f84au, 0x49 }, { 0xdd3bebf9u, 0xd223f84au, 0x4a },   /* Rims, Spinners */
    { 0xdacd9fffu, 0x7eb55a26u, 0x46 }, { 0x3b3bfaf0u, 0x4e875a92u, 0x44 },   /* Brakes, Engine Accents */
    { 0xba1cc49cu, 0xa6343aecu, 0x47 },                                       /* Muffler Tip */
};
/* Vinyl categories (0x546030), in the game's order: label, icon (0x50e190
   by group), the vinyl group (the part's upgrade byte & 0x1f). */
static const ShopEntry k_vinyl_cat[] = {
    { 0x4e6ae825u, 0x2075eaf3u, 1 },  { 0xb6d9a479u, 0x695654a8u, 0 },    /* Tear Set, Tear */
    { 0xff8de430u, 0x4c12bc51u, 3 },  { 0xd2982464u, 0xf7352706u, 2 },    /* Stripe Set, Stripes */
    { 0x6e37cdc4u, 0x006a2c32u, 5 },  { 0xd24c5678u, 0x16b5ce67u, 4 },    /* Splash Set, Splash */
    { 0x687f411eu, 0x43a00accu, 7 },  { 0xc436f812u, 0x08a07001u, 6 },    /* Modern Set, Modern */
    { 0x4aa403deu, 0x4ef4919fu, 9 },  { 0xb3ab0d65u, 0xf8148554u, 8 },    /* Flames Set, Flames */
    { 0x0a16b73du, 0x8ca7c26bu, 11 }, { 0x588a0a91u, 0xf2173020u, 10 },   /* Lightning Set, Lightning */
    { 0x7f61feedu, 0x36da827bu, 13 }, { 0xcee2f041u, 0x134c6830u, 12 },   /* Racing Set, Racing */
    { 0x2909a9d3u, 0x4eece261u, 15 }, { 0xb6d214e7u, 0x694ec516u, 14 },   /* Flag Set, Flag */
    { 0xd55ad357u, 0x4bede425u, 17 }, { 0xd4c40cebu, 0x192d84dau, 16 },   /* Tribal Set, Tribal */
    { 0x3f6da129u, 0x31cfc477u, 19 }, { 0xb6db5bfdu, 0x69580c2cu, 18 },   /* Wild Set, Wild */
    { 0x913ac36au, 0x694fec66u, 20 }, { 0x64cf99a3u, 0xe22eb851u, 21 },   /* Hoods, After Market */
    { 0xb6cff09bu, 0x694ca0cau, 23 }, { 0xd6d115e4u, 0x1b3a8dd3u, 24 },   /* Body, Unique */
    { 0x6fd3b71cu, 0x0960dcabu, 25 }, { 0x8e076becu, 0x0dd7673au, 26 },   /* Top Layer, Art Factory */
    { 0x4a0c932du, 0x1ba508fcu, 27 }, { 0x34e6adbau, 0x93ec1ce9u, 22 },   /* Contest, Sponsor */
};
#define N_VCAT (int)(sizeof k_vinyl_cat / sizeof k_vinyl_cat[0])
/* Car Specialties Shop (~0x55973a). */
static const ShopEntry k_special_shop[] = {
    { 0x1eb104c8u, 0xc591c4b6u, 0xa3 }, { 0xa420be7cu, 0x02929058u, 0x9e },   /* Custom Gauges (slot 0xa3), Neon (class 0x53aa80, slot 0x9e) */
    { 0xa42419abu, 0x3f23165cu, 0x99 }, { 0xef6679c9u, 0x599c870fu, 0x9f },   /* Window Tint (slot 0x99), Lights (0x9f) */
    { 0x2981cd3eu, 0xbded289au, 0xa2 }, { 0xeac74624u, 0x0c9e0d00u, 0xa1 },   /* Nitrous Purge, Hydraulics */
    { 0x8b543f71u, 0xe803e1cdu, 0x22 },                                         /* Audio (ICE, class 0x53aac0, slot 0x22) */
    { 0x094a635eu, 0x0dc9c33au, 0x1d }, { 0xa41d9eb6u, 0x5a309b10u, 9 },      /* Spinners, Split Hoods */
    { 0x27891593u, 0xb2798bb4u, 0xa0 },                                         /* Doors */
};
#define NELEM(a) (int)(sizeof (a) / sizeof (a)[0])
/* Car Specialties -> Trunk Audio, UI_ICEMAIN mode 3 (see trunk audio) */
#define ICE_GROUP_COMP   (-4)           /* Audio Components: mode 4 */
static const ShopEntry k_ice_mode3[] = {
    { 0x28bf66f6u, 0x76167b7eu, ICE_GROUP_COMP },   /* Audio Components */
    { 0xc8d23393u, 0xf7e5b04au, 0x22 },             /* Trunk Layout */
};
#define ICE_SLOT_ICON 0xe803e1cdu
/* Car Specialties -> Neon (UI_CustomNeonMain, builder 0x548010: one
   0x53bc90(slot, icon, name) per entry); group = the neon slot - 0x9a */
static const ShopEntry k_neon_shop[] = {
    { 0xd8ea6d2fu, 0x745ee753u, 0 }, { 0x437e4111u, 0x05d41facu, 1 }, { 0x2225348fu, 0x759c2bcau, 3 } };

/* Rim brands (fn ~0x546540, 0x5463b0(key, tex, str) per brand): the language
   hash, the logo, the BRAND_NAME key of the brand's wheels in the part
   database, and the CARS/WHEELS/GEOMETRY_<folder>.BIN that holds them. The
   game builds one of two lists; the second is the tuner list. The first is
   used here for the SUVs (assumption: the branch condition is not decoded). */
typedef struct { uint32_t str, tex, key; const char *folder; } RimBrand;
static const RimBrand k_rims_tuner[] = {
    { 0x0cf90ef1u, 0xf75128b1u, 0x352d08d1u, "5ZIGEN" },
    { 0xd2e7035eu, 0x20edad1eu, 0x0013053eu, "AVUS" },
    { 0x6b3d4f56u, 0xf224ab29u, 0x00009536u, "BBS" },
    { 0x300b420bu, 0x0e38de9eu, 0x02b77febu, "ENKEI" },
    { 0x9d50e640u, 0xbe399df3u, 0xa26ad820u, "GIANELLE" },
    { 0x30786eb7u, 0x0ea60b4au, 0x0324ac97u, "KONIG" },
    { 0x412bd420u, 0xe50d0313u, 0x695fce00u, "LEXANI" },
    { 0xa08a29b3u, 0x9a00eb73u, 0x48e25793u, "LOWENHART" },
    { 0xd2ed7917u, 0x36c04f4au, 0x00197af7u, "MOMO" },
    { 0xfb7dfc68u, 0x6c2fa9dbu, 0x00000648u, "OZ" },
    { 0xd2eddffbu, 0x3f85c14du, 0x0019e1dbu, "NFSU" },
    { 0x29f66022u, 0x27ebd095u, 0xdd544a02u, "RACINGHART" },
    { 0xd2f037d5u, 0x20f6e195u, 0x001c39b5u, "ROTA" },
    { 0xd2f2685bu, 0x20f9121bu, 0x001e6a3bu, "VOLK" },
    { 0xd2f2f582u, 0x20f99f42u, 0x001ef762u, "WORK" },
    { 0x310bf423u, 0x0f3990b6u, 0u,          NULL },     /* Stock */
};
static const RimBrand k_rims_suv[] = {
    { 0xd2e889dau, 0x20ef339au, 0x00148bbau, "DONZ" },
    { 0xd2e9a3e4u, 0x20f04da4u, 0x0015a5c4u, "FOXX" },
    { 0x9d50e640u, 0xbe399df3u, 0xa26ad820u, "GIANELLE" },
    { 0xbe87ba52u, 0x9f4d4412u, 0xc3a1ac32u, "GIOVANNA" },
    { 0x3e867905u, 0x28de92c5u, 0x66ba72e5u, "KAIZER" },
    { 0xfb7dfc68u, 0x6c2fa9dbu, 0x00000648u, "OZ" },
    { 0xd2eddffbu, 0x3f85c14du, 0x0019e1dbu, "NFSU" },
    { 0xc49c11feu, 0xf6ca43beu, 0xbf3608c0u, "WELDWHEEL" },
    { 0x310bf423u, 0x0f3990b6u, 0u,          NULL },     /* Stock */
};
/* What "Stock" puts back: the port's boot rims (NFSU STYLE01). */
#define STOCK_RIM_FOLDER "NFSU"
#define STOCK_RIM_STYLE  1
#define STR_RIMS        0x0294d2a3u   /* VISUAL_PART_RIMS "Rims" */
#define STR_RIM_STYLES  0xe4504d8du   /* CUSTOMIZE_RIM_STYLES_TITLE */

/* Main menu (fn ~0x4aee66; the PC list, so Online and LAN are in it).
   group: what confirming it does in this port (0 = not built yet). */
enum { MM_NONE, MM_QUICK_RACE, MM_CUSTOMIZE, MM_OPTIONS };
static const ShopEntry k_main_menu[] = {
    { 0x5e7e1192u, 0x03704f3du, MM_NONE },        /* Career          MAIN_ICON_CAREER */
    { 0x24909fd7u, 0x4e6fbb02u, MM_QUICK_RACE },  /* Quick Race      MAIN_ICON_QUICK_RACE */
    { 0x7b63ff85u, 0x771849a5u, MM_NONE },        /* Online          MAIN_ICON_GO_ONLINE */
    { 0x7294434cu, 0xd13a2e66u, MM_NONE },        /* LAN             MAIN_ICON_LAN */
    { 0xa53039a3u, 0xb0c46023u, MM_CUSTOMIZE },   /* Customize       MAIN_ICON_CUSTOMIZE_CAR */
    { 0xbcb18f38u, 0x6b303856u, MM_NONE },        /* Profile Manager MAIN_ICON_PROFILE_MANAG */
    { 0xed1f0b2cu, 0x3058fe37u, MM_OPTIONS },     /* Options         MAIN_ICON_OPTIONS */
};
#define STR_MAIN_MENU  0x1398f642u   /* UI_MAIN_TITLE */

/* Options (UI_OptionsMain 0x4e3560, icons 0x4b6230: 0x51f670(icon, label);
   picking one sets the category 0x83aa04 (0x49a620..) and 0x4b6080 opens
   UI_Options.fng for 0-3 / 7, UI_Options_PC_Controller 4, EA_Trax_Jukebox 5,
   Credits 8). Split screen / LAN / online are out of scope. */
enum { OC_AUDIO = 0, OC_DISPLAY = 1, OC_GAMEPLAY = 2, OC_PLAYER = 3, OC_CONTROLS = 4,
       OC_EATRAX = 5, OC_CREDITS = 8 };
static const ShopEntry k_opt_main[] = {
    { 0xe76cd783u, 0xf37af144u, OC_AUDIO },      /* Audio */
    { 0x4204d527u, 0x8a006328u, OC_DISPLAY },    /* Display (PC video modes: not on the Vita) */
    { 0xd0cf6ee1u, 0x4df98fb2u, OC_GAMEPLAY },   /* Gameplay */
    { 0xf760eabeu, 0xd708efefu, OC_PLAYER },     /* Player */
    { 0x04dc6db5u, 0xa04a7b26u, OC_CONTROLS },   /* Controls */
    { 0xdcfb6b36u, 0xc52ccbf6u, OC_EATRAX },     /* EA GAMES TRAX */
    { 0x0905101fu, 0x51009e20u, OC_CREDITS },    /* Credits */
};
#define STR_OPTIONS 0xed1f0b2cu
/* UI_Options rows (OR_*): the category builders 0x4b6e60 audio, 0x4b7310
   gameplay, 0x4b7cd0 player; each row's label (vt slot 3) and its value */
enum { OR_MASTER, OR_FE_MUSIC, OR_IG_MUSIC, OR_SPEECH, OR_SFX, OR_TIRE, OR_ENGINE, OR_AUDIO_MODE,
       OR_JUMP_CAM, OR_MIRROR, OR_UNITS, OR_CAREER_DIFF, OR_EXPLORE_MAP, OR_RACE_MAP, OR_SAVE,
       OR_TRANS, OR_STABILITY, OR_CAMERA, OR_GAUGES, OR_RACE_POS, OR_LAP_INFO, OR_SPLIT, OR_SCORE,
       OR_LEADERBOARD, OR_WHEEL, OR_FEEDBACK, OR_ASK_TRANS,
       OR_REAR_TOUCH, OR_TOUCH_SENS,     /* the Vita's rear touchpad, in the wheel rows' place */
       OR_COUNT };
static const uint32_t k_or_name[OR_COUNT] = {
    0x26782c3eu, 0x418e681du, 0xdf21eac2u, 0x9e5fb82au, 0xfd487543u, 0x69b745c6u, 0xa2b1f888u, 0x2881ab87u,
    0xf26a5cbfu, 0x85a6ce05u, 0xf86a6272u, 0xbc0e6c55u, 0xc6269082u, 0x9fa5ec9eu, 0x996463e6u,
    0xd31407e7u, 0xdf06ed22u, 0xf6ccdc5fu, 0xac148579u, 0x82cd8f92u, 0x969f96a5u, 0x084bc378u, 0xc03f9f19u,
    0xc93fbfb5u, 0x4deab931u, 0x358cdcfdu, 0xbfb34120u, STR_REAR_TOUCH, STR_TOUCH_SENS };
#define OPT_MAX_ROWS 12          /* the package's pool */
#define OPT_PAGE     8           /* 0x4cf750: +0x134 = 8 */
#define K_ARROW_WHITE 0x9943d584u   /* U2_Menu_Arrow_White: 0x54ef30 / 0x54f410 with 1 */
/* UI_Options_PC_Controller (ctor 0x4ecd10, rows 0x4dba50: per action a name
   0x51e090 and Primary / Secondary cells 0x51dfc0, 7 a page (+0x13c)) */
static const uint32_t k_ctl_name[OPT_ACT_COUNT] = {
    0x153e7337u, 0xbd7ba1aau, 0x409607e2u, 0x82ebcc98u, 0x3eb318ceu, 0xb7d33921u, 0xb3d1ea35u,
    0xdf5aef26u, 0x419ca63au, 0x35b9f571u, 0x4a1341b3u, 0x1e7e3391u, 0x58baebbau, 0x735b2e6cu,
    0x6fc04e6fu, 0xa081f0ccu, 0x64730aacu, 0x6f99f62au };
#define CTL_PAGE         7
#define O_CTL_DEVICE     0x4e11cf90u   /* "Keyboard Configuration" -> the device's (0x4ace00) */
#define O_CTL_PRIMARY    0x666593c4u
#define O_CTL_SECONDARY  0x59aba1e8u
#define O_CTL_DEV_L      0xd1cda594u   /* device arrows: hidden with one device */
#define O_CTL_DEV_R      0x9345e4a7u
#define STR_CTL_JOYPAD   0xf91fba4du   /* "Joypad Configuration" */
#define STR_CTL_CONFIG   0x8bab66efu   /* "Configure your settings" */
#define STR_CTL_PRESS    0x23cf49aau   /* "Press a button to use for this function" */
#define STR_CTL_ASSIGN   0xfed0d3dfu   /* "Press enter to assign a new key or button" */

/* Quick Race (QR.md). Mode Select (0x4b2d60, 0x51f670(icon, string) per
   mode; Outrun only in split screen / online). group = the mode type. */
static const ShopEntry k_qr_modes[] = {
    { 0x93fdee74u, 0xe9638d3eu, QR_CIRCUIT },   /* Circuit */
    { 0xdcb9f861u, 0x2521e5ebu, QR_SPRINT },    /* Sprint */
    { 0x35102edfu, 0xaaab31e9u, QR_DRAG },      /* Drag */
    { 0xd7162cfau, 0x00119144u, QR_DRIFT },     /* Drift */
    { 0x30da61e1u, 0x91703aefu, QR_STREETX },   /* Street X */
    { 0xc786c4e7u, 0x3f6495c5u, QR_URL },       /* Underground Racing League */
    { 0x6f2421b7u, 0x713d99c1u, QR_FREERUN },   /* Free Run */
};
#define STR_QR_MODE_TITLE  0x05416105u   /* "Quick Race Mode Select" (0x4e2a50) */
#define STR_QR_TRACK_TITLE 0xe5d63bb5u   /* "Quick Race Track Select" (0x4e2f30) */
#define STR_QR_CAR_TITLE   0x5ac1fb59u   /* "Quick Race Car Select" (0x4b2310) */
#define STR_URL_RACE       0x75f903f4u   /* the track select's race type for URL */
#define STR_KM 0xb5d640b1u
#define STR_MI 0xac3a4f4cu
/* Track Select objects (0x4ce170) */
#define O_QR_TRACK_SLOT  0x1cf6f873u   /* the icon strip's centre slot */
#define O_QR_TRACK_NAME  0xf601f2d4u
#define O_QR_RACE_TYPE   0x15e1351bu
#define O_QR_DISTANCE    0x080c9daau
#define O_QR_BEST_LABEL  0x36227ee9u
#define O_QR_BEST        0x0555eecau
#define O_QR_DEBUG       0x53c956c4u   /* "Track Number DEBUG", hidden */
#define O_QR_MAP         0x90fe80f3u   /* TRACKMAP<event> (0x4cdd40) */
#define O_QR_COMMENT     0x258e6a24u   /* QR_TRACK_%d_COMMENT */
#define O_SCROLL_L       0x444969fdu   /* ScrollBar_Arrow_1: hidden on the first entry */
#define O_SCROLL_R       0x444969feu   /* ScrollBar_Arrow_2: hidden on the last */
/* Car Select objects (0x4b2310 / 0x497bf0) */
#define O_QR_LOGO        0x3e01ad1du   /* ManufacturerLogo: CARSELECT_MANUFACTURER_<make> */
#define O_QR_BADGE       0xb05dd708u   /* CarBadge: SECONDARY_LOGO_<type> */
#define O_QR_FILTER      0x3e81de59u
#define O_QR_RACE_MODE   0xa9205bd8u   /* the "Race Mode:" box */
#define O_QR_CUSTOMIZE   0xa936c3a2u   /* help bar "Customize" (car select -> Car Shop: not ported) */
#define K_GENERIC_PLACEHOLDER 0xc8e80d1fu
#define K_MENU_ARROW     0x367b3e04u   /* U2_Menu_Arrow: the option rows' arrows (0x54ef30) */
/* car stat sliders: backing, handle, fill (U2 names hashed upper) */
static const uint32_t k_qr_slider[3][3] = {
    { 0x70187fe5u, 0xdb1ce262u, 0x30cf3780u },   /* accel_backing / _handle / _cur */
    { 0x2c2f6ab0u, 0xd14c1fcdu, 0x164274cbu },   /* top_speed_ */
    { 0xaeee7572u, 0x1b13da4fu, 0x4ff66e8du },   /* handling_ */
};
/* track icons by track record +0x7c (0x4984b0); 5: by mode */
static const uint32_t k_qr_area_icon[5] = { 0xb5a633a0u, 0x79cead40u, 0x4b938e3cu, 0x4fde6febu, 0x6ba31820u };
#define K_TRACK_LOCKED  0xf05704acu
#define K_TRACK_DEFAULT 0x74ce8c0bu
/* car makes (type record +0xc0) for CARSELECT_MANUFACTURER_<make> */
static const char *const k_qr_makes[] = {
    "PEUGEOT", "FORD", "TOYOTA", "NISSAN", "MAZDA", "HONDA", "HUMMER", "LINCOLN", "CADILLAC",
    "HYUNDAI", "LEXUS", "VOLKSWAGEN", "AUDI", "ACURA", "MITSUBISHI", "INFINITI", "PONTIAC", "SUBARU" };
static const char *const k_qr_badges[] = {
    "PEUGOT", "FOCUS", "COROLLA", "240SX", "MIATA", "CIVIC", "HUMMER", "NAVIGATOR", "ESCALADE",
    "TIBURON", "SENTRA", "CELICA", "IS300", "SUPRA", "GOLF", "A3", "RSX", "ECLIPSE", "TT", "RX8",
    "350Z", "G35", "3000GT", "GTO", "MUSTANGGT", "SKYLINE", "LANCEREVO8", "RX7", "IMPREZAWRX" };
/* Car Select filters 0x7f444c */
static const struct { int id; uint32_t str; } k_qr_filters[] = {
    { 3, 0x6b68362cu },   /* All Cars */
    { 1, 0xe798a2f7u },   /* Stock Cars */
    { 2, 0xe7ab43d3u },   /* My Cars */
};
/* Game Options rows (vt 0x798014 builders) */
enum { QO_DIR, QO_KO, QO_LAPS, QO_TRAFFIC, QO_OPP, QO_DIFF, QO_CATCHUP };
static const uint32_t k_qo_name[7] = {
    0xa88ffeb4u, 0x7fd73849u, 0x48494e83u, 0xeb9dfc09u, 0x3384a679u, 0x4d156786u, 0x8b8e913au };
#define STR_FORWARD 0x9e4c4648u
#define STR_REVERSE 0x22f254afu
#define STR_OFF     0x0000ccfau
#define STR_ON      0x0000063cu
static const uint32_t k_qo_traffic[4] = { 0x896bcb4du, 0xb6bd9c95u, 0x907c555eu, 0xb672f15au };
static const uint32_t k_qo_diff[3] = { 0xdbbb4e8fu, 0xcab262deu, 0xdbbcf37cu };
#define QR_ROWS_SHOWN 7     /* the option list's page (0x552780 +0x134) */
#define QR_ROW_STEP   36.0f /* TITLE_MASTER's height (+0x108) */
#define O_PRESS_START  0xc4df3ff2u   /* "Please press START to begin" (script-faded) */

/* Screen titles (English.bin labels). */
#define STR_CAR_SHOP     0x1da8562fu   /* CAR_SHOP_TITLE "Car Shop" */
#define STR_BODY_SHOP    0x796dc449u   /* CUSTOMIZE_TITLE_BODY_SHOP */
#define STR_PERF_SHOP    0xb938cb4bu   /* PERFORMANCE_SHOP_TITLE */
#define STR_GRAPHICS     0xb6aaecf7u   /* CUSTOMIZE_TITLE_PAINT_SHOP "Graphics Shop" */
#define STR_SPECIALTIES  0xdf86227au   /* CUSTOMIZE_TITLE_INTERIORS_SHOP */

/* FEng object hashes the code fills in (shared by all three packages). */
#define O_TITLE        0x42adb44cu   /* header "Car Shop" */
#define O_OPTION_A     0x5e7b09c9u   /* option title (category packages) */
#define O_OPTION_B     0x6c9e933eu   /* option title (parts browser) */
#define O_CASH         0xeb3a688au
#define O_PLAYER       0xeb406fecu
#define O_HELP_LABEL   0x00169268u
#define O_HELP_TOKEN   0xa314f2bau
#define O_SELECT_TOKEN 0x89782734u
#define O_PART_NAME    0xd28b9316u
#define O_PART_PRICE   0x0380b632u
#define O_TOTAL        0x2f5e7da3u
#define O_LOCK         0xa2f11b13u
#define O_SCROLL_BAR   0x688a1551u
#define O_SCROLL_TAB   0x3fbb7a4eu
#define O_SLOT_A       0xb04669e3u   /* centre icon slot (category packages) */
#define O_SLOT_B       0xef8bb088u   /* centre icon slot (parts browser) */
#define O_MINI_BACK    0xd82b4a08u   /* part list: selected mini icon backing */
#define O_MINI_TAB     0xf31e8ed3u   /* part list slider tab */
#define O_MINI_UP      0x640f0f22u
#define O_MINI_DOWN    0x640f0f23u
#define K_MINI_ICON    0x0b114adau   /* U2_mini_icon.tga */
#define K_LOCK_ICON    0xcf5c1c91u   /* U2_OL_LockIcon.tga */

/* IconScroller (SPEED2.EXE 0x543a90 ctor, 0x51f870 layout, 0x52eea0/0x52f8c0
   placement, 0x538760 selected pulse, 0x51fd60 locked alpha). */
#define SCROLL_SPACING   40.0f   /* [+0x24] = 40.0 */
#define SCROLL_UNSEL     0.8f    /* unselected icons are drawn at 0.8 size */
#define SCROLL_EASE      0.3f    /* [+0xb4] */
#define PULSE_STEP       0.03f   /* phase += 0.03 per frame, triangle over 0..2 */
#define LOCKED_ALPHA     (150.0f / 255.0f)

/* Car part DB groups -> our N2CarConfig slots and the career unlock row
   that holds the price (GCareer 0x34A1C, 1-based index). */
static int group_slot(int g) {
    switch (g) {
    case G_FRONT_BUMPER: return N2_PART_FRONT;    case G_REAR_BUMPER: return N2_PART_REAR;
    case G_SKIRT: return N2_PART_SKIRT;           case G_HOOD: return N2_PART_HOOD;
    case G_HEADLIGHT: return N2_PART_HEADLIGHT;   case G_BRAKELIGHT: return N2_PART_TAILLIGHT;
    case G_SPOILER: return N2_PART_SPOILER;       case G_EXHAUST: return N2_PART_EXHAUST;
    case G_ROOF: return N2_PART_SCOOP;           case G_LEFT_MIRROR: return N2_PART_MIRROR;
    case G_WIDE_BODY: return N2_PART_WIDE;
    default: return -1;
    }
}
static int group_price_row(int g) {
    switch (g) {
    case G_HOOD: return 11; case G_SPOILER: return 12; case G_WHEEL: return 13;
    case G_BRAKELIGHT: return 14; case G_HEADLIGHT: return 15; case G_SKIRT: return 16;
    case G_REAR_BUMPER: return 17; case G_FRONT_BUMPER: return 18; case G_LEFT_MIRROR: return 19;
    case G_ROOF: return 20; case G_EXHAUST: return 21;
    default: return 0;
    }
}
static const char *group_name(int g) {   /* as in the part hash "<CAR>_KITnn_<NAME>" */
    switch (g) {
    case G_FRONT_BUMPER: return "FRONT_BUMPER"; case G_REAR_BUMPER: return "REAR_BUMPER";
    case G_SKIRT: return "SKIRT"; case G_HOOD: return "HOOD"; case G_HEADLIGHT: return "HEADLIGHT";
    case G_BRAKELIGHT: return "BRAKELIGHT"; case G_SPOILER: return "SPOILER";
    case G_EXHAUST: return "EXHAUST"; case G_ROOF: return "ROOF"; case G_LEFT_MIRROR: return "WING_MIRROR";
    case G_WIDE_BODY: return "WIDE_BODY";
    default: return NULL;
    }
}

/* ------------------------------------------------------------------ data */

typedef struct { uint32_t key; GLuint tex; int w, h; unsigned char additive; } ShopTex;
typedef struct { GLuint tex; int aw, ah, ok; FengFont f; } ShopFont;

typedef struct {
    char name[48];          /* displayed (the DB's debug name) */
    int  value;             /* N2CarConfig value; 0 = stock */
    int  level, price;
    int  available;         /* the renderer can fit it on this car */
    uint32_t hash;          /* the database part (vinyls) */
} ShopPart;
#define SHOP_MAX_PARTS 200

enum { SC_CAR_SHOP, SC_BODY, SC_PERFORMANCE, SC_GRAPHICS, SC_SPECIALTIES, SC_PARTS,
       SC_RIM_BRAND, SC_RIMS, SC_SPLASH, SC_MAIN, SC_LOADING,
       SC_QR_MODE, SC_QR_TRACK, SC_QR_OPTIONS, SC_QR_CAR, SC_OPT_MAIN, SC_OPT, SC_CTL, SC_JUKE, SC_CREDITS, SC_PAINT, SC_PERF_PKG,
       SC_VLAYER, SC_VCAT, SC_VLIST, SC_DLOC, SC_DPICK, SC_TINT, SC_BULB, SC_CARBON,
       SC_NEON, SC_NEON_LIST, SC_HUD, SC_HUD_COL, SC_SPEC, SC_ICE };

/* one settings record (0x83aac4 + type * 20, ctor 0x52a220) */
typedef struct { int opp, diff, traffic, catchup, ev, laps, dir, ko; } QrSet;
/* An image the code changes: its texture, place, size or alpha, or hidden. */
typedef struct { uint32_t hash; GLuint tex; int hide, geom; float x, y, w, h, alpha; uint32_t col; /* 0xAARRGGBB, 0 authored */ } ObjOv;

#define QR_NTYPE 11
#define QR_MAX_TRACKS 64
#define QR_MAX_CARS 40

typedef struct {               /* one horizontal icon scroller */
    const ShopEntry *e; int n, sel;
    unsigned char locked[32];
    float scroll, pulse;       /* scroll: current offset (FEng units) */
} Scroller;

struct Shop {
    char dataroot[512], car[32];
    /* screens */
    FengPackage *pk[PK_COUNT];
    short draw[PK_COUNT][FENG_MAX_OBJ]; int ndraw[PK_COUNT];
    float rgba[PK_COUNT][FENG_MAX_OBJ][4];   /* colour through the parent chain */
    signed char font_of[PK_COUNT][FENG_MAX_RES];
    GLuint res_tex[PK_COUNT][FENG_MAX_RES];
    ShopTex *tex; int ntex;
    ShopFont font[4]; int nfont;
    GLuint qb_vbo, qb_nbo, qb_ibo;
    GLuint prog; GLint u_color, u_usetex, u_tex;   /* the shop's own 2D program */
    /* strings */
    unsigned char *lang; long langlen;
    uint32_t *str_hash; const char **str_txt; int nstr;
    /* database */
    unsigned char *db; long dblen;           /* chunk 0x80034602 payload */
    long db_str, db_off, db_attr, db_models, db_parts, db_parts_len;
    int nmodels;
    short price[96][3];                      /* GCareer 0x34A1C rows, 1-based */
    short rep[96][3];                        /* ... their points (+0 of each level, 0x501860) */
    /* state */
    N2CarConfig fitted, preview;
    /* the graphics setup installed when the Graphics Shop opened (config B
       0x8392c0); the shop's paint / vinyl / decal screens change `fitted`
       (the workspace with the pending list 0x8389c8) until Install Setup */
    N2CarConfig gfx_base;
    N2PartMenu menus[N2_PART_COUNT];
    int screen;
    Scroller top, body, cat;                 /* top: Car Shop, body: Body Shop list */
    ShopPart parts[SHOP_MAX_PARTS]; int nparts, part_sel, part_group;
    float part_scroll;
    float debounce; int pending;             /* preview after the cursor rests */
    int exit_req;
    int gfx_cancel;                          /* GFX_* the Square dialog cancels */
    float dim;                               /* a dialog's DIM over the screen, 0..1 */
    /* the Visual Rating box: installed / preview ratings and what they were taken from */
    float rate_inst, rate_prev; int rate_ok;
    N2CarConfig rate_b, rate_a; char rate_rim[2][16]; int rate_style[2];
    unsigned char dim_done[PK_COUNT], dim_mask[PK_COUNT][FENG_MAX_OBJ / 8];
    /* rims: brand scroller over k_rims_tuner / k_rims_suv */
    const RimBrand *rb; int nrb;
    ShopEntry rim_entries[24];
    Scroller rims;
    char rim_folder[16]; int rim_style;      /* fitted (RIM_VALUE: style + size) */
    char prev_folder[16]; int prev_style;    /* shown */
    int rim_pending; float rim_debounce;
    /* a style's sizes for the car's OUTER_RADIUS, in database order (0x55f340
       groups them under one entry); the size list (0x55fb00, +0x214) */
    unsigned char rim_sizes[SHOP_MAX_PARTS][8], rim_nsizes[SHOP_MAX_PARTS];
    int size_mode, size_sel; float size_scroll;
    /* front end */
    Scroller main; int front_end, fe_action; float blink;
    char ls_line1[96], ls_line2[96], ls_line3[96];   /* PC_Loading text in place of "LOADING" (shop_loading_text) */
    float text_dx, text_dy;            /* offset applied by text() (second loading line) */
    /* quick race */
    const unsigned char *trk; int ntrk;          /* GLOBALB 0x34201 records */
    QrSet set[QR_NTYPE];
    int qr_type;                                 /* the mode picked */
    Scroller qr_mode, qr_track;
    ShopEntry qr_track_e[QR_MAX_TRACKS]; short qr_track_rec[QR_MAX_TRACKS];
    int qr_row[QR_ROWS_SHOWN], qr_nrow, qr_rsel; /* option rows (QO_*) and the cursor */
    FengAnim *qr_anim; int qr_anim_ms;           /* UI_QRModeOptions with its scripts */
    float map_wait; int map_ev, map_want; GLuint map_tex; float map_alpha;
    ShopCar cars[QR_MAX_CARS]; int ncars;
    short qr_car_list[QR_MAX_CARS]; int qr_ncar, qr_car_sel, qr_filter;
    char qr_car_cur[32];
    int qr_car_pending; float qr_car_debounce;
    int qr_go;
    ObjOv ov[16]; int nov;                       /* draw_package's image overrides */
    unsigned char qr_rowobj[FENG_MAX_OBJ];       /* options: 1 a row object, 2 an arrow */
    uint32_t row_arrow;                          /* the rows' arrow texture */
    /* options */
    Scroller opt_main; int opt_cat;
    int opt_row[OPT_MAX_ROWS], opt_nrow, opt_rsel, opt_top;
    FengAnim *opt_anim;
    int snd_done;                                /* the screen played its own sound */
    /* controller setup */
    FengAnim *ctl_anim;
    int ctl_sel, ctl_col, ctl_top;               /* ctl_sel -1: the device title */
    int ctl_cap;                                 /* 1 armed, 2 waiting for the button */
    uint32_t ctl_instr;                          /* Instructions_Label's string */
    /* EA Trax jukebox */
    FengAnim *juke_anim;
    int juke_sel, juke_top, juke_grab;           /* the focused playlist slot, the first shown, grabbed */
    /* paint shop */
    Scroller paintcat; FengAnim *paint_anim;
    /* custom gauges: UI_ChooseCustomHUD's scripts, the style's gauge art
       (loaded from GLOBAL/HUD_CustomTextures_NN.bin), the CUSTOM_HUD_PAINT
       colours (0x01RRGGBB, DB order), the layer being coloured (0 backing,
       1 dial, 2 needle) and the three picked so far */
    FengAnim *chud_anim; GLuint chud_tex[6]; int chud_style;
    unsigned hud_pal[12]; int hud_npal, hud_layer, hud_csel;
    unsigned hud_pick[3];
    int paint_mode;                              /* +0x188: 1 choosing a colour */
    uint32_t paint_brand;                        /* +0x174 */
    uint32_t paint_hash[60]; int paint_obj[60];  /* the swatches (+0x158) */
    int paint_n, paint_sel;
    /* vinyls: the layer strip, the category strip, the layer being changed;
       vmode >= 0 puts UI_Paint on that layer's colours (DAT_008389a0) */
    Scroller vlay, vcat; ShopEntry vlay_e[VINYL_LAYERS], vcat_e[N_VCAT], vcol_e[3];
    int vlayer, vmode;
    uint32_t vcol_open;                          /* the colour when the swatches opened */
    /* decals: the location menu's level (+0x154), the levels above it
       (+0x158), its strip; the slot being decorated, the layout asked for */
    Scroller dloc; int dlev, dstack[4], dsel[4], ndst;
    int dslot, dlay_loc, dlay_want, dlay_next;
    /* performance shop: a category's packages */
    Scroller perfpkg; FengAnim *perf_anim;
    /* Carbon Fiber: its strip (0x545d30), whether a parts list is the
       carbon one (0x545700's last argument) */
    Scroller cfm; ShopEntry cfm_e[6]; int cf_mode;
    int spec_slot;             /* SC_SPEC: the Specialties slot being browsed */
    int spec_open;             /* SC_SPEC: Square opened the hood / doors (0x568d30 0xc519bfc0) */
    int spec_ret;              /* SC_SPEC: where Back / Install return (the Specialties or SC_ICE) */
    Scroller icem; ShopEntry ice_e[12]; int ice_mode;   /* SC_ICE: UI_ICEMAIN's strip, mode 3 / 4 */
    Scroller neonm;            /* Car Specialties -> Neon's strip */
    int perf_cat;                                /* DAT_00838994 */
    int perf_shown;                              /* the contents shown (PackageFadeIn) */
    uint32_t perf_row_name[4], perf_row_price[4]; int perf_nrow;
#define BUY_MAX 12
    FengAnim *buy_anim;                          /* UI_BuyPerformanceParts */
    int buy_idx[BUY_MAX]; uint32_t buy_name[BUY_MAX];
    int buy_n, buy_sel, buy_top, buy_focus;
    char buy_txt[2][32];                         /* the torque / power gains */
    /* credits */
    FengAnim *cred_anim;
    char **cred_line; int cred_n, cred_cur;      /* the text's lines (wrapped), the last one rolled */
    int cred_ms, cred_loaded;                    /* time on the screen */
    struct { int line, t; } cred_roll[48]; int cred_nroll;
    /* a GenericDialog over the screen (0x558020): its package (-1 none),
       one or two buttons, the cursor, what its first button does */
    FengAnim *dlg_anim;
    int dlg_pk, dlg_nbtn, dlg_sel, dlg_act, dlg_act2;   /* act2: the second button's, when it does something */
    uint32_t dlg_b1, dlg_b2;
    char dlg_text[512];
};

static void make_program(Shop *s);
static void credits_free(Shop *s);
static void credits_enter(Shop *s);
static void credits_tick(Shop *s, int ms);

/* -------------------------------------------------------------- strings */

static void load_strings(Shop *s) {
    char path[600];
    snprintf(path, sizeof path, "%s/LANGUAGES/English.bin", s->dataroot);
    s->lang = n2_read_file(path, &s->langlen);
    if (!s->lang) return;
    for (long o = 0; o + 8 <= s->langlen; ) {
        uint32_t id = n2_u32(s->lang + o), sz = n2_u32(s->lang + o + 4);
        long p = o + 8;
        if (p + (long)sz > s->langlen) break;
        if (id == 0x00039000u && sz >= 16) {
            uint32_t n = n2_u32(s->lang + p + 4), toff = n2_u32(s->lang + p + 8);
            uint32_t soff = n2_u32(s->lang + p + 12);
            if ((long)toff + (long)n * 8 <= (long)sz && n < 20000) {
                s->str_hash = (uint32_t *)malloc(n * sizeof *s->str_hash);
                s->str_txt = (const char **)malloc(n * sizeof *s->str_txt);
                if (!s->str_hash || !s->str_txt) return;
                for (uint32_t i = 0; i < n; i++) {
                    s->str_hash[i] = n2_u32(s->lang + p + toff + i * 8);
                    uint32_t so = n2_u32(s->lang + p + toff + i * 8 + 4);
                    s->str_txt[i] = (long)soff + (long)so < (long)sz
                                  ? (const char *)s->lang + p + soff + so : "";
                }
                s->nstr = (int)n;
            }
            return;
        }
        o = p + sz;
    }
}

static const char *str_get(const Shop *s, uint32_t h) {
    for (int i = 0; i < s->nstr; i++) if (s->str_hash[i] == h) return s->str_txt[i];
    const char *p = options_port_str(h);
    return p ? p : "";
}

/* --------------------------------------------------------- part database */

static long find_chunk_in_file(FILE *f, uint32_t want, uint32_t *size) {
    unsigned char h[8];
    long o = 0;
    while (fseek(f, o, SEEK_SET) == 0 && fread(h, 1, 8, f) == 8) {
        uint32_t id = n2_u32(h), sz = n2_u32(h + 4);
        if (id == want) { *size = sz; return o + 8; }
        o += 8 + (long)sz;
    }
    return -1;
}

static void load_db(Shop *s) {
    char path[600];
    snprintf(path, sizeof path, "%s/GLOBAL/GLOBALB.BUN", s->dataroot);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("shop: %s missing -- no part database\n", path); return; }
    uint32_t sz = 0;
    long at = find_chunk_in_file(f, 0x80034602u, &sz);
    if (at >= 0 && sz < (8u << 20)) {
        s->db = (unsigned char *)malloc(sz);
        if (s->db && fseek(f, at, SEEK_SET) == 0 && fread(s->db, 1, sz, f) == sz) s->dblen = sz;
        else { free(s->db); s->db = NULL; }
    }
    at = find_chunk_in_file(f, 0x80034A10u, &sz);
    if (at >= 0 && sz < (4u << 20)) {
        unsigned char *c = (unsigned char *)malloc(sz);
        if (c && fseek(f, at, SEEK_SET) == 0 && fread(c, 1, sz, f) == sz) {
            for (long o = 0; o + 8 <= (long)sz; ) {
                uint32_t id = n2_u32(c + o), cs = n2_u32(c + o + 4);
                if (o + 8 + (long)cs > (long)sz) break;
                if (id == 0x00034A1Cu)
                    for (long r = o + 8; r + 0x28 <= o + 8 + (long)cs; r += 0x28) {
                        int idx = (int)n2_u32(c + r);
                        if (idx <= 0 || idx >= 96) continue;
                        for (int l = 0; l < 3; l++) {
                            short pr; memcpy(&pr, c + r + 4 + 12 * l + 2, 2);
                            s->price[idx][l] = pr;
                            memcpy(&pr, c + r + 4 + 12 * l, 2);
                            s->rep[idx][l] = pr;
                        }
                    }
                o += 8 + cs;
            }
        }
        free(c);
    }
    fclose(f);
    if (!s->db) { printf("shop: car part database not found\n"); return; }
    for (long o = 0; o + 8 <= s->dblen; ) {
        uint32_t id = n2_u32(s->db + o), cs = n2_u32(s->db + o + 4);
        long p = o + 8;
        if (p + (long)cs > s->dblen) break;
        switch (id) {
        case 0x00034606u: s->db_str = p; break;
        case 0x0003460Cu: s->db_off = p; break;
        case 0x00034605u: s->db_attr = p; break;
        case 0x0003460Bu: s->db_models = p; s->nmodels = (int)(cs / 4); break;
        case 0x00034604u: s->db_parts = p; s->db_parts_len = cs; break;
        }
        o = p + cs;
    }
    printf("shop: part database %ld bytes, %d models, %ld part records\n",
           s->dblen, s->nmodels, s->db_parts_len / 14);
}

static int db_model(const Shop *s, const char *name) {
    uint32_t h = feng_hash(name);
    for (int i = 0; i < s->nmodels; i++)
        if (n2_u32(s->db + s->db_models + 4L * i) == h) return i;
    return -1;
}

/* 1 when the part's attribute list holds `key` (any value). */
static int db_has_attr(const Shop *s, unsigned aoff, uint32_t key) {
    if (aoff == 0xffffu || !s->db_off || !s->db_attr) return 0;
    const unsigned char *p = s->db + s->db_off + aoff * 2L;
    unsigned n = p[0] | p[1] << 8;
    for (unsigned k = 0; k < n && k < 64; k++) {
        unsigned ai = p[2 + 2 * k] | p[3 + 2 * k] << 8;
        if (n2_u32(s->db + s->db_attr + ai * 8L) == key) return 1;
    }
    return 0;
}

/* Our N2CarConfig value for a part hash: the car's own KITnn/STYLEnn meshes,
   or a shared library style (SPOILER/SPOILER_HATCH/SPOILER_SUV/EXHAUST/ROOF,
   encoded lib*1000 + variant*100 + style as n2_mod_catalog does). -1 = none. */
static int part_value(const char *car, int group, uint32_t hash) {
    const char *g = group_name(group);
    char nm[96];
    if (group == G_WIDE_BODY) {   /* "<CAR>_KITWnn_BODY"; KITW00 = no wide body */
        for (int nn = 0; nn < 10; nn++) {
            snprintf(nm, sizeof nm, "%s_KITW%02d_BODY", car, nn);
            if (feng_hash(nm) == hash) return nn ? 1 + nn : 0;
        }
        return -1;
    }
    if (!g) return -1;
    for (int kind = 0; kind < 2; kind++)
        for (int nn = 0; nn < 60; nn++) {
            snprintf(nm, sizeof nm, "%s_%s%02d_%s", car, kind ? "STYLE" : "KIT", nn, g);
            if (feng_hash(nm) == hash) return kind ? 101 + nn : (nn ? 1 + nn : 0);
            if (kind && group == G_HOOD && feng_hash(strcat(nm, "_CF")) == hash) return 101 + nn;   /* the same hood, carbon */
        }
    if (group == G_LEFT_MIRROR) {
        static const char *const mt[] = { "BODY", "POST", "SUV", "HUMMER" };
        for (int l = 0; l < 4; l++)
            for (int nn = 1; nn < 60; nn++) {
                snprintf(nm, sizeof nm, "STYLE%02d_MIRROR_%s", nn, mt[l]);
                if (feng_hash(nm) == hash) return (6 + l) * 1000 + nn;
                if (feng_hash(strcat(nm, "_CF")) == hash) return (6 + l) * 1000 + 500 + nn;
            }
        return -1;
    }
    static const char *const lib[] = { "", "SPOILER", "SPOILER_HATCH", "SPOILER_SUV", "EXHAUST", "ROOF" };
    static const char *const var[] = { "", "_DUAL", "_OFFSET" };
    for (int l = 1; l <= 5; l++)
        for (int nn = 1; nn < 60; nn++)
            for (int v = 0; v < 3; v++) {
                if (l == 4) {
                    if (v) break;
                    for (int lv = 1; lv <= 3; lv++) {
                        snprintf(nm, sizeof nm, "EXHAUST_STYLE%02d_LEVEL%d", nn, lv);
                        if (feng_hash(nm) == hash) return 4000 + nn;
                    }
                    continue;
                }
                if (l != 5 && v) break;
                snprintf(nm, sizeof nm, "%s_STYLE%02d%s", lib[l], nn, var[v]);
                if (feng_hash(nm) == hash) return l * 1000 + v * 100 + nn;
                if (feng_hash(strcat(nm, "_CF")) == hash) return l * 1000 + 500 + v * 100 + nn;
            }
    return -1;
}

static int menu_has(const N2PartMenu *m, int value) {
    for (int i = 0; i < m->count; i++) if (m->options[i].value == value) return 1;
    return 0;
}

/* The part list for one Body Shop group, in database order: the car's own
   parts, then the shared libraries. Parts the game excludes from UG2
   (EXCLUDE_UG2) are left out; parts we cannot fit stay listed but locked. */
static void build_parts(Shop *s, int group) {
    s->nparts = 0; s->part_sel = 0; s->part_group = group;
    int slot = group_slot(group), row = group_price_row(group);
    if (!s->db || !s->db_parts) return;
    static const char *const libs[] = { NULL, "SPOILER", "SPOILER_HATCH", "SPOILER_SUV", "EXHAUST", "ROOF",
                                        "MIRRORS_BODY", "MIRRORS_POST", "MIRRORS_SUV", "MIRRORS_HUMMER" };
    int models[6], nm = 0, hidden = 0;
    int suv = !strcmp(s->car, "ESCALADE") || !strcmp(s->car, "HUMMER") || !strcmp(s->car, "NAVIGATOR");
    models[nm++] = db_model(s, s->car);
    for (int l = 1; l <= 5; l++) {
        if ((group == G_SPOILER && l <= 3) || (group == G_EXHAUST && l == 4) ||
            (group == G_ROOF && l == 5))
            models[nm++] = db_model(s, libs[l]);
    }
    if (group == G_LEFT_MIRROR) models[nm++] = db_model(s, libs[n2_mirror_lib(s->car)]);   /* 0x6101e0 */
    for (int mi = 0; mi < nm; mi++) {
        if (models[mi] < 0) continue;
        for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
            const unsigned char *r = s->db + p;
            uint32_t hash = n2_u32(r);
            int grp = r[4], upg = r[5] | r[6] << 8, model = r[7];
            unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
            if (!hash && !soff) break;
            if (model != models[mi] || grp != group) continue;
            if (db_has_attr(s, aoff, 0xF7934316u)) continue;      /* EXCLUDE_UG2 */
            if (db_has_attr(s, aoff, 0x721AFF7Cu) != (s->cf_mode != 0)) continue;   /* CARBONFIBRE: the Carbon Fiber menu */
            if (group == G_HOOD && (upg & 0x1f) == 5) continue;   /* split hoods: Car Specialties (0x539960) */
            if (suv && db_has_attr(s, aoff, 0xF7933C86u)) continue;   /* EXCLUDE_SUV */
            if (s->nparts >= SHOP_MAX_PARTS) break;
            ShopPart *q = &s->parts[s->nparts];
            const char *nmstr = s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?";
            snprintf(q->name, sizeof q->name, "%s", nmstr);
            q->value = part_value(s->car, group, hash);
            /* a library's "none" entry (e.g. NO ROOF SCOOP) is the stock look */
            if (q->value < 0 && mi > 0 && (upg & 0xff) == 0) q->value = 0;
            int lvl = (upg >> 5) & 7;
            q->level = lvl > 3 ? 3 : lvl;
            q->price = q->level > 0 && row > 0 ? s->price[row][q->level - 1] : 0;
            if (q->price < 0) q->price = 0;
            q->available = q->value == 0 || (q->value > 0 && slot >= 0 &&
                                             menu_has(&s->menus[slot], q->value));
            if (!q->available) { hidden++; continue; }   /* no geometry on this car */
            /* one stock entry is enough */
            if (q->value == 0) {
                int dup = 0;
                for (int k = 0; k < s->nparts; k++) if (s->parts[k].value == 0) dup = 1;
                if (dup) continue;
            }
            s->nparts++;
        }
    }
    /* start on what is fitted */
    int cur = slot >= 0 ? s->fitted.parts[slot] : 0;
    if (group == G_HOOD && (s->fitted.carbon & 1) != (s->cf_mode ? 1u : 0u)) cur = -1;   /* the other kind of hood */
    for (int k = 0; k < s->nparts; k++) if (s->parts[k].value == cur) s->part_sel = k;
    s->part_scroll = (float)s->part_sel;
    printf("shop: %s on %s: %d parts (%d database entries without geometry here)\n",
           group_name(group) ? group_name(group) : "?", s->car, s->nparts, hidden);
}

/* bStringHash continued over more characters (h*33 + c). */
static uint32_t hash_more(uint32_t h, const char *t) {
    for (; *t; t++) h = h * 33u + (unsigned char)*t;
    return h;
}

/* A WHEELS part's name hash -> <folder>_STYLEnn_<inner>_<outer> (the data has
   inner 15..26, outer 23..34): every name of a folder hashed once, sorted. */
#define RIM_IN_LO 13
#define RIM_IN_HI 28
#define RIM_OUT_LO 15
#define RIM_OUT_HI 36
typedef struct { uint32_t hash; unsigned char style, inner, outer; } RimName;
static int rim_name_cmp(const void *a, const void *b) {
    uint32_t x = ((const RimName *)a)->hash, y = ((const RimName *)b)->hash;
    return x < y ? -1 : x > y;
}
static int rim_part_parse(const char *folder, uint32_t hash, int *style, int *inner, int *outer) {
    static char cf[16]; static RimName *tab; static int n;
    if (strcmp(cf, folder) || !tab) {
        free(tab); n = 0;
        tab = (RimName *)malloc(39 * (RIM_IN_HI - RIM_IN_LO + 1) * (RIM_OUT_HI - RIM_OUT_LO + 1) * sizeof *tab);
        if (!tab) { cf[0] = 0; return 0; }
        snprintf(cf, sizeof cf, "%s", folder);
        char pre[48];
        for (int nn = 1; nn < 40; nn++) {
            snprintf(pre, sizeof pre, "%s_STYLE%02d_", folder, nn);
            uint32_t h0 = hash_more(0xffffffffu, pre);
            for (int a = RIM_IN_LO; a <= RIM_IN_HI; a++) {
                char t[16]; snprintf(t, sizeof t, "%d_", a);
                uint32_t h1 = hash_more(h0, t);
                for (int b = RIM_OUT_LO; b <= RIM_OUT_HI; b++) {
                    char u[8]; snprintf(u, sizeof u, "%d", b);
                    tab[n++] = (RimName){ hash_more(h1, u), (unsigned char)nn, (unsigned char)a, (unsigned char)b };
                }
            }
        }
        qsort(tab, n, sizeof *tab, rim_name_cmp);
    }
    RimName key = { hash, 0, 0, 0 };
    const RimName *r = (const RimName *)bsearch(&key, tab, n, sizeof *tab, rim_name_cmp);
    if (!r) return 0;
    *style = r->style; *inner = r->inner; *outer = r->outer;
    return 1;
}

static unsigned char g_shop_rec[N2_REC_SIZE]; static int g_shop_rec_ok;   /* the car record (shop_set_car_rec) */

/* One entry per rim style of a brand, in database order, named and priced
   as the game does (0x55f340): the parts of the brand whose OUTER_RADIUS is
   the car's (record +0xdc, 0x539b70), parts of one style grouped as one
   entry -- each style ships in several sizes (<FOLDER>_STYLEnn_<inner>_<outer>),
   kept in rim_sizes for the size list. */
static void build_rims(Shop *s, int b) {
    s->nparts = 0; s->part_sel = 0; s->part_group = G_WHEEL; s->size_mode = 0;
    const RimBrand *r = &s->rb[b];
    if (!r->folder) {                        /* Stock */
        ShopPart *q = &s->parts[s->nparts++];
        snprintf(q->name, sizeof q->name, "%s", str_get(s, 0x25b9eb23u));   /* KIT00_FRONT_WHEEL */
        q->value = STOCK_RIM_STYLE; q->level = 0; q->price = 0; q->available = 1;
        s->rim_nsizes[0] = 0;
        return;
    }
    int model = db_model(s, "WHEELS");
    if (!s->db || model < 0) return;
    int rim_outer = g_shop_rec_ok ? (signed char)g_shop_rec[0xdc] : 0;
    for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
        const unsigned char *rec = s->db + p;
        uint32_t hash = n2_u32(rec);
        int grp = rec[4], upg = rec[5] | rec[6] << 8, mdl = rec[7];
        unsigned soff = rec[8] | rec[9] << 8, aoff = rec[10] | rec[11] << 8;
        if (!hash && !soff) break;
        if (mdl != model || grp != G_WHEEL) continue;
        /* BRAND_NAME */
        uint32_t brand = 0;
        if (aoff != 0xffffu && s->db_off && s->db_attr) {
            const unsigned char *o = s->db + s->db_off + aoff * 2L;
            unsigned n = o[0] | o[1] << 8;
            for (unsigned k = 0; k < n && k < 64; k++) {
                unsigned ai = o[2 + 2 * k] | o[3 + 2 * k] << 8;
                if (n2_u32(s->db + s->db_attr + ai * 8L) == 0xEBB03E66u)
                    brand = n2_u32(s->db + s->db_attr + ai * 8L + 4);
            }
        }
        if (brand != r->key || db_has_attr(s, aoff, 0xF7934316u)) continue;
        int style, inner, outer;
        if (!rim_part_parse(r->folder, hash, &style, &inner, &outer)) continue;
        if (rim_outer && outer != rim_outer) continue;
        int dup = -1;
        for (int k = 0; k < s->nparts; k++) if (s->parts[k].value == style) dup = k;
        if (dup >= 0) {
            if (s->rim_nsizes[dup] < 8) s->rim_sizes[dup][s->rim_nsizes[dup]++] = (unsigned char)inner;
            continue;
        }
        if (s->nparts >= SHOP_MAX_PARTS) continue;
        s->rim_sizes[s->nparts][0] = (unsigned char)inner; s->rim_nsizes[s->nparts] = 1;
        ShopPart *q = &s->parts[s->nparts++];
        snprintf(q->name, sizeof q->name, "%s", s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?");
        q->value = style;
        int lvl = (upg >> 5) & 7; q->level = lvl > 3 ? 3 : lvl;
        q->price = q->level > 0 ? s->price[13][q->level - 1] : 0;
        if (q->price < 0) q->price = 0;
        q->available = 1;
    }
    if (!strcmp(s->rim_folder, r->folder))
        for (int k = 0; k < s->nparts; k++) if (s->parts[k].value == RIM_STYLE(s->rim_style)) s->part_sel = k;
    s->part_scroll = (float)s->part_sel;
    printf("shop: rims %s: %d styles for outer %d\n", r->folder, s->nparts, rim_outer);
}

/* What the rim browser puts on the car (0x567a50 / 0x55f8f0): in the size
   list the size under the cursor; on a style the fitted part when it is that
   style, else the style's first size. */
static void rim_preview(Shop *s, float debounce) {
    if (!s->nparts) return;
    const RimBrand *r = &s->rb[s->rims.sel];
    const char *folder = r->folder ? r->folder : STOCK_RIM_FOLDER;
    int st = s->parts[s->part_sel].value, v = st;
    if (s->rim_nsizes[s->part_sel]) {
        if (s->size_mode) v = RIM_VALUE(st, s->rim_sizes[s->part_sel][s->size_sel]);
        else if (!strcmp(s->rim_folder, folder) && RIM_STYLE(s->rim_style) == st) v = s->rim_style;
        else v = RIM_VALUE(st, s->rim_sizes[s->part_sel][0]);
    }
    if (!strcmp(s->prev_folder, folder) && s->prev_style == v) return;
    snprintf(s->prev_folder, sizeof s->prev_folder, "%s", folder);
    s->prev_style = v; s->rim_pending = 1; s->rim_debounce = debounce;
}

/* ------------------------------------------------------------ cache build */

/* FEn package payload of chunk (`d` bundle) whose PkHd name matches. */
static unsigned char *extract_package(const unsigned char *d, long len, const char *name, long *outlen) {
    for (long o = 0; o + 8 <= len; ) {
        uint32_t id = n2_u32(d + o), sz = n2_u32(d + o + 4);
        long body = o + 8, bend = body + (long)sz;
        if (bend > len) break;
        if (id == 0x00030203u || id == 0x00030210u) {
            unsigned char *pk = NULL; long pklen = 0;
            if (id == 0x00030210u) {
                for (long j = body; j + 16 <= bend; j++)
                    if (!memcmp(d + j, "JDLZ", 4)) {
                        long ulen = (long)n2_u32(d + j + 8);
                        pk = (unsigned char *)malloc((size_t)ulen);
                        if (pk) pklen = n2_jdlz(d + j, (int)(bend - j), pk, (int)ulen);
                        break;
                    }
            } else {
                pk = (unsigned char *)malloc(sz);
                if (pk) { memcpy(pk, d + body, sz); pklen = sz; }
            }
            for (long j = 0; pk && j + 0x40 < pklen && j < 128; j++)
                if (!memcmp(pk + j, "PkHd", 4)) {
                    const char *pn = (const char *)pk + j + 8 + 0x18;
                    if (!strncmp(pn, name, 40)) { *outlen = pklen; return pk; }
                    break;
                }
            free(pk);
        }
        o = bend;
    }
    return NULL;
}

/* Every icon key the menus show. */
static int icon_keys(uint32_t *k, int cap) {
    int n = 0;
    const ShopEntry *lists[] = { k_car_shop, k_body_shop, k_perf_shop, k_graphics_shop, k_special_shop };
    int counts[] = { CAT_COUNT, N_BODY, NELEM(k_perf_shop), NELEM(k_graphics_shop), NELEM(k_special_shop) };
    for (int l = 0; l < 5; l++)
        for (int i = 0; i < counts[l] && n < cap; i++) k[n++] = lists[l][i].tex;
    for (int i = 0; i < NELEM(k_rims_tuner) && n < cap; i++) k[n++] = k_rims_tuner[i].tex;
    for (int i = 0; i < NELEM(k_rims_suv) && n < cap; i++) k[n++] = k_rims_suv[i].tex;
    for (int i = 0; i < NELEM(k_main_menu) && n < cap; i++) k[n++] = k_main_menu[i].tex;
    for (int i = 0; i < NELEM(k_opt_main) && n < cap; i++) k[n++] = k_opt_main[i].tex;
    for (int i = 0; i < NELEM(k_paint_cat) && n < cap; i++) k[n++] = k_paint_cat[i].tex;
    for (int i = 0; i < NELEM(k_perf_pkg) && n < cap; i++) k[n++] = k_perf_pkg[i].tex;
    for (int i = 0; i < N_VCAT && n < cap; i++) k[n++] = k_vinyl_cat[i].tex;
    for (int i = 0; i < VINYL_LAYERS && n < cap; i++) k[n++] = 0x27b8f0ddu + (uint32_t)i;   /* VINYL_LAYER_1..4 */
    for (int l = 0; l < NELEM(k_decal_level); l++)
        for (int i = 0; i < k_decal_level[l].n && n < cap; i++) k[n++] = k_decal_level[l].e[i].tex;
    if (n < cap) k[n++] = 0x73c4ec2du;   /* GENERIC_ICON_PARTS */
    static const uint32_t cf_icons[] = { 0xf7363420u, 0xc43176d4u, 0x643bde3cu, 0x10b5e8d1u,   /* Carbon Fiber */
                                         0xf8111a17u, 0x8f5dc19fu, 0x246de0cbu, 0x2bdb0653u };
    for (int i = 0; i < NELEM(cf_icons) && n < cap; i++) k[n++] = cf_icons[i];
    for (int i = 0; i < NELEM(k_neon_shop) && n < cap; i++) k[n++] = k_neon_shop[i].tex;
    for (int i = 0; i < NELEM(k_ice_mode3) && n < cap; i++) k[n++] = k_ice_mode3[i].tex;
    if (n < cap) k[n++] = ICE_SLOT_ICON;
    for (int i = 1; i <= 3 && n < cap; i++) {
        char pi[24]; snprintf(pi, sizeof pi, "PAINT_ICON_%d", i);
        k[n++] = feng_tex_key(pi);
    }
    /* quick race: mode icons, track icons, car logos and badges */
    for (int i = 0; i < NELEM(k_qr_modes) && n < cap; i++) k[n++] = k_qr_modes[i].tex;
    for (int i = 0; i < 5 && n < cap; i++) k[n++] = k_qr_area_icon[i];
    static const char *const ticon[] = { "Track_Icon_Drag", "Track_Icon_Drift",
                                         "Track_Icon_U_Racing_League", "Track_Icon_Short_Track" };
    for (int i = 0; i < 4 && n < cap; i++) k[n++] = feng_tex_key(ticon[i]);
    if (n + 4 <= cap) { k[n++] = K_TRACK_LOCKED; k[n++] = K_TRACK_DEFAULT; k[n++] = K_GENERIC_PLACEHOLDER; k[n++] = K_MENU_ARROW; }
    char nm[64];
    for (int i = 0; i < NELEM(k_qr_makes) && n < cap; i++) {
        snprintf(nm, sizeof nm, "CARSELECT_MANUFACTURER_%s", k_qr_makes[i]);
        k[n++] = feng_tex_key(nm);
    }
    for (int i = 0; i < NELEM(k_qr_badges) && n < cap; i++) {
        snprintf(nm, sizeof nm, "SECONDARY_LOGO_%s", k_qr_badges[i]);
        k[n++] = feng_tex_key(nm);
    }
    return n;
}

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

static int build_cache(const char *dataroot) {
    char path[600];
    snprintf(path, sizeof path, "%s/FRONTEND/FrontB.lzc", dataroot);
    long plen = 0;
    unsigned char *packed = n2_read_file(path, &plen);
    if (!packed || plen < 16 || memcmp(packed, "JDLZ", 4)) {
        free(packed); printf("shop: %s unreadable\n", path); return 0;
    }
    long flen = (long)n2_u32(packed + 8);
    unsigned char *fb = (unsigned char *)malloc((size_t)flen);
    if (fb) flen = n2_jdlz(packed, (int)plen, fb, (int)flen);
    free(packed);
    if (!fb || flen <= 0) { free(fb); return 0; }

    unsigned char *pkd[PK_COUNT] = {0}; long pkl[PK_COUNT] = {0};
    uint32_t keys[1024]; int nk = 0;   /* every package + icon texture (256 cut the last packages off) */
    /* GLOBALB holds the loading screens (PC_Loading) and SHOP_ICON_* art */
    snprintf(path, sizeof path, "%s/GLOBAL/GLOBALB.BUN", dataroot);
    long glen = 0;
    unsigned char *gb = n2_read_file(path, &glen);
    FengPackage *tmp = (FengPackage *)malloc(sizeof *tmp);
    for (int i = 0; i < PK_COUNT && tmp; i++) {
        pkd[i] = extract_package(fb, flen, k_pk_name[i], &pkl[i]);
        if (!pkd[i] && gb) pkd[i] = extract_package(gb, glen, k_pk_name[i], &pkl[i]);
        if (!pkd[i]) { printf("shop: %s not in FrontB or GLOBALB\n", k_pk_name[i]); continue; }
        /* one-chunk bundle so feng_load_package lists its resources */
        unsigned char *one = (unsigned char *)malloc((size_t)pkl[i] + 8);
        if (!one) continue;
        uint32_t cid = 0x00030203u, csz = (uint32_t)pkl[i];
        memcpy(one, &cid, 4); memcpy(one + 4, &csz, 4); memcpy(one + 8, pkd[i], (size_t)pkl[i]);
        if (feng_load_package(one, pkl[i] + 8, k_pk_name[i], tmp))
            for (int r = 0; r < tmp->nres && nk < 768; r++)
                if (tmp->res[r].type == 1) {
                    uint32_t key = feng_tex_key(tmp->res[r].name);
                    int dup = 0;
                    for (int q = 0; q < nk; q++) if (keys[q] == key) dup = 1;
                    if (!dup) keys[nk++] = key;
                }
        feng_free_package(tmp);
        free(one);
    }
    free(tmp);
    nk += icon_keys(keys + nk, 1024 - nk);
    FengImage *img = (FengImage *)calloc((size_t)nk, sizeof *img);
    if (!img) { free(fb); free(gb); for (int i = 0; i < PK_COUNT; i++) free(pkd[i]); return 0; }
    int got = feng_tpk_load(fb, flen, keys, nk, img);
    free(fb);
    if (got < nk && gb) got += feng_tpk_load(gb, glen, keys, nk, img);   /* SHOP_ICON_*, PC_load ... */
    free(gb);
    printf("shop: cache build: %d of %d textures\n", got, nk);
    FILE *f = fopen(SHOP_CACHE, "wb");
    int ok = 0;
    if (f) {
        fwrite("OUSHOP01", 1, 8, f);
        put32(f, PK_COUNT);
        for (int i = 0; i < PK_COUNT; i++) {
            put32(f, 0x00030203u); put32(f, (uint32_t)pkl[i]);
            if (pkl[i]) fwrite(pkd[i], 1, (size_t)pkl[i], f);
        }
        int nt = 0;
        for (int i = 0; i < nk; i++) if (img[i].rgba) nt++;
        put32(f, (uint32_t)nt);
        for (int i = 0; i < nk; i++) {
            if (!img[i].rgba) continue;
            put32(f, keys[i]); put32(f, (uint32_t)img[i].w | (uint32_t)img[i].h << 16);
            put32(f, img[i].blend);
            fwrite(img[i].rgba, 4, (size_t)img[i].w * img[i].h, f);
        }
        ok = fclose(f) == 0;
    }
    for (int i = 0; i < nk; i++) feng_image_free(&img[i]);
    free(img);
    for (int i = 0; i < PK_COUNT; i++) free(pkd[i]);
    return ok;
}

/* ------------------------------------------------------------ cache load */

static GLuint upload_rgba(int w, int h, const unsigned char *px) {
    GLuint id = 0; glGenTextures(1, &id);
    if (!id) return 0;
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return id;
}

static const ShopTex *tex_find(const Shop *s, uint32_t key) {
    for (int i = 0; i < s->ntex; i++) if (s->tex[i].key == key) return &s->tex[i];
    return NULL;
}

static int load_cache(Shop *s) {
    long len = 0;
    unsigned char *d = n2_read_file(SHOP_CACHE, &len);
    if (!d || len < 16 || memcmp(d, "OUSHOP01", 8)) { free(d); return 0; }
    long o = 8;
    uint32_t npk = n2_u32(d + o); o += 4;
    long bun = o;
    for (uint32_t i = 0; i < npk && o + 8 <= len; i++) o += 8 + (long)n2_u32(d + o + 4);
    long bunlen = o - bun;
    for (int i = 0; i < PK_COUNT; i++) {
        s->pk[i] = (FengPackage *)malloc(sizeof *s->pk[i]);
        if (s->pk[i] && !feng_load_package(d + bun, bunlen, k_pk_name[i], s->pk[i])) {
            free(s->pk[i]); s->pk[i] = NULL;
        }
    }
    uint32_t nt = o + 4 <= len ? n2_u32(d + o) : 0; o += 4;
    s->tex = (ShopTex *)calloc(nt ? nt : 1, sizeof *s->tex);
    for (uint32_t i = 0; s->tex && i < nt && o + 12 <= len; i++) {
        uint32_t key = n2_u32(d + o), wh = n2_u32(d + o + 4), bl = n2_u32(d + o + 8);
        int w = (int)(wh & 0xffff), h = (int)(wh >> 16);
        o += 12;
        if (o + (long)w * h * 4 > len) break;
        ShopTex *t = &s->tex[s->ntex++];
        t->key = key; t->w = w; t->h = h; t->additive = bl == 6;
        t->tex = upload_rgba(w, h, d + o);
        o += (long)w * h * 4;
    }
    free(d);
    return s->pk[PK_CATEGORY] && s->pk[PK_PARTS];
}

/* Fonts the packages name. The HUD fonts are in LANGUAGES/English.bin with
   atlases in LanguageTextures.bin; the front end's ConduitMdITC_TT21i is in
   GLOBAL/GLOBALA.BUN (FNTF chunk + its own atlas TPK). */
static void load_fonts(Shop *s) {
    char path[600];
    long glen = 0;
    snprintf(path, sizeof path, "%s/GLOBAL/GLOBALA.BUN", s->dataroot);
    unsigned char *globa = n2_read_file(path, &glen);
    for (int p = 0; p < PK_COUNT; p++) {
        memset(s->font_of[p], -1, sizeof s->font_of[p]);
        const FengPackage *pk = s->pk[p];
        if (!pk) continue;
        for (int r = 0; r < pk->nres; r++) {
            if (pk->res[r].type != 2) continue;
            int found = -1;
            char a[40]; int n = 0;   /* FengFont.name: lower case, no extension */
            const char *nm = pk->res[r].name;
            while (nm[n] && nm[n] != '.' && n < 39) { a[n] = (char)tolower((unsigned char)nm[n]); n++; }
            a[n] = 0;
            for (int f = 0; f < s->nfont; f++) if (!strcmp(a, s->font[f].f.name)) found = f;
            if (found < 0 && s->nfont < 4 &&
                ((s->lang && feng_load_font(s->lang, s->langlen, nm, &s->font[s->nfont].f)) ||
                 (globa && feng_load_font(globa, glen, nm, &s->font[s->nfont].f))))
                found = s->nfont++;
            if (found < 0) printf("shop: font %s not found\n", nm);
            s->font_of[p][r] = (signed char)found;
        }
    }
    if (s->nfont) {
        uint32_t keys[4]; FengImage img[4]; memset(img, 0, sizeof img);
        for (int f = 0; f < s->nfont; f++) keys[f] = s->font[f].f.atlas_key;
        if (globa) feng_tpk_load(globa, glen, keys, s->nfont, img);
        long alen = 0;
        snprintf(path, sizeof path, "%s/LANGUAGES/LanguageTextures.bin", s->dataroot);
        unsigned char *atlas = n2_read_file(path, &alen);
        if (atlas) { feng_tpk_load(atlas, alen, keys, s->nfont, img); free(atlas); }
        for (int f = 0; f < s->nfont; f++) {
            if (img[f].rgba) {
                s->font[f].tex = upload_rgba(img[f].w, img[f].h, img[f].rgba);
                s->font[f].aw = img[f].w; s->font[f].ah = img[f].h;
                s->font[f].ok = s->font[f].tex != 0;
            } else printf("shop: atlas for font %s not found\n", s->font[f].f.name);
            feng_image_free(&img[f]);
        }
    }
    free(globa);
}

/* A screen coming up: the package's FadeIn on every object that has one.
   Most objects' INIT chains to HIDE (alpha 0) -- the backings, the title and
   help bar groups -- and only FadeIn brings them in, as on a package push. */
static void anim_fadein(FengAnim *a) {
    uint32_t fi = feng_hash_upper("FadeIn");
    for (int i = 0; i < a->p->nobj; i++) feng_anim_set(a, i, fi, 1);
}

/* ------------------------------------------------------------- dialogs */

/* DialogInterface (0x558790 two buttons, 0x558430 one "OK"; both -> 0x558680
   / 0x558390 -> ShowDialog 0x558020) over the screen. A message with no
   package named gets one by its length (0x526c40): strlen x the
   ConduitMdITC_TT21i FNTF +0x24 (32) under 2561 GenericDialog_SMALL, under
   5122 _MED, else _LARGE. The layout (0x54c3d0): two buttons hide Button3
   and put the cursor on Button2; one button hides Button1 / Button2 and
   fades in Button3 with its text. The handler (0x54c050): Left / Right move
   between the buttons, accept sends the button's message, back the cancel
   message (every shop dialog's is its second button's). */
#define O_DLG_BLURB 0x1e2640fau          /* message_blurb */
#define O_DLG_BUTTON 0xb8a7c6ccu         /* Button1 .. Button3 */
#define O_DLG_TEXT 0xda473d2fu           /* BUTTON_TEXT_1 .. _3 */
#define STR_YES 0xf770u
#define STR_NO 0x61cu
#define STR_OK 0x639u
#define STR_CANCEL 0x541389e5u
#define DLG_AUTO (-1)
enum { DA_NONE, DA_PAINT_ADD, DA_PART, DA_RIM, DA_PERF_PKG, DA_PERF_STOCK, DA_PERF_PART, DA_CTL_RESET,
       DA_VINYL_ADD, DA_VINYL_CHANGE, DA_VINYL_REMOVE, DA_VINYL_UNIQUE,
       DA_DECAL_LAYOUT, DA_DECAL_ADD, DA_DECAL_REMOVE, DA_DECAL_CLEAR, DA_TINT, DA_BULB, DA_CF_TRUNK, DA_CF_KIT, DA_NEON,
       DA_HUD, DA_SPEC, DA_GFX_INSTALL, DA_GFX_LEAVE, DA_GFX_DISCARD, DA_GFX_CANCEL };
static void dlg_act(Shop *s, int act);

static int dlg_open(const Shop *s) { return s->dlg_pk >= 0; }
static void dlg_focus(Shop *s) {
    const FengPackage *pk = s->pk[s->dlg_pk];
    FengAnim *a = s->dlg_anim;
    int cur = s->dlg_nbtn == 1 ? 2 : s->dlg_sel;
    uint32_t hl = feng_hash_upper("Highlight"), un = feng_hash_upper("UnHighlight");
    for (int b = 0; b < 3; b++) {
        if (s->dlg_nbtn == 2 && b == 2) continue;
        if (s->dlg_nbtn == 1 && b < 2) continue;
        int o;
        if ((o = feng_find(pk, O_DLG_BUTTON + (uint32_t)b)) >= 0) feng_anim_set(a, o, b == cur ? hl : un, 1);
        if ((o = feng_find(pk, O_DLG_TEXT + (uint32_t)b)) >= 0) feng_anim_set(a, o, b == cur ? hl : un, 1);
    }
}
/* pk: PK_DLG_S / _M / _L, or DLG_AUTO; nbtn 2: Yes / No (b1 / b2 when
   given), 1: OK; act: what the first button does */
static void dialog(Shop *s, int pk, int nbtn, uint32_t msg, int act, uint32_t b1, uint32_t b2) {
    const char *t = str_get(s, msg);
    snprintf(s->dlg_text, sizeof s->dlg_text, "%s", t);
    if (pk == DLG_AUTO) {
        float m = (float)strlen(s->dlg_text) * 32.0f;
        pk = m < 2561.0f ? PK_DLG_S : m < 5122.0f ? PK_DLG_M : PK_DLG_L;
    }
    if (!s->pk[pk]) { printf("shop: %s not loaded\n", k_pk_name[pk]); dlg_act(s, act); return; }
    if (!s->dlg_anim) s->dlg_anim = (FengAnim *)malloc(sizeof *s->dlg_anim);
    if (!s->dlg_anim) return;
    FengAnim *a = s->dlg_anim;
    const FengPackage *p = s->pk[pk];
    feng_anim_init(a, p);
    anim_fadein(a);
    s->dlg_pk = pk; s->dlg_nbtn = nbtn; s->dlg_act = act; s->dlg_act2 = DA_NONE;
    s->dlg_b1 = nbtn == 1 ? STR_OK : b1 ? b1 : STR_YES; s->dlg_b2 = b2 ? b2 : STR_NO;
    s->dlg_sel = 1;
    int o;
    if (nbtn == 2) {
        if ((o = feng_find(p, O_DLG_BUTTON + 2)) >= 0) feng_anim_hide(a, o, 1);
        if ((o = feng_find(p, O_DLG_TEXT + 2)) >= 0) feng_anim_hide(a, o, 1);
    } else {
        for (int b = 0; b < 2; b++) {
            if ((o = feng_find(p, O_DLG_BUTTON + (uint32_t)b)) >= 0) feng_anim_hide(a, o, 1);
            if ((o = feng_find(p, O_DLG_TEXT + (uint32_t)b)) >= 0) feng_anim_hide(a, o, 1);
        }
    }
    dlg_focus(s);
    audio_fe_play(FE_SND_DIALOG); s->snd_done = 1;
    printf("shop: dialog %s \"%s\"\n", k_pk_name[pk], s->dlg_text);
}
static void dlg_input(Shop *s, int key) {
    s->snd_done = 1;
    if ((key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) && s->dlg_nbtn == 2) {
        int j = key == SHOP_KEY_LEFT ? 0 : 1;
        if (j == s->dlg_sel) return;
        s->dlg_sel = j;
        dlg_focus(s);
        audio_fe_play(key == SHOP_KEY_LEFT ? FE_SND_LEFT : FE_SND_RIGHT);
    } else if (key == SHOP_KEY_OK || key == SHOP_KEY_BACK || key == SHOP_KEY_TRI) {
        int yes = key == SHOP_KEY_OK && s->dlg_nbtn == 2 && s->dlg_sel == 0;
        audio_fe_play(key == SHOP_KEY_OK ? FE_SND_SELECT : FE_SND_BACK);
        int act = s->dlg_act;
        s->dlg_pk = -1;
        printf("shop: dialog answered %s\n", s->dlg_nbtn == 1 ? "OK" : yes ? "yes" : "no");
        if (yes) dlg_act(s, act);
        else if (key == SHOP_KEY_OK && s->dlg_nbtn == 2 && s->dlg_act2) dlg_act(s, s->dlg_act2);
    }
}

/* Painter's order, colours through the parent chain, textures per resource. */
static void prepare_package(Shop *s, int p) {
    const FengPackage *pk = s->pk[p];
    s->ndraw[p] = 0;
    if (!pk) return;
    for (int r = 0; r < pk->nres; r++) {
        const ShopTex *t = pk->res[r].type == 1 ? tex_find(s, feng_tex_key(pk->res[r].name)) : NULL;
        s->res_tex[p][r] = t ? t->tex : 0;
    }
    for (int i = 0; i < pk->nobj; i++) {
        const FengObj *o = &pk->obj[i];
        float c[4] = { o->rgba[0], o->rgba[1], o->rgba[2], o->rgba[3] };
        for (int q = o->parent; q >= 0; q = pk->obj[q].parent)
            for (int k = 0; k < 4; k++) c[k] *= pk->obj[q].rgba[k];
        memcpy(s->rgba[p][i], c, sizeof c);
        if (o->type == FENG_GROUP) continue;
        /* mode-specific objects (flag 0x80000000, also via a parent group)
           are shown only by scripts for other modes */
        int hidden = 0;
        for (int q = i; q >= 0; q = pk->obj[q].parent) if (pk->obj[q].flags & 0x80000000u) hidden = 1;
        if (o->hash == O_PRESS_START) { c[3] = 1.0f; memcpy(s->rgba[p][i], c, sizeof c); }
        /* PC_Loading's circles are authored invisible: the script fades
           them in turn, which draw_package does in code */
        if (p == PK_LOADING && o->hash >= 0x50d98304u && o->hash <= 0x50d98308u) {
            c[3] = 1.0f; memcpy(s->rgba[p][i], c, sizeof c);
        }
        if (hidden || c[3] <= 0.0f) continue;
        /* pools and templates the code clones sit far off screen */
        if (fabsf(o->ax) > 330.0f || fabsf(o->ay) > 250.0f) continue;
        int k = s->ndraw[p]++;
        while (k > 0 && pk->obj[s->draw[p][k - 1]].az < o->az) { s->draw[p][k] = s->draw[p][k - 1]; k--; }
        s->draw[p][k] = (short)i;
    }
}

/* ------------------------------------------------------------- lifecycle */

static void scroller_set(Scroller *sc, const ShopEntry *e, int n) {
    memset(sc, 0, sizeof *sc);
    sc->e = e; sc->n = n;
}

static void set_locks(Shop *s) {
    /* Car Shop: only the Body Shop has working parts so far */
    for (int i = 0; i < CAT_COUNT; i++) s->top.locked[i] = 0;   /* Specialties: Window Tint built, its other entries locked inside */
    for (int i = 0; i < N_BODY; i++)
        s->body.locked[i] = group_slot(k_body_shop[i].group) < 0 && k_body_shop[i].group != G_WHEEL &&
                            k_body_shop[i].group != G_CARBON;
}

Shop *shop_open(const char *dataroot, const char *car, const N2CarConfig *installed,
                const N2PartMenu menus[N2_PART_COUNT], const char *rim_folder, int rim_style) {
    Shop *s = (Shop *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->dlg_pk = -1; s->vmode = -1;
    snprintf(s->dataroot, sizeof s->dataroot, "%s", dataroot ? dataroot : ".");
    if (!load_cache(s)) {
        for (int i = 0; i < PK_COUNT; i++) { feng_free_package(s->pk[i]); free(s->pk[i]); s->pk[i] = NULL; }
        printf("shop: building %s (first use)\n", SHOP_CACHE);
        if (!build_cache(s->dataroot) || !load_cache(s)) {
            printf("shop: screens unavailable\n");
            shop_close(&s);
            return NULL;
        }
    }
    load_strings(s);
    load_fonts(s);
    for (int p = 0; p < PK_COUNT; p++) prepare_package(s, p);
    load_db(s);
    if (s->db) vinyl_db_load(s->db, s->dblen);
    /* glyph / sprite quad batch */
    {
        enum { QMAX = 64 };
        float nrm[QMAX * 4 * 3]; unsigned short idx[QMAX * 6];
        for (int i = 0; i < QMAX * 4; i++) { nrm[i*3] = 0; nrm[i*3+1] = 0; nrm[i*3+2] = 1; }
        for (int q = 0; q < QMAX; q++) {
            unsigned short b = (unsigned short)(q * 4);
            idx[q*6] = b; idx[q*6+1] = b + 1; idx[q*6+2] = b + 2;
            idx[q*6+3] = b; idx[q*6+4] = b + 2; idx[q*6+5] = b + 3;
        }
        glGenBuffers(1, &s->qb_vbo); glGenBuffers(1, &s->qb_nbo); glGenBuffers(1, &s->qb_ibo);
        glBindBuffer(GL_ARRAY_BUFFER, s->qb_nbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof nrm, nrm, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->qb_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, s->qb_vbo);
        glBufferData(GL_ARRAY_BUFFER, QMAX * 4 * 5 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    }
    make_program(s);
    scroller_set(&s->top, k_car_shop, CAT_COUNT);
    scroller_set(&s->body, k_body_shop, N_BODY);
    snprintf(s->rim_folder, sizeof s->rim_folder, "%s", rim_folder ? rim_folder : STOCK_RIM_FOLDER);
    s->rim_style = rim_style;
    memcpy(s->prev_folder, s->rim_folder, sizeof s->prev_folder); s->prev_style = s->rim_style;
    shop_set_car(s, car, installed, menus);
    s->screen = SC_CAR_SHOP;
    printf("shop: open (%d textures, %d fonts, %d strings)\n", s->ntex, s->nfont, s->nstr);
    return s;
}

/* The loading screen alone (PC_Loading, its textures and fonts): the whole
   front end (16 MB cache, the part database) took 2-3 s to open just to
   show it at every track switch. Read from the same cache, seeking past
   every other package's pixels; kept by the caller for the session. */
static int load_cache_loading(Shop *s) {
    FILE *f = fopen(SHOP_CACHE, "rb");
    if (!f) return 0;
    unsigned char h[12];
    int ok = 0;
    unsigned char *bun = NULL;
    if (fread(h, 1, 12, f) == 12 && !memcmp(h, "OUSHOP01", 8)) {
        uint32_t npk = n2_u32(h + 8);
        long at = 12, bunlen = 0;
        for (uint32_t i = 0; i < npk; i++) {   /* the packages' extent */
            unsigned char c[8];
            if (fseek(f, at + bunlen, SEEK_SET) || fread(c, 1, 8, f) != 8) break;
            bunlen += 8 + (long)n2_u32(c + 4);
        }
        bun = (unsigned char *)malloc((size_t)(bunlen ? bunlen : 1));
        if (bun && !fseek(f, at, SEEK_SET) && fread(bun, 1, (size_t)bunlen, f) == (size_t)bunlen) {
            s->pk[PK_LOADING] = (FengPackage *)malloc(sizeof *s->pk[PK_LOADING]);
            if (s->pk[PK_LOADING] && !feng_load_package(bun, bunlen, k_pk_name[PK_LOADING], s->pk[PK_LOADING])) {
                free(s->pk[PK_LOADING]); s->pk[PK_LOADING] = NULL;
            }
        }
        const FengPackage *pk = s->pk[PK_LOADING];
        unsigned char c[12];
        if (pk && !fseek(f, at + bunlen, SEEK_SET) && fread(c, 1, 4, f) == 4) {
            uint32_t nt = n2_u32(c);
            s->tex = (ShopTex *)calloc((size_t)pk->nres + 1, sizeof *s->tex);
            long o = at + bunlen + 4;
            for (uint32_t i = 0; s->tex && i < nt && !fseek(f, o, SEEK_SET) && fread(c, 1, 12, f) == 12; i++) {
                uint32_t key = n2_u32(c), wh = n2_u32(c + 4), bl = n2_u32(c + 8);
                int w = (int)(wh & 0xffff), ht = (int)(wh >> 16);
                long px = (long)w * ht * 4;
                o += 12 + px;
                int want = 0;
                for (int r = 0; r < pk->nres && !want; r++)
                    want = pk->res[r].type == 1 && feng_tex_key(pk->res[r].name) == key;
                if (!want || s->ntex >= pk->nres) continue;
                unsigned char *rgba = (unsigned char *)malloc((size_t)(px ? px : 1));
                if (rgba && fread(rgba, 1, (size_t)px, f) == (size_t)px) {
                    ShopTex *t = &s->tex[s->ntex++];
                    t->key = key; t->w = w; t->h = ht; t->additive = bl == 6;
                    t->tex = upload_rgba(w, ht, rgba);
                }
                free(rgba);
            }
            ok = 1;
        }
    }
    free(bun);
    fclose(f);
    return ok;
}
Shop *shop_open_loading(const char *dataroot) {
    Shop *s = (Shop *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->dlg_pk = -1; s->vmode = -1;
    snprintf(s->dataroot, sizeof s->dataroot, "%s", dataroot ? dataroot : ".");
    if (!load_cache_loading(s)) {
        /* no cache yet: the full front end builds it */
        Shop *full = shop_open(dataroot, "", NULL, NULL, NULL, 0);
        if (!full) { shop_close(&s); return NULL; }
        shop_close(&full);
        if (!load_cache_loading(s)) { shop_close(&s); return NULL; }
    }
    load_strings(s);
    load_fonts(s);
    prepare_package(s, PK_LOADING);
    {   /* glyph / sprite quad batch (as shop_open) */
        enum { QMAX = 64 };
        float nrm[QMAX * 4 * 3]; unsigned short idx[QMAX * 6];
        for (int i = 0; i < QMAX * 4; i++) { nrm[i*3] = 0; nrm[i*3+1] = 0; nrm[i*3+2] = 1; }
        for (int q = 0; q < QMAX; q++) {
            unsigned short b = (unsigned short)(q * 4);
            idx[q*6] = b; idx[q*6+1] = b + 1; idx[q*6+2] = b + 2;
            idx[q*6+3] = b; idx[q*6+4] = b + 2; idx[q*6+5] = b + 3;
        }
        glGenBuffers(1, &s->qb_vbo); glGenBuffers(1, &s->qb_nbo); glGenBuffers(1, &s->qb_ibo);
        glBindBuffer(GL_ARRAY_BUFFER, s->qb_nbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof nrm, nrm, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->qb_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, s->qb_vbo);
        glBufferData(GL_ARRAY_BUFFER, QMAX * 4 * 5 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    }
    make_program(s);
    s->front_end = 1; s->screen = SC_LOADING;
    printf("shop: loading screen only (%d textures, %d fonts, %d strings)\n", s->ntex, s->nfont, s->nstr);
    return s;
}

void shop_close(Shop **sp) {
    if (sp && *sp) { free((*sp)->opt_anim); (*sp)->opt_anim = NULL; free((*sp)->ctl_anim); (*sp)->ctl_anim = NULL;
                       free((*sp)->juke_anim); (*sp)->juke_anim = NULL;
                       credits_free(*sp); free((*sp)->cred_anim); (*sp)->cred_anim = NULL;
                       free((*sp)->paint_anim); (*sp)->paint_anim = NULL;
                       free((*sp)->chud_anim); (*sp)->chud_anim = NULL;
                       glDeleteTextures(6, (*sp)->chud_tex); memset((*sp)->chud_tex, 0, sizeof (*sp)->chud_tex);
                       free((*sp)->perf_anim); (*sp)->perf_anim = NULL;
                       free((*sp)->buy_anim); (*sp)->buy_anim = NULL;
                       free((*sp)->dlg_anim); (*sp)->dlg_anim = NULL; }
    if (!sp || !*sp) return;
    Shop *s = *sp;
    for (int i = 0; i < s->ntex; i++) if (s->tex[i].tex) glDeleteTextures(1, &s->tex[i].tex);
    for (int f = 0; f < s->nfont; f++) if (s->font[f].tex) glDeleteTextures(1, &s->font[f].tex);
    if (s->qb_vbo) glDeleteBuffers(1, &s->qb_vbo);
    if (s->qb_nbo) glDeleteBuffers(1, &s->qb_nbo);
    if (s->qb_ibo) glDeleteBuffers(1, &s->qb_ibo);
    /* s->prog is the process-wide program (make_program): never deleted */
    for (int i = 0; i < PK_COUNT; i++) { feng_free_package(s->pk[i]); free(s->pk[i]); }
    if (s->map_tex) glDeleteTextures(1, &s->map_tex);
    free(s->qr_anim);
    free(s->tex); free(s->lang); free(s->str_hash); free((void *)s->str_txt); free(s->db);
    free(s);
    *sp = NULL;
}

void shop_set_car(Shop *s, const char *car, const N2CarConfig *fitted,
                  const N2PartMenu menus[N2_PART_COUNT]) {
    if (!s) return;
    if (car) snprintf(s->car, sizeof s->car, "%s", car);
    {   /* SUVs get the game's other brand list */
        int suv = !strcmp(s->car, "ESCALADE") || !strcmp(s->car, "HUMMER") || !strcmp(s->car, "NAVIGATOR");
        s->rb = suv ? k_rims_suv : k_rims_tuner;
        s->nrb = suv ? NELEM(k_rims_suv) : NELEM(k_rims_tuner);
        for (int i = 0; i < s->nrb; i++) {
            s->rim_entries[i].str = s->rb[i].str; s->rim_entries[i].tex = s->rb[i].tex;
            s->rim_entries[i].group = i;
        }
        int sel = s->rims.sel;
        scroller_set(&s->rims, s->rim_entries, s->nrb);
        if (sel < s->nrb) s->rims.sel = sel;
        for (int i = 0; i < s->nrb; i++)
            if (s->rb[i].folder && !strcmp(s->rb[i].folder, s->rim_folder)) s->rims.sel = i;
        s->rims.scroll = (float)s->rims.sel;
    }
    if (fitted) { s->fitted = *fitted; s->preview = *fitted; }
    if (menus) memcpy(s->menus, menus, sizeof s->menus);
    set_locks(s);
    if (s->screen == SC_PARTS) {
        int sel = s->part_sel;
        build_parts(s, s->part_group);
        if (sel < s->nparts) s->part_sel = sel;
    }
}

/* Text on the loading screen in place of "LOADING" (NULL/"" = the authored
   word), and an optional second line under it, in the screen's own font. */
void shop_loading_text(Shop *s, const char *line1, const char *line2) {
    if (!s) return;
    snprintf(s->ls_line1, sizeof s->ls_line1, "%s", line1 ? line1 : "");
    snprintf(s->ls_line2, sizeof s->ls_line2, "%s", line2 ? line2 : "");
    s->ls_line3[0] = 0;
}

void shop_loading_text3(Shop *s, const char *line1, const char *line2, const char *line3) {
    if (!s) return;
    shop_loading_text(s, line1, line2);
    snprintf(s->ls_line3, sizeof s->ls_line3, "%s", line3 ? line3 : "");
}

/* The game's boot loading screen (PC_Loading) until shop_front_end. */
void shop_loading(Shop *s) {
    if (!s) return;
    s->front_end = 1; s->screen = SC_LOADING;
}

/* The quick race settings live in the profile (0x83aab4) for the session:
   kept here across front end openings. */
static QrSet g_qr_set[QR_NTYPE]; static int g_qr_set_ok, g_qr_mode_sel, g_qr_filter;
static void qr_defaults(Shop *s);

void shop_front_end(Shop *s) {
    if (!s) return;
    s->front_end = 1; s->screen = SC_SPLASH;
    scroller_set(&s->main, k_main_menu, NELEM(k_main_menu));
    for (int i = 0; i < s->main.n; i++) s->main.locked[i] = k_main_menu[i].group == MM_NONE;
    s->main.sel = 1; s->main.scroll = 1.0f;     /* Quick Race */
    scroller_set(&s->opt_main, k_opt_main, NELEM(k_opt_main));
    for (int i = 0; i < s->opt_main.n; i++)     /* not built yet: Display (PC video), Controls, Trax, Credits */
        s->opt_main.locked[i] = k_opt_main[i].group != OC_AUDIO && k_opt_main[i].group != OC_GAMEPLAY &&
                                k_opt_main[i].group != OC_PLAYER && k_opt_main[i].group != OC_CONTROLS &&
                                k_opt_main[i].group != OC_EATRAX && k_opt_main[i].group != OC_CREDITS;
    scroller_set(&s->qr_mode, k_qr_modes, NELEM(k_qr_modes));
    if (g_qr_set_ok) memcpy(s->set, g_qr_set, sizeof s->set);
    else qr_defaults(s);
    s->qr_mode.sel = g_qr_mode_sel; s->qr_mode.scroll = (float)g_qr_mode_sel;
    s->qr_filter = g_qr_filter;
}

void shop_main_menu(Shop *s) {
    if (!s) return;
    shop_front_end(s);
    s->screen = SC_MAIN;                        /* no title screen */
}

void shop_preview_result(Shop *s, const N2CarConfig *shown) {
    if (!s || !shown) return;
    s->preview = *shown;
}

/* ------------------------------------------------------------------ input */

static void request_preview(Shop *s) { s->pending = 1; s->debounce = 0.35f; }

static void parts_cursor(Shop *s, int step) {
    if (s->nparts <= 0) return;
    s->part_sel = (s->part_sel + step + s->nparts) % s->nparts;
    const ShopPart *q = &s->parts[s->part_sel];
    int slot = group_slot(s->part_group);
    N2CarConfig next = s->fitted;
    if (slot >= 0 && q->available) next.parts[slot] = q->value;
    if (slot == N2_PART_HOOD && q->available)   /* a carbon hood is the same hood, carbon */
        next.carbon = s->cf_mode ? next.carbon | 1u : next.carbon & ~1u;
    printf("shop: cursor on %s (value %d%s)\n", q->name, q->value, q->available ? "" : ", not fitted");
    if (memcmp(&next, &s->preview, sizeof next)) { s->preview = next; request_preview(s); }
}

static void open_parts(Shop *s) {
    int g = k_body_shop[s->body.sel].group;
    s->cf_mode = 0;
    build_parts(s, g);
    s->screen = SC_PARTS;
}

/* ---------------------------------------------------------------- carbon fibre */

static void scroller_move(Scroller *sc, int step);
/* Body Shop -> Carbon Fiber (0x545d30): the carbon hoods, spoilers, roof
   scoops and side mirrors (0x545700 with its carbon flag set: the parts
   with CARBONFIBRE), then Convert / Revert Trunk (TRUNK_CARBON, slot 0x60)
   and Carbon Fiber Body Kit / Revert (KIT_CARBON, slot 0x5d), named by
   what is fitted ("CARBON FIBRE NONE" = 0xc8d313ac). */
static void cf_menu(Shop *s, int keep) {
    int sel = keep ? s->cfm.sel : 0;
    static const ShopEntry e0[4] = {
        { 0xf7363420u, 0xf7363420u, G_HOOD }, { 0xc43176d4u, 0xc43176d4u, G_SPOILER },
        { 0x643bde3cu, 0x643bde3cu, G_ROOF }, { 0x10b5e8d1u, 0x10b5e8d1u, G_LEFT_MIRROR } };
    memcpy(s->cfm_e, e0, sizeof e0);
    s->cfm_e[4] = (s->fitted.carbon & 4) ? (ShopEntry){ 0x1036e38eu, 0x8f5dc19fu, G_CF_TRUNK }
                                         : (ShopEntry){ 0xf8111a17u, 0xf8111a17u, G_CF_TRUNK };
    s->cfm_e[5] = (s->fitted.carbon & 2) ? (ShopEntry){ 0xe170f115u, 0x2bdb0653u, G_CF_KIT }
                                         : (ShopEntry){ 0xb229fa7eu, 0x246de0cbu, G_CF_KIT };
    scroller_set(&s->cfm, s->cfm_e, 6);
    s->cfm.sel = sel;
    s->screen = SC_CARBON;
}
static void cf_open_parts(Shop *s) {
    s->cf_mode = 1;
    build_parts(s, s->cfm.e[s->cfm.sel].group);
    s->screen = SC_PARTS;
    parts_cursor(s, 0);   /* the fitted part may not be carbon: show the one under the cursor */
}
/* 0x566490: the kit can't go carbon over a vinyl that covers the panels
   (groups 0xe, 0xf, 0x15..0x1a; on a wide body every group but 0x14) */
static int cf_kit_blocked(const Shop *s) {
    int wide = s->fitted.parts[N2_PART_WIDE] > 1;
    for (int l = 0; l < VINYL_LAYERS; l++) {
        const VinylPart *v = s->fitted.vinyl[l] ? vinyl_find(s->fitted.vinyl[l]) : NULL;
        if (!v) continue;
        int g = v->group;
        if (wide ? g != 0x14 : (g == 0xe || g == 0xf || (g >= 0x15 && g <= 0x1a))) return 1;
    }
    return 0;
}
static void cf_input(Shop *s, int key) {
    if (key == SHOP_KEY_LEFT) scroller_move(&s->cfm, -1);
    else if (key == SHOP_KEY_RIGHT) scroller_move(&s->cfm, 1);
    else if (key == SHOP_KEY_OK) {
        int g = s->cfm.e[s->cfm.sel].group;
        if (g == G_CF_TRUNK)
            dialog(s, DLG_AUTO, 2, (s->fitted.carbon & 4) ? 0xa645a29eu : 0x2b950c36u, DA_CF_TRUNK, 0, 0);
        else if (g == G_CF_KIT) {
            if (s->fitted.carbon & 2) dialog(s, DLG_AUTO, 2, 0x660bdf52u, DA_CF_KIT, 0, 0);
            else if (cf_kit_blocked(s)) dialog(s, DLG_AUTO, 1, 0xee2c85bbu, DA_NONE, 0, 0);
            else dialog(s, DLG_AUTO, 2, 0x9e274daau, DA_CF_KIT, 0, 0);
        } else cf_open_parts(s);
    } else if (key == SHOP_KEY_BACK) s->screen = SC_BODY;
}
static void cf_toggle(Shop *s, unsigned bit) {
    s->fitted.carbon ^= bit;
    s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
    printf("shop: carbon fibre = %x\n", s->fitted.carbon);
    cf_menu(s, 1);
}

static void scroller_move(Scroller *sc, int step) {
    if (sc->n <= 0) return;
    sc->sel = (sc->sel + step + sc->n) % sc->n;
    sc->pulse = 0.0f;
}

/* ------------------------------------------------------------ quick race */

/* The track record's QR mask bit for a mode (0x4987c0). */
static int qr_bit(int type) {
    switch (type) {
    case QR_CIRCUIT: return 0;  case QR_SPRINT: return 2;  case QR_DRAG: return 1;
    case QR_DRIFT: return 3;    case QR_URL: return 13;    case QR_STREETX: return 11;
    default: return 4;          /* free run / outrun */
    }
}
static int rec_ev(const unsigned char *r) { return (int16_t)(r[0x8a] | r[0x8b] << 8); }
static const unsigned char *qr_rec(const Shop *s, int ev) {
    for (int i = 0; i < s->ntrk; i++) {
        const unsigned char *r = s->trk + (long)i * 0x128;
        if (rec_ev(r) == ev && !r[0x88]) return r;
    }
    return NULL;
}

/* 0x52a220: every mode's settings record at its defaults */
static void qr_defaults(Shop *s) {
    for (int t = 0; t < QR_NTYPE; t++) {
        QrSet *q = &s->set[t];
        q->opp = 3; q->diff = 1; q->traffic = 1; q->catchup = 1;
        q->ev = 0; q->laps = 3; q->dir = 0; q->ko = 0;
    }
}

/* The mode's name as the track select's "race type" shows it (0x4988c0) */
static uint32_t qr_type_str(int type) {
    if (type == QR_URL) return STR_URL_RACE;
    for (int i = 0; i < NELEM(k_qr_modes); i++) if (k_qr_modes[i].group == type) return k_qr_modes[i].str;
    return 0;
}

/* 0x4984b0: the track's icon */
static uint32_t qr_track_icon(int type, const unsigned char *r) {
    uint32_t a = n2_u32(r + 0x7c);
    if (a < 5) return k_qr_area_icon[a];
    if (a == 5) switch (type) {
        case QR_DRAG: return feng_tex_key("Track_Icon_Drag");
        case QR_DRIFT: return feng_tex_key("Track_Icon_Drift");
        case QR_URL: return feng_tex_key("Track_Icon_U_Racing_League");
        case QR_STREETX: return feng_tex_key("Track_Icon_Short_Track");
        }
    return K_TRACK_DEFAULT;
}

static const char *qr_track_name(const Shop *s, int ev, int reverse) {
    char l[40];
    snprintf(l, sizeof l, reverse ? "REVERSE_TRACK_NAME_%d" : "TRACK_NAME_%d", ev);
    return str_get(s, feng_hash(l));
}

static void qr_map_request(Shop *s) {
    int ev = s->qr_track.n ? s->qr_track_e[s->qr_track.sel].group : 0;
    if (ev == s->map_want) return;
    s->map_want = ev; s->map_wait = 0.3f;      /* shown once the cursor rests (0x4cdd40) */
}

/* 0x4cded0: the mode's tracks in database order (locked ones would follow;
   the port is unlock-all, 0x838464) */
static void qr_build_tracks(Shop *s) {
    int t = s->qr_type, bit = qr_bit(t), n = 0, sel = 0;
    for (int i = 0; i < s->ntrk && n < QR_MAX_TRACKS; i++) {
        const unsigned char *r = s->trk + (long)i * 0x128;
        if (r[0x88] || !(n2_u32(r + 0x94) & (1u << bit))) continue;
        s->qr_track_rec[n] = (short)i;
        s->qr_track_e[n].str = 0;
        s->qr_track_e[n].tex = qr_track_icon(t, r);
        s->qr_track_e[n].group = rec_ev(r);
        if (rec_ev(r) == s->set[t].ev) sel = n;
        n++;
    }
    scroller_set(&s->qr_track, s->qr_track_e, n);
    s->qr_track.sel = sel; s->qr_track.scroll = (float)sel;
    s->set[t].ev = n ? s->qr_track_e[sel].group : 0;      /* 0 = the first entry */
    qr_map_request(s);
    printf("quick race: mode %d, %d tracks\n", t, n);
}

/* Game Options rows per mode (0x4ccf60 circuit, 0x4cd2d0 sprint, 0x4cd530
   drag, 0x4cd7a0 drift, 0x4b47b0 URL / street X, 0x4b4a10 free run) */
static void qr_build_rows(Shop *s) {
    int t = s->qr_type, n = 0, ev = s->set[t].ev;
    const unsigned char *r = qr_rec(s, ev);
    int dh = r && n2_u32(r + 0x80) == 1;
#define QROW(x) do { if (n < QR_ROWS_SHOWN) s->qr_row[n++] = (x); } while (0)
    if (r && r[0x86] == 1) QROW(QO_DIR);
    switch (t) {
    case QR_CIRCUIT: QROW(QO_KO); QROW(QO_LAPS); QROW(QO_TRAFFIC); QROW(QO_OPP); QROW(QO_DIFF); QROW(QO_CATCHUP); break;
    case QR_SPRINT: QROW(QO_TRAFFIC); QROW(QO_OPP); QROW(QO_DIFF); QROW(QO_CATCHUP); break;
    case QR_DRAG: if (ev < 4211 || ev > 4223) QROW(QO_TRAFFIC);
                  QROW(QO_OPP); QROW(QO_DIFF); QROW(QO_CATCHUP); break;
    case QR_DRIFT: if (!dh) { QROW(QO_LAPS); QROW(QO_OPP); } QROW(QO_DIFF); if (dh) QROW(QO_TRAFFIC); break;
    case QR_URL: case QR_STREETX: QROW(QO_LAPS); QROW(QO_OPP); QROW(QO_DIFF); QROW(QO_CATCHUP); break;
    default: QROW(QO_TRAFFIC); break;
    }
#undef QROW
    s->qr_nrow = n; s->qr_rsel = 0;
    if (!(r && r[0x86] == 1)) s->set[t].dir = 0;
}

static int qr_row_disabled(const Shop *s, int row) {
    return row == QO_LAPS && s->set[s->qr_type].ko;        /* Lap KO fixes the laps */
}

static int qr_opp_max(const Shop *s) {
    if (s->qr_type == QR_URL) return 5;
    if (s->qr_type == QR_DRAG && s->set[QR_DRAG].ev == 4402) return 2;
    return 3;
}

static int wrapi(int v, int lo, int hi) { return v < lo ? hi : v > hi ? lo : v; }

/* left / right on a row: the values wrap */
static void qr_row_change(Shop *s, int row, int step) {
    QrSet *q = &s->set[s->qr_type];
    switch (row) {
    case QO_DIR: q->dir = !q->dir; break;
    case QO_KO: q->ko = !q->ko; if (q->ko) { q->laps = 3; q->opp = 3; } break;
    case QO_LAPS: q->laps = wrapi(q->laps + step, 1, 10); break;
    case QO_TRAFFIC: q->traffic = wrapi(q->traffic + step, 0, 3); break;
    case QO_OPP: q->opp = wrapi(q->opp + step, 0, qr_opp_max(s)); break;
    case QO_DIFF: q->diff = wrapi(q->diff + step, 0, 2); break;
    case QO_CATCHUP: q->catchup = !q->catchup; break;
    }
}

static void qr_row_text(const Shop *s, int row, char *b, int cap) {
    const QrSet *q = &s->set[s->qr_type];
    switch (row) {
    case QO_DIR: snprintf(b, cap, "%s", str_get(s, q->dir ? STR_REVERSE : STR_FORWARD)); break;
    case QO_KO: snprintf(b, cap, "%s", str_get(s, q->ko ? STR_ON : STR_OFF)); break;
    case QO_LAPS: snprintf(b, cap, "%d", q->laps); break;
    case QO_TRAFFIC: snprintf(b, cap, "%s", str_get(s, k_qo_traffic[q->traffic & 3])); break;
    case QO_OPP: snprintf(b, cap, "%d", q->opp); break;
    case QO_DIFF: snprintf(b, cap, "%s", str_get(s, k_qo_diff[q->diff > 2 ? 2 : q->diff])); break;
    case QO_CATCHUP: snprintf(b, cap, "%s", str_get(s, q->catchup ? STR_ON : STR_OFF)); break;
    default: b[0] = 0;
    }
}

/* the option list's pool objects of row n (1-based, "OPTION_NAME_%d" ...) */
enum { QP_NAME, QP_DATA, QP_LEFT, QP_RIGHT, QP_BACK, QP_N };
static int qr_pool(const FengPackage *pk, int part, int n) {
    static const char *const fmt[QP_N] = { "OPTION_NAME_%d", "OPTION_DATA_%d", "LEFT_ARROW_%d",
                                           "RIGHT_ARROW_%d", "OPTION_BACKING_%d" };
    char nm[32]; snprintf(nm, sizeof nm, fmt[part], n);
    return feng_find(pk, feng_hash_upper(nm));
}

/* Highlight / UnHighlight / Disable per row (0x51e270, 0x51e3e0, 0x51e550) */
static void qr_options_scripts(Shop *s, int restart) {
    FengAnim *a = s->qr_anim;
    const FengPackage *pk = s->pk[PK_QR_OPTIONS];
    if (!a || !pk) return;
    uint32_t hi = feng_hash_upper("Highlight"), un = feng_hash_upper("UnHighlight"),
             dis = feng_hash_upper("Disable");
    for (int k = 0; k < s->qr_nrow; k++) {
        uint32_t h = k == s->qr_rsel ? hi : qr_row_disabled(s, s->qr_row[k]) ? dis : un;
        for (int part = 0; part < QP_N; part++) {
            int o = qr_pool(pk, part, k + 1);
            if (o < 0) continue;
            if (!feng_anim_set(a, o, h, restart) && h == dis) feng_anim_set(a, o, un, restart);
        }
    }
}

/* 0x552780 / 0x54ef30 / 0x52d800 / 0x52dac0: row n from TITLE_MASTER's top
   left and DATA_MASTER (data centred in it), one TITLE_MASTER height apart;
   the arrows just outside DATA_MASTER (U2_Menu_Arrow), the backing at the
   name less (-276, -10). Unused pool rows are hidden. */
static void qr_options_layout(Shop *s) {
    const FengPackage *pk = s->pk[PK_QR_OPTIONS];
    if (!pk) return;
    if (!s->qr_anim) s->qr_anim = (FengAnim *)malloc(sizeof *s->qr_anim);
    FengAnim *a = s->qr_anim;
    if (!a) return;
    feng_anim_init(a, pk);
    anim_fadein(a);                                     /* backings/title come in on FadeIn */
    s->qr_anim_ms = 0;
    float tl = -272.0f, tt = -140.0f, step = QR_ROW_STEP, dcx = 126.0f, dt = -140.0f, dw = 200.0f;
    int tm = feng_find(pk, feng_hash_upper("TITLE_MASTER")), dm = feng_find(pk, feng_hash_upper("DATA_MASTER"));
    if (tm >= 0) {
        const FengObj *o = &pk->obj[tm];
        tl = o->ax - 0.5f * fabsf(o->size[0]); tt = o->ay - 0.5f * fabsf(o->size[1]); step = fabsf(o->size[1]);
    }
    if (dm >= 0) {
        const FengObj *o = &pk->obj[dm];
        dcx = o->ax; dt = o->ay - 0.5f * fabsf(o->size[1]); dw = fabsf(o->size[0]);
    }
    memset(s->qr_rowobj, 0, sizeof s->qr_rowobj);
    s->row_arrow = K_MENU_ARROW;
    for (int n = 1; n <= 12; n++) {
        int used = n <= s->qr_nrow;
        float y = (float)(n - 1) * step;
        for (int part = 0; part < QP_N; part++) {
            int o = qr_pool(pk, part, n);
            if (o < 0) continue;
            s->qr_rowobj[o] = part == QP_LEFT || part == QP_RIGHT ? 2 : 1;
            for (int c = o + 1; c < pk->nobj; c++)          /* a backing group's children */
                if (feng_inside(pk, c, o)) s->qr_rowobj[c] = 1;
            if (!used) { feng_anim_hide(a, o, 1); continue; }
            float aw = fabsf(pk->obj[o].size[0]), ah = fabsf(pk->obj[o].size[1]);
            switch (part) {
            case QP_NAME: feng_anim_pos(a, o, tl, tt + y); break;
            case QP_DATA: feng_anim_pos(a, o, dcx, dt + y); break;
            case QP_LEFT: feng_anim_pos(a, o, dcx - 0.5f * dw - 0.5f * aw, dt + y + 0.5f * ah); break;
            case QP_RIGHT: feng_anim_pos(a, o, dcx + 0.5f * dw + 0.5f * aw, dt + y + 0.5f * ah); break;
            case QP_BACK: feng_anim_pos(a, o, tl + 276.0f, tt + y + 10.0f); break;
            }
        }
    }
    /* 0x538080: the scroll bar's tab and arrows only when the list pages */
    if (s->qr_nrow <= QR_ROWS_SHOWN) {
        int o;
        if ((o = feng_find(pk, O_SCROLL_TAB)) >= 0) feng_anim_hide(a, o, 1);
        if ((o = feng_find(pk, O_SCROLL_L)) >= 0) feng_anim_hide(a, o, 1);
        if ((o = feng_find(pk, O_SCROLL_R)) >= 0) feng_anim_hide(a, o, 1);
    }
    qr_options_scripts(s, 1);
}

/* ---------------------------------------------------------------- options */

/* a row's value: toggles cycle on left / right (each row's vt slot 1) */
static int opt_is_slider(int r) { return r <= OR_ENGINE; }
static float *opt_slider(int r) {
    switch (r) {
    case OR_MASTER: return &g_opt.master;   case OR_FE_MUSIC: return &g_opt.fe_music;
    case OR_IG_MUSIC: return &g_opt.ig_music; case OR_SPEECH: return &g_opt.speech;
    case OR_SFX: return &g_opt.sfx;         case OR_TIRE: return &g_opt.tire;
    case OR_ENGINE: return &g_opt.engine;   default: return NULL;
    }
}
/* the Wheel Options link (0x51dfc0) and Feedback (enabled by 0x5b8240, a
   force-feedback wheel) are wheel-only: the Vita has none, and has the rear
   touchpad's rows in their place. Its sensitivity is greyed while it is off */
static int opt_row_disabled(int r) {
    return r == OR_WHEEL || r == OR_FEEDBACK || (r == OR_TOUCH_SENS && !g_opt.rear_touch);
}

static void opt_build_rows(Shop *s) {
    int n = 0;
#define OROW(x) do { if (n < OPT_MAX_ROWS) s->opt_row[n++] = (x); } while (0)
    switch (s->opt_cat) {
    case OC_AUDIO:      /* 0x4b6e60 (Audio Mode unless split screen 0x850078 == 8) */
        for (int r = OR_MASTER; r <= OR_AUDIO_MODE; r++) OROW(r);
        break;
    case OC_GAMEPLAY:   /* 0x4b7310 */
        for (int r = OR_JUMP_CAM; r <= OR_SAVE; r++) OROW(r);
        break;
    case OC_PLAYER:     /* 0x4b7cd0 */
        OROW(OR_TRANS); OROW(OR_STABILITY); OROW(OR_CAMERA); OROW(OR_GAUGES); OROW(OR_RACE_POS);
        OROW(OR_LAP_INFO); OROW(OR_SPLIT); OROW(OR_SCORE); OROW(OR_LEADERBOARD); OROW(OR_REAR_TOUCH);
        OROW(OR_TOUCH_SENS); OROW(OR_ASK_TRANS);
        break;
    }
#undef OROW
    s->opt_nrow = n; s->opt_rsel = 0; s->opt_top = 0;
    while (s->opt_rsel < n - 1 && opt_row_disabled(s->opt_row[s->opt_rsel])) s->opt_rsel++;
}

static unsigned char togb(unsigned char v) { return (unsigned char)!v; }
static void opt_row_change(int r, int step) {
    OptPlayer *pl = &g_opt.p[0];
    switch (r) {
    case OR_AUDIO_MODE: g_opt.stereo = !g_opt.stereo; break;          /* 0x49ab80: 0..1 wrap */
    case OR_JUMP_CAM: g_opt.jump_cam = !g_opt.jump_cam; break;        /* 0 <-> 127 */
    case OR_MIRROR: g_opt.mirror = togb(g_opt.mirror); break;
    case OR_UNITS: g_opt.kmh = togb(g_opt.kmh); break;
    case OR_CAREER_DIFF: g_opt.career_diff = wrapi(g_opt.career_diff + step, 0, 2); break;
    case OR_EXPLORE_MAP: g_opt.explore_map = (unsigned char)wrapi(g_opt.explore_map + step, 0, 2); break;
    case OR_RACE_MAP: g_opt.race_map = (unsigned char)wrapi(g_opt.race_map + step, 0, 2); break;
    case OR_SAVE:       /* 0x49b170 (the memory card check 0x502fe0 always passes) */
        if (step > 0) {
            if (g_opt.autosave) { g_opt.save_on = 1; g_opt.autosave = 0; }
            else if (g_opt.save_on) g_opt.save_on = 0;
            else { g_opt.save_on = 1; g_opt.autosave = 1; }
        } else {
            if (g_opt.autosave) { g_opt.save_on = 0; g_opt.autosave = 0; }
            else if (g_opt.save_on) g_opt.autosave = 1;
            else g_opt.save_on = 1;
        }
        break;
    case OR_TRANS: pl->manual = togb(pl->manual); break;
    case OR_STABILITY: pl->stability_off = togb(pl->stability_off); break;
    case OR_CAMERA: pl->camera = wrapi(pl->camera + step, 0, 3); break;   /* Super Far is not cycled */
    case OR_GAUGES: pl->gauges = togb(pl->gauges); break;
    case OR_RACE_POS: pl->race_pos = togb(pl->race_pos); break;
    case OR_LAP_INFO: pl->lap_info = togb(pl->lap_info); break;
    case OR_SPLIT: pl->split = (unsigned char)wrapi(pl->split + step, 0, 4); break;
    case OR_SCORE: pl->score = togb(pl->score); break;
    case OR_LEADERBOARD: pl->leaderboard = togb(pl->leaderboard); break;
    case OR_FEEDBACK: pl->feedback = togb(pl->feedback); break;
    case OR_ASK_TRANS: pl->ask_trans = togb(pl->ask_trans); break;
    case OR_REAR_TOUCH: g_opt.rear_touch = togb(g_opt.rear_touch); break;
    case OR_TOUCH_SENS: g_opt.touch_sens = (unsigned char)wrapi(g_opt.touch_sens + step, 0, 2); break;
    }
}

static uint32_t opt_row_value(int r) {
    const OptPlayer *pl = &g_opt.p[0];
    static const uint32_t map3[3] = { STR_OFF, 0xf75595f2u, 0xf4b00e99u };       /* Off Static Rotate */
    static const uint32_t cam[5] = { 0xc3e9ae58u, 0x414f19d7u, 0x5ae3441fu, 0x1ea4cec2u, 0x916039b4u };
    static const uint32_t split[5] = { 0xd93f8f55u, 0xc44d3943u, 0x17fafc32u, 0x1ea459f8u, STR_OFF };
    static const uint32_t sens[3] = { 0x26033125u, 0x632b8da7u, 0x612af8a6u };    /* Low Medium High */
    switch (r) {
    case OR_AUDIO_MODE: return g_opt.stereo ? 0x55da8bf8u : 0xc50fa35fu;
    case OR_JUMP_CAM: return g_opt.jump_cam ? STR_ON : STR_OFF;
    case OR_MIRROR: return g_opt.mirror ? STR_ON : STR_OFF;
    case OR_UNITS: return g_opt.kmh ? 0x8569a25fu : 0x8569ab44u;
    case OR_CAREER_DIFF: return k_qo_diff[g_opt.career_diff > 2 ? 2 : g_opt.career_diff];
    case OR_EXPLORE_MAP: return map3[g_opt.explore_map % 3];
    case OR_RACE_MAP: return map3[g_opt.race_map % 3];
    case OR_SAVE: return g_opt.autosave ? 0x1b7abf8eu : 0x1b7abf90u - (g_opt.save_on != 0);
    case OR_TRANS: return pl->manual ? 0x1b3a2485u : 0x61cb0720u;
    case OR_STABILITY: return pl->stability_off ? STR_OFF : STR_ON;
    case OR_CAMERA: return cam[pl->camera < 0 ? 0 : pl->camera > 4 ? 4 : pl->camera];
    case OR_GAUGES: return pl->gauges ? STR_ON : STR_OFF;
    case OR_RACE_POS: return pl->race_pos ? STR_ON : STR_OFF;
    case OR_LAP_INFO: return pl->lap_info ? STR_ON : STR_OFF;
    case OR_SPLIT: return split[pl->split % 5];
    case OR_SCORE: return pl->score ? STR_ON : STR_OFF;
    case OR_LEADERBOARD: return pl->leaderboard ? STR_ON : STR_OFF;
    case OR_FEEDBACK: return pl->feedback ? STR_ON : STR_OFF;
    case OR_ASK_TRANS: return pl->ask_trans ? STR_ON : STR_OFF;
    case OR_REAR_TOUCH: return g_opt.rear_touch ? STR_ON : STR_OFF;
    case OR_TOUCH_SENS: return sens[g_opt.touch_sens > 2 ? 2 : g_opt.touch_sens];
    default: return 0;
    }
}

/* the slider's pool objects of row n (0x54f410: "SLIDER_BASE_%d" ...) */
enum { SP_BASE, SP_KNOB, SP_HANDLE, SP_BEG, SP_END, SP_N };
static int opt_slider_obj(const FengPackage *pk, int part, int n) {
    static const char *const fmt[SP_N] = { "SLIDER_BASE_%d", "SLIDER_KNOB_%d", "SLIDER_HANDLE_%d",
                                           "SLIDER_BEG_%d", "SLIDER_END_%d" };
    char nm[32]; snprintf(nm, sizeof nm, fmt[part], n);
    return feng_find(pk, feng_hash_upper(nm));
}

/* the row geometry of UI_Options (TITLE_MASTER / DATA_MASTER, as the QR rows) */
typedef struct { float tl, tt, step, dcx, dt, dw; } RowGeom;
static void row_geom(const FengPackage *pk, RowGeom *g) {
    g->tl = -272.0f; g->tt = -140.0f; g->step = QR_ROW_STEP; g->dcx = 126.0f; g->dt = -140.0f; g->dw = 200.0f;
    int tm = feng_find(pk, feng_hash_upper("TITLE_MASTER")), dm = feng_find(pk, feng_hash_upper("DATA_MASTER"));
    if (tm >= 0) {
        const FengObj *o = &pk->obj[tm];
        g->tl = o->ax - 0.5f * fabsf(o->size[0]); g->tt = o->ay - 0.5f * fabsf(o->size[1]); g->step = fabsf(o->size[1]);
    }
    if (dm >= 0) {
        const FengObj *o = &pk->obj[dm];
        g->dcx = o->ax; g->dt = o->ay - 0.5f * fabsf(o->size[1]); g->dw = fabsf(o->size[0]);
    }
}

/* 0x52def0 / 0x540d20: the slider of the row at screen slot y. The base is
   centred on DATA_MASTER at its top; the knob 46 in from the base's left
   end plus value x (base width - 92), its top at the base's middle less half
   its height, less 1; the green handle from the same 46 in, value x that
   length wide, its top 1 under the knob's top before the -2. Positions are
   top left (0x51d380 for images). BEG / END are not moved (stay in the pool). */
static void opt_slider_place(Shop *s, int k) {
    const FengPackage *pk = s->pk[PK_OPTIONS];
    FengAnim *a = s->opt_anim;
    float *v = opt_slider(s->opt_row[k]);
    if (!pk || !a || !v) return;
    RowGeom g; row_geom(pk, &g);
    float y = (float)(k - s->opt_top) * g.step;
    int ob = opt_slider_obj(pk, SP_BASE, k + 1), okn = opt_slider_obj(pk, SP_KNOB, k + 1),
        oh = opt_slider_obj(pk, SP_HANDLE, k + 1);
    if (ob < 0) return;
    float bw = fabsf(pk->obj[ob].size[0]), bh = fabsf(pk->obj[ob].size[1]);
    float bl = g.dcx - 0.5f * bw, bt = g.dt + y;
    feng_anim_pos(a, ob, bl + 0.5f * bw, bt + 0.5f * bh);
    float len = bw - 92.0f, f = *v < 0.0f ? 0.0f : *v > 1.0f ? 1.0f : *v;
    float kh = okn >= 0 ? fabsf(pk->obj[okn].size[1]) : 16.0f;
    if (okn >= 0) {
        float kw = fabsf(pk->obj[okn].size[0]);
        float kx = f * len - 0.5f * kw + 46.0f + bl, ky = 0.5f * bh + bt - 0.5f * kh + 1.0f - 2.0f;
        feng_anim_pos(a, okn, kx + 0.5f * kw, ky + 0.5f * kh);
    }
    if (oh >= 0) {
        float hw = f * len, hh = fabsf(pk->obj[oh].size[1]);
        float hy = 0.5f * bh + bt - 0.5f * kh + 1.0f;
        feng_anim_size(a, oh, hw, hh);
        feng_anim_pos(a, oh, bl + 46.0f + 0.5f * hw, hy + 0.5f * hh);
        feng_anim_hide(a, oh, hw <= 0.0f);
    }
}

/* Highlight / UnHighlight / Disable per row (0x52dff0 / 0x51e8d0, the
   slider's parts with it: 0x526d90 / 0x526f80) */
static void opt_scripts(Shop *s, int restart) {
    FengAnim *a = s->opt_anim;
    const FengPackage *pk = s->pk[PK_OPTIONS];
    if (!a || !pk) return;
    uint32_t hi = feng_hash_upper("Highlight"), un = feng_hash_upper("UnHighlight"),
             dis = feng_hash_upper("Disable");
    for (int k = 0; k < s->opt_nrow; k++) {
        uint32_t h = k == s->opt_rsel ? hi : opt_row_disabled(s->opt_row[k]) ? dis : un;
        for (int part = 0; part < QP_N; part++) {
            int o = qr_pool(pk, part, k + 1);
            if (o < 0) continue;
            if (!feng_anim_set(a, o, h, restart) && h == dis) feng_anim_set(a, o, un, restart);
        }
        if (opt_is_slider(s->opt_row[k]))
            for (int part = 0; part < SP_N; part++) {
                int o = opt_slider_obj(pk, part, k + 1);
                if (o >= 0) feng_anim_set(a, o, h, restart);
            }
    }
}

/* 0x52fdb0: the page's rows one TITLE_MASTER height apart from the top
   row; rows off the page and unused pool rows hidden. 0x538080 / 0x52e320:
   the scroll bar's tab only when the list pages, its centre at the bar's
   top + half the tab + (bar - tab) / (rows - page) per row scrolled. */
static void opt_layout(Shop *s) {
    const FengPackage *pk = s->pk[PK_OPTIONS];
    FengAnim *a = s->opt_anim;
    if (!pk || !a) return;
    RowGeom g; row_geom(pk, &g);
    memset(s->qr_rowobj, 0, sizeof s->qr_rowobj);
    s->row_arrow = K_ARROW_WHITE;
    for (int n = 1; n <= OPT_MAX_ROWS; n++) {
        int k = n - 1, used = k < s->opt_nrow, shown = used && k >= s->opt_top && k < s->opt_top + OPT_PAGE;
        int slider = used && opt_is_slider(s->opt_row[k]);
        float y = (float)(k - s->opt_top) * g.step;
        for (int part = 0; part < QP_N; part++) {
            int o = qr_pool(pk, part, n);
            if (o < 0) continue;
            s->qr_rowobj[o] = part == QP_LEFT || part == QP_RIGHT ? 2 : 1;
            for (int c = o + 1; c < pk->nobj; c++)
                if (feng_inside(pk, c, o)) s->qr_rowobj[c] = 1;
            int link = used && s->opt_row[k] == OR_WHEEL;       /* a link row (0x51dfc0): name only */
            int vis = shown && !(slider && part == QP_DATA) &&  /* a slider row has no data string */
                      !(link && (part == QP_DATA || part == QP_LEFT || part == QP_RIGHT));
            feng_anim_hide(a, o, !vis);
            if (!vis) continue;
            float aw = fabsf(pk->obj[o].size[0]), ah = fabsf(pk->obj[o].size[1]);
            switch (part) {
            case QP_NAME: feng_anim_pos(a, o, g.tl, g.tt + y); break;
            case QP_DATA: feng_anim_pos(a, o, g.dcx, g.dt + y); break;
            case QP_LEFT: feng_anim_pos(a, o, g.dcx - 0.5f * g.dw - 0.5f * aw, g.dt + y + 0.5f * ah); break;
            case QP_RIGHT: feng_anim_pos(a, o, g.dcx + 0.5f * g.dw + 0.5f * aw, g.dt + y + 0.5f * ah); break;
            case QP_BACK: feng_anim_pos(a, o, g.tl + 276.0f, g.tt + y + 10.0f); break;
            }
        }
        for (int part = 0; part < SP_N; part++) {
            int o = opt_slider_obj(pk, part, n);
            if (o < 0) continue;
            s->qr_rowobj[o] = 1;
            feng_anim_hide(a, o, !(shown && slider && part <= SP_HANDLE));
        }
        if (shown && slider) opt_slider_place(s, k);
    }
    int tab = feng_find(pk, O_SCROLL_TAB), bar = feng_find(pk, O_SCROLL_BAR);
    if (tab >= 0) {
        int pages = s->opt_nrow > OPT_PAGE;
        feng_anim_hide(a, tab, !pages);
        if (pages && bar >= 0) {
            float blen = fabsf(pk->obj[bar].size[1]), btop = pk->obj[bar].ay - 0.5f * blen;
            float th = fabsf(pk->obj[tab].size[1]);
            float per = (blen - th) / (float)(s->opt_nrow - OPT_PAGE);
            feng_anim_pos(a, tab, pk->obj[tab].ax, btop + 0.5f * th + per * (float)s->opt_top);
        }
    }
}

/* 0x4cf750 / 0x4b8470: the title, and the objects the category hides */
static void opt_enter(Shop *s, int cat) {
    const FengPackage *pk = s->pk[PK_OPTIONS];
    s->opt_cat = cat;
    opt_build_rows(s);
    s->screen = SC_OPT;
    if (!pk) return;
    if (!s->opt_anim) s->opt_anim = (FengAnim *)malloc(sizeof *s->opt_anim);
    FengAnim *a = s->opt_anim;
    if (!a) return;
    feng_anim_init(a, pk);
    anim_fadein(a);
    static const char *const hide_nm[] = { "ColorCal_Logo", "ColorCal_Blurb", "Colour_Calibration_Backing" };
    static const uint32_t hide_h[] = { 0x010cba23u, 0x97b71ddeu, 0x7326e44cu, 0xc90dc4abu };
    for (int i = 0; i < 3; i++) { int o = feng_find(pk, feng_hash_upper(hide_nm[i])); if (o >= 0) feng_anim_hide(a, o, 1); }
    for (int i = 0; i < 4; i++) { int o = feng_find(pk, hide_h[i]); if (o >= 0) feng_anim_hide(a, o, 1); }
    int o;   /* the list's scroll arrows: only the player options show them */
    if ((o = feng_find(pk, O_SCROLL_L)) >= 0) feng_anim_hide(a, o, cat != OC_PLAYER);
    if ((o = feng_find(pk, O_SCROLL_R)) >= 0) feng_anim_hide(a, o, cat != OC_PLAYER);
    opt_layout(s);
    opt_scripts(s, 1);
}

/* the rows for the pause menu's UI_PauseOptions (0x4c4bd0): audio 0x4c3370
   (no Audio Mode), gameplay 0x4c38f0 (Career Difficulty only in a career,
   Rearview Mirror and Explore Map Mode not in a drag race), player 0x4c4740
   (Transmission not in a drag race; Race Position / Lap Info / Split Time /
   Leaderboard only with 0x89e7e6 clear -- taken as "not free roam") */
int shop_pause_rows(int cat, int drag, int roam, int *rows, int cap) {
    int n = 0;
#define PROW(x) do { if (n < cap) rows[n++] = (x); } while (0)
    switch (cat) {
    case OC_AUDIO: for (int r = OR_MASTER; r <= OR_ENGINE; r++) PROW(r); break;
    case OC_GAMEPLAY:
        PROW(OR_JUMP_CAM); if (!drag) PROW(OR_MIRROR); PROW(OR_UNITS); if (!drag) PROW(OR_EXPLORE_MAP);
        PROW(OR_RACE_MAP); PROW(OR_SAVE);
        break;
    case OC_PLAYER:
        if (!drag) PROW(OR_TRANS);
        PROW(OR_STABILITY); PROW(OR_CAMERA); PROW(OR_REAR_TOUCH); PROW(OR_TOUCH_SENS); PROW(OR_GAUGES);
        if (!roam) { PROW(OR_RACE_POS); PROW(OR_LAP_INFO); PROW(OR_SPLIT); PROW(OR_LEADERBOARD); }
        PROW(OR_SCORE);
        break;
    }
#undef PROW
    return n;
}
int shop_row_slider(int r) { return opt_is_slider(r); }
float *shop_row_level(int r) { return opt_slider(r); }
int shop_row_disabled(int r) { return opt_row_disabled(r); }
void shop_row_change(int r, int step) {
    float *v = opt_slider(r);
    if (v) { float nv = *v + 0.05f * (float)step; *v = nv < 0.0f ? 0.0f : nv > 1.0f ? 1.0f : nv; }
    else opt_row_change(r, step);
}
uint32_t shop_row_name(int r) { return r >= 0 && r < OR_COUNT ? k_or_name[r] : 0; }
uint32_t shop_row_value(int r) { return opt_row_value(r); }

/* ------------------------------------------------------- controller setup */

static int ctl_pool(const FengPackage *pk, const char *fmt, int n) {
    char nm[40]; snprintf(nm, sizeof nm, fmt, n);
    return feng_find(pk, feng_hash_upper(nm));
}

/* 0x4accf0 / 0x4decd0 / 0x4cb4b0: the page's rows one TITLE_MASTER height
   apart from its top; the name's text top left at TITLE_MASTER's left
   (0x51e0f0), its Options_Backing_%d at the name + (276, 8); each cell's
   text centred on DATA_MASTER / DATA_MASTER2 (DATA_MASTER's width) with its
   top on the row (0x52d6b0). Rows off the page are hidden; the scroll tab
   (0x538080 with (1, rows - 1, row)) follows the selected row. */
static void ctl_layout(Shop *s) {
    const FengPackage *pk = s->pk[PK_CTL];
    FengAnim *a = s->ctl_anim;
    if (!pk || !a) return;
    float tl = -277.0f, tt = -106.0f, step = 36.0f, d1 = -64.0f, d2 = 98.0f, dw = 150.0f;
    int tm = feng_find(pk, feng_hash_upper("TITLE_MASTER")), dm = feng_find(pk, feng_hash_upper("DATA_MASTER")),
        dm2 = feng_find(pk, feng_hash_upper("DATA_MASTER2"));
    if (tm >= 0) { const FengObj *o = &pk->obj[tm];
        tl = o->ax - 0.5f * fabsf(o->size[0]); tt = o->ay - 0.5f * fabsf(o->size[1]); step = fabsf(o->size[1]); }
    if (dm >= 0) { const FengObj *o = &pk->obj[dm]; d1 = o->ax - 0.5f * fabsf(o->size[0]); dw = fabsf(o->size[0]); }
    if (dm2 >= 0) { const FengObj *o = &pk->obj[dm2]; d2 = o->ax - 0.5f * fabsf(o->size[0]); }
    memset(s->qr_rowobj, 0, sizeof s->qr_rowobj);
    for (int n = 1; n <= 24; n++) {
        int k = n - 1, shown = k < OPT_ACT_COUNT && k >= s->ctl_top && k < s->ctl_top + CTL_PAGE;
        float y = tt + (float)(k - s->ctl_top) * step;
        int on = ctl_pool(pk, "OPTION_NAME_%d", n), oa = ctl_pool(pk, "OPTION_DATA_%d", n),
            ob = ctl_pool(pk, "OPTION_DATA_2_%d", n), og = ctl_pool(pk, "Options_Backing_%d", n);
        if (on < 0 && oa < 0 && ob < 0 && og < 0) break;
        int objs[4] = { on, oa, ob, og };
        for (int q = 0; q < 4; q++) {
            int o = objs[q];
            if (o < 0) continue;
            s->qr_rowobj[o] = 1;
            for (int c = o + 1; c < pk->nobj; c++) if (feng_inside(pk, c, o)) s->qr_rowobj[c] = 1;
            feng_anim_hide(a, o, !shown);
        }
        if (!shown) continue;
        if (on >= 0) feng_anim_pos(a, on, tl, y);
        if (og >= 0) feng_anim_pos(a, og, tl + 276.0f, y + 8.0f);
        for (int q = 0; q < 2; q++) {
            int o = q ? ob : oa;
            if (o < 0) continue;
            const FengObj *fo = &pk->obj[o];
            float ay = y;
            if (fo->just & (FENG_JUST_VCENTER | FENG_JUST_BOTTOM)) {   /* the text's top on the row */
                int fi = fo->res >= 0 && fo->res < pk->nres ? s->font_of[PK_CTL][fo->res] : -1;
                float lh = fi >= 0 ? (float)s->font[fi].f.line * fabsf(fo->size[1]) : 0.0f;
                ay += fo->just & FENG_JUST_VCENTER ? 0.5f * lh : lh;
            }
            feng_anim_pos(a, o, (q ? d2 : d1) + 0.5f * dw, ay);
        }
    }
    int tab = feng_find(pk, O_SCROLL_TAB), bar = feng_find(pk, O_SCROLL_BAR);
    if (tab >= 0 && bar >= 0) {
        float blen = fabsf(pk->obj[bar].size[1]), btop = pk->obj[bar].ay - 0.5f * blen;
        float th = fabsf(pk->obj[tab].size[1]);
        int cur = s->ctl_sel < 0 ? 0 : s->ctl_sel;
        feng_anim_pos(a, tab, pk->obj[tab].ax,
                      btop + 0.5f * th + (blen - th) * (float)cur / (float)(OPT_ACT_COUNT - 1));
    }
}

/* the row's name and backing and the cursor's cell Highlight (0x51e130 /
   0x52d770), the rest UnHighlight; a capture pulses its cell; the device
   title Highlight while the cursor is on it */
static void ctl_scripts(Shop *s, int restart) {
    const FengPackage *pk = s->pk[PK_CTL];
    FengAnim *a = s->ctl_anim;
    if (!pk || !a) return;
    uint32_t hi = feng_hash_upper("Highlight"), un = feng_hash_upper("UnHighlight"), pu = feng_hash_upper("Pulse");
    for (int k = 0; k < OPT_ACT_COUNT; k++) {
        int n = k + 1, row = k == s->ctl_sel;
        int on = ctl_pool(pk, "OPTION_NAME_%d", n), og = ctl_pool(pk, "Options_Backing_%d", n);
        if (on >= 0) feng_anim_set(a, on, row ? hi : un, restart);
        if (og >= 0) feng_anim_set(a, og, row ? hi : un, restart);
        for (int q = 0; q < 2; q++) {
            int o = ctl_pool(pk, q ? "OPTION_DATA_2_%d" : "OPTION_DATA_%d", n);
            if (o < 0) continue;
            int cell = row && q == s->ctl_col;
            if (cell && s->ctl_cap) { if (!feng_anim_set(a, o, pu, restart)) feng_anim_set(a, o, hi, restart); }
            else feng_anim_set(a, o, cell ? hi : un, restart);
        }
    }
    int dv = feng_find(pk, O_CTL_DEVICE);
    if (dv >= 0) feng_anim_set(a, dv, s->ctl_sel < 0 ? hi : un, restart);
}

static void ctl_enter(Shop *s) {
    const FengPackage *pk = s->pk[PK_CTL];
    s->screen = SC_CTL;
    s->ctl_sel = 0; s->ctl_col = 0; s->ctl_top = 0; s->ctl_cap = 0;
    s->ctl_instr = STR_CTL_CONFIG;
    if (!pk) return;
    if (!s->ctl_anim) s->ctl_anim = (FengAnim *)malloc(sizeof *s->ctl_anim);
    FengAnim *a = s->ctl_anim;
    if (!a) return;
    feng_anim_init(a, pk);
    anim_fadein(a);
    int o;   /* one device (0x4ace00): no device arrows */
    if ((o = feng_find(pk, O_CTL_DEV_L)) >= 0) feng_anim_hide(a, o, 1);
    if ((o = feng_find(pk, O_CTL_DEV_R)) >= 0) feng_anim_hide(a, o, 1);
    s->row_arrow = K_MENU_ARROW;
    ctl_layout(s);
    ctl_scripts(s, 1);
}

int shop_ctl_capture(const Shop *s) { return s && s->screen == SC_CTL ? s->ctl_cap : 0; }
void shop_ctl_arm(Shop *s) { if (s && s->ctl_cap == 1) s->ctl_cap = 2; }
/* 0xc98356ba: the device reported the new key: the binding, then "Press
   enter to assign a new key or button" */
void shop_ctl_button(Shop *s, int vb) {
    if (!s || s->ctl_cap != 2 || s->ctl_sel < 0 || vb <= VB_NONE || vb >= VB_COUNT) return;
    g_opt.ctl[s->ctl_sel][s->ctl_col] = (unsigned char)vb;
    s->ctl_cap = 0;
    s->ctl_instr = STR_CTL_ASSIGN;
    audio_fe_play(FE_SND_SELECT);
    printf("controls: %s %s = %s\n", s->ctl_col ? "secondary" : "primary",
           str_get(s, k_ctl_name[s->ctl_sel]), g_vb_name[vb]);
    ctl_scripts(s, 1);
}

/* 0x4f9340: Up / Down move the cursor (Up from the first row to the device
   title), Left / Right between the Primary and Secondary cells, accept
   starts the capture (0x406415e3: Pulse, "Press a button ..."), back leaves
   for UI_OptionsMain. 0xc519bfbf asks "Reset to default settings?" in a
   GenericDialog_SMALL (here Triangle): OK (0xefedcba0) resets, Cancel. */
static void ctl_input(Shop *s, int key) {
    if (s->ctl_cap) return;                          /* the next button is the binding */
    if (key == SHOP_KEY_UP) {
        if (s->ctl_sel < 0) return;
        s->ctl_sel--;
        if (s->ctl_sel >= 0 && s->ctl_sel < s->ctl_top) s->ctl_top = s->ctl_sel;
    } else if (key == SHOP_KEY_DOWN) {
        if (s->ctl_sel >= OPT_ACT_COUNT - 1) return;
        s->ctl_sel++;
        if (s->ctl_sel >= s->ctl_top + CTL_PAGE) s->ctl_top = s->ctl_sel - CTL_PAGE + 1;
    } else if ((key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) && s->ctl_sel >= 0) {
        s->ctl_col = key == SHOP_KEY_RIGHT;
    } else if (key == SHOP_KEY_OK && s->ctl_sel >= 0) {
        s->ctl_cap = 1;
        s->ctl_instr = STR_CTL_PRESS;
    } else if (key == SHOP_KEY_TRI) {
        dialog(s, PK_DLG_S, 2, 0x2bbcca6bu, DA_CTL_RESET, STR_OK, STR_CANCEL);
        return;
    } else if (key == SHOP_KEY_BACK) {
        options_save();
        s->screen = SC_OPT_MAIN;
        return;
    } else return;
    ctl_layout(s);
    ctl_scripts(s, 0);
}

/* ------------------------------------------------------------ EA Trax jukebox */

/* EA_TRAX_Jukebox.fng (ctor 0x553c70, list 0x54b310, handler 0x56b1c0):
   four fixed slots k showing the playlist (0x83acb8) from juke_top: the
   number ("0%d" / "%d"), artist, title, album and the mode, with the slot's
   backing and Left_arrow / Right_arrow_%d; the focused slot's widgets
   Highlight, the rest UnHighlight; the grab frame 0x8065767b + k plays
   Grab / UnGrab. */
#define JUKE_SLOTS 4
#define O_JUKE_SHUFFLE 0xca74a2fau
static const uint32_t k_juke_mode_str[4] = { 0x9cce9f86u, 0x5278c50bu, 0x5c1b351cu, 0x9cce64c4u };   /* Off Menu Racing All */
enum { JW_NUM, JW_ARTIST, JW_TITLE, JW_ALBUM, JW_MODE, JW_BACK, JW_LARROW, JW_RARROW, JW_GRAB, JW_COUNT };
static const uint32_t k_juke_widget[JW_COUNT] = { 0xe454e9a5u, 0x66646fc4u, 0x2890c8afu, 0x77bd189eu, 0xf3ebdc4eu,
                                                  0xde635f7cu, 0x7bcd6704u, 0x7c51b6d7u, 0x8065767bu };
static int juke_obj(const Shop *s, int w, int k) { return feng_find(s->pk[PK_JUKE], k_juke_widget[w] + (uint32_t)k); }

/* the playlist the screen edits and the masks it gives the music (0x479220) */
static void juke_apply(void) {
    g_opt.playlist_set = 1;
    music_set_playlist(g_opt.pl_song, g_opt.pl_mode, g_opt.shuffle);
}

static void juke_scripts(Shop *s, int restart) {
    const FengPackage *pk = s->pk[PK_JUKE];
    FengAnim *a = s->juke_anim;
    if (!pk || !a) return;
    uint32_t hi = feng_hash_upper("Highlight"), un = feng_hash_upper("UnHighlight");
    for (int k = 0; k < JUKE_SLOTS; k++) {
        int foc = s->juke_top + k == s->juke_sel;
        for (int w = 0; w < JW_GRAB; w++) {
            int o = juke_obj(s, w, k);
            if (o >= 0) feng_anim_set(a, o, foc ? hi : un, restart);
        }
    }
    /* 0x538080: the scroll tab along ScrollBar_Backing by the focused slot */
    int tab = feng_find(pk, O_SCROLL_TAB), bar = feng_find(pk, O_SCROLL_BAR);
    if (tab >= 0 && bar >= 0) {
        float blen = fabsf(pk->obj[bar].size[1]), btop = pk->obj[bar].ay - 0.5f * blen;
        float th = fabsf(pk->obj[tab].size[1]);
        feng_anim_pos(a, tab, pk->obj[tab].ax,
                      btop + 0.5f * th + (blen - th) * (float)s->juke_sel / (float)(MUSIC_SONGS - 1));
    }
}

static void juke_enter(Shop *s) {
    const FengPackage *pk = s->pk[PK_JUKE];
    s->screen = SC_JUKE;
    s->juke_sel = 0; s->juke_top = 0; s->juke_grab = 0;
    if (!g_opt.playlist_set) music_default_playlist(g_opt.pl_song, g_opt.pl_mode);
    if (!pk) return;
    if (!s->juke_anim) s->juke_anim = (FengAnim *)malloc(sizeof *s->juke_anim);
    FengAnim *a = s->juke_anim;
    if (!a) return;
    feng_anim_init(a, pk);
    anim_fadein(a);
    uint32_t st = feng_hash_upper("Static");   /* the ctor: the grab frames and the arrows */
    for (int k = 0; k < JUKE_SLOTS; k++)
        for (int w = JW_LARROW; w <= JW_GRAB; w++) {
            int o = juke_obj(s, w, k);
            if (o >= 0) feng_anim_set(a, o, st, 1);
        }
    s->row_arrow = K_MENU_ARROW;
    juke_scripts(s, 1);
}

/* the grab frame of a shown slot */
static void juke_grab_script(Shop *s, int slot, int on) {
    int o = s->juke_anim ? juke_obj(s, JW_GRAB, slot) : -1;
    if (o >= 0) feng_anim_set(s->juke_anim, o, feng_hash_upper(on ? "Grab" : "UnGrab"), 1);
}

/* 0x56b1c0 */
static void juke_input(Shop *s, int key) {
    int n = MUSIC_SONGS;
    if (key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) {
        int step = key == SHOP_KEY_UP ? -1 : 1, to = s->juke_sel + step;
        if (!s->juke_grab) {   /* 0x550e60: at an end nothing moves */
            if (to < 0 || to >= n) return;
            s->juke_sel = to;
        } else {               /* 0x550f10: the grabbed song moves with the cursor */
            audio_fe_play(step < 0 ? FE_SND_UP : FE_SND_DOWN);
            s->snd_done = 1;
            if (to < 0 || to >= n) return;
            int old_slot = s->juke_sel - s->juke_top;
            unsigned char ts = g_opt.pl_song[to], tm = g_opt.pl_mode[to];
            g_opt.pl_song[to] = g_opt.pl_song[s->juke_sel]; g_opt.pl_mode[to] = g_opt.pl_mode[s->juke_sel];
            g_opt.pl_song[s->juke_sel] = ts; g_opt.pl_mode[s->juke_sel] = tm;
            s->juke_sel = to;
            if (to < s->juke_top) s->juke_top = to;
            if (to >= s->juke_top + JUKE_SLOTS) s->juke_top = to - JUKE_SLOTS + 1;
            int new_slot = s->juke_sel - s->juke_top;
            if (new_slot != old_slot) { juke_grab_script(s, new_slot, 1); juke_grab_script(s, old_slot, 0); }
            juke_apply();
            juke_scripts(s, 0);
            return;
        }
        if (s->juke_sel < s->juke_top) s->juke_top = s->juke_sel;
        if (s->juke_sel >= s->juke_top + JUKE_SLOTS) s->juke_top = s->juke_sel - JUKE_SLOTS + 1;
        juke_scripts(s, 0);
    } else if (key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) {
        /* 0x53deb0: the song's mode Off / Menu / Racing / All, Blink on the arrow */
        int m = g_opt.pl_mode[s->juke_sel] & 3;
        m = key == SHOP_KEY_LEFT ? (m + 3) % 4 : (m + 1) % 4;
        g_opt.pl_mode[s->juke_sel] = (unsigned char)m;
        int o = s->juke_anim ? juke_obj(s, key == SHOP_KEY_LEFT ? JW_LARROW : JW_RARROW, s->juke_sel - s->juke_top) : -1;
        if (o >= 0) feng_anim_set(s->juke_anim, o, feng_hash_upper("Blink"), 1);
        audio_fe_play(key == SHOP_KEY_LEFT ? FE_SND_LEFT : FE_SND_RIGHT);   /* type TOGGLE: generic ids */
        s->snd_done = 1;
        juke_apply();
    } else if (key == SHOP_KEY_TRI) {
        /* 0xc519bfc0 (Grab, $JOY_EVENT_FENG_BUTTON1$): 0x531630 / 0x5316b0 */
        s->juke_grab = !s->juke_grab;
        juke_grab_script(s, s->juke_sel - s->juke_top, s->juke_grab);
        juke_apply();
        audio_fe_play(FE_SND_SELECT);
        s->snd_done = 1;
    } else if (key == SHOP_KEY_LTRIG || key == SHOP_KEY_RTRIG) {
        /* 0x5241e0: Sequential / Random */
        g_opt.shuffle = !g_opt.shuffle;
        juke_apply();
        audio_fe_play(key == SHOP_KEY_LTRIG ? FE_SND_LEFT : FE_SND_RIGHT);
        s->snd_done = 1;
    } else if (key == SHOP_KEY_OK) {
        /* 0xc407210: preview the focused song (0x480c10 -> 0x47af90) */
        music_play_song(g_opt.pl_song[s->juke_sel]);
        audio_fe_play(FE_SND_SELECT);
        s->snd_done = 1;
    } else if (key == SHOP_KEY_BACK) {
        /* 0x911ab364: the masks, back to UI_OptionsMain; the front-end
           music carries on (0x480c30(-1): already in state 1) */
        if (s->juke_grab) { s->juke_grab = 0; juke_grab_script(s, s->juke_sel - s->juke_top, 0); }
        juke_apply();
        options_save();
        s->screen = SC_OPT_MAIN;
    }
}

/* ------------------------------------------------------------- paint shop */

/* UI_Paint.fng (factory 0x56c1b0, ctor 0x56aeb0, handler 0x563410): the
   category strip (IconScroller, k_paint_cat) and, after accept, a grid of
   up to 60 swatches PAINT_SWATCH_1..60 (20 a row, +0x178) with the
   U2_Paint_HighLight cursor. The colours are the part database's PAINT
   group (48) parts of the category's brand (BRAND_NAME 0xebb03e66, 0x5449c0);
   base paint, spoiler, roof scoop and mirrors switch brand with L / R
   (gloss / metallic / pearlescent, 0x55d0d0). Moving the cursor puts the
   colour on the car; accept installs it; back restores what was installed. */
static uint32_t g_shop_stock_paint;
void shop_set_stock_paint(uint32_t part_hash) { g_shop_stock_paint = part_hash; }

#define BR_GLOSS 0x02daab07u
#define BR_METAL 0x03437a52u
#define BR_PEARL 0x03797533u
#define O_PAINT_HL    0x9ba6d658u   /* U2_Paint_HighLight */
#define O_PAINT_TEXT  0xef653affu   /* Paint_text: the brand */
#define O_PAINT_GRID  0xdb285767u
#define O_PAINT_LOCK  0x23a43844u
#define O_PAINT_ARROW_L 0xcf6f0439u
#define O_PAINT_ARROW_R 0xaba93d3fu
#define O_ICON_HL     0xdf422345u
#define PAINT_COLS    20

/* 0x50f5b0 */
static uint32_t paint_cat_brand(int slot) {
    switch (slot) {
    case 0x3f: case 0x45: case 0x4b: case 0x4c: return BR_GLOSS;
    case 0x44: return 0x0016bdceu;
    case 0x46: return 0xd6640dffu;   /* CALIPER */
    case 0x47: return 0x066350f0u;   /* MUFFLER */
    case 0x49: case 0x4a: return 0x0000da27u;   /* RIM */
    default: return slot >= 0x4d && slot < 0x5d ? 0x03e871f1u : 0;   /* VINYL_COLOURl_c: VINYL_PAINT */
    }
}
static int paint_slot(const Shop *s) { return s->paintcat.e ? s->paintcat.e[s->paintcat.sel].group : 0x3f; }
/* 0x637050: a vinyl layer's colours before any is chosen */
static uint32_t vinyl_default_col(int c) {
    static const char *const d[3] = { "VINYL_L1_COLOR09", "VINYL_L1_COLOR02", "VINYL_L2_COLOR11" };
    return hash_more(0xffffffffu, d[c < 0 ? 0 : c > 2 ? 2 : c]);
}
/* what the paint screen's category holds in a configuration */
static uint32_t *paint_ref(const Shop *s, N2CarConfig *c) {
    return s->vmode >= 0 ? &c->vinyl_col[s->vmode][s->paintcat.sel] : &c->paint[s->paintcat.sel];
}

/* the attribute `key` of a part (its value), 0 when absent */
static int db_attr(const Shop *s, unsigned aoff, uint32_t key, uint32_t *val) {
    if (aoff == 0xffffu || !s->db_off || !s->db_attr) return 0;
    const unsigned char *p = s->db + s->db_off + aoff * 2L;
    unsigned n = p[0] | p[1] << 8;
    for (unsigned k = 0; k < n && k < 64; k++) {
        unsigned ai = p[2 + 2 * k] | p[3 + 2 * k] << 8;
        if (n2_u32(s->db + s->db_attr + ai * 8L) == key) { *val = n2_u32(s->db + s->db_attr + ai * 8L + 4); return 1; }
    }
    return 0;
}
/* the database record of a part hash, NULL none */
static const unsigned char *db_part(const Shop *s, uint32_t hash) {
    if (!s->db || !s->db_parts || !hash) return NULL;
    for (long q = s->db_parts; q + 14 <= s->db_parts + s->db_parts_len; q += 14) {
        const unsigned char *r = s->db + q;
        if (!n2_u32(r) && !(r[8] | r[9] << 8)) break;
        if (n2_u32(r) == hash) return r;
    }
    return NULL;
}

/* 0x50f310: what the car needs before a category can be painted */
/* 0x50f310: base paint always; engine accents need the engine part at
   level 3 (the message: level 3 engine, ECU and turbo); brakes the
   performance parts 0x28, 0x29 or 0x2b; the others an aftermarket part in
   their slot (spoiler 0xc, exhaust 0x10, rims 0x1d, roof 7, mirrors 0x20),
   not carbon fibre; spinners also a rim that takes them (0x931ff82e) */
static int paint_can(const Shop *s, int slot) {
    PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit);
    if (slot >= 0x4d && slot < 0x5d) return 1;   /* a vinyl's colours */
    switch (slot) {
    case 0x3f: return 1;
    case 0x44: return perf_cat_level(&pp, 3) >= 3 && perf_cat_level(&pp, 7) >= 3 && perf_cat_level(&pp, 4) >= 3;
    case 0x45: return s->fitted.parts[N2_PART_SPOILER] != 0;
    case 0x46: return s->fitted.perf[0x28] || s->fitted.perf[0x29] || s->fitted.perf[0x2b];
    case 0x47: return s->fitted.parts[N2_PART_EXHAUST] != 0;
    case 0x49: return strcmp(s->rim_folder, STOCK_RIM_FOLDER) || s->rim_style != 1;
    case 0x4b: return s->fitted.parts[N2_PART_SCOOP] != 0;
    case 0x4c: return s->fitted.parts[N2_PART_MIRROR] != 0;
    case 0x4a: return !strcmp(s->rim_folder, "SPINNER");   /* a spinner rim fitted */
    default: return 0;
    }
}
/* 0x563410 0x406415e3: the message when it cannot */
static uint32_t paint_cant_msg(int slot) {
    switch (slot) {
    case 0x44: return 0xd286be3au;   /* level 3 engine, ECU and turbo first */
    case 0x4a: return 0x6bcbceeau;   /* spinners first */
    default: return 0x73bc702eu;     /* stock parts cannot be painted */
    }
}

static int paint_swatch_obj(const Shop *s, int n) {
    char nm[24]; snprintf(nm, sizeof nm, "PAINT_SWATCH_%d", n);
    return feng_find(s->pk[PK_PAINT], feng_hash_upper(nm));
}

/* the cursor U2_Paint_HighLight on swatch k: its position in the grid group
   + (152, 165) (+0x180 / +0x184) */
static void paint_cursor(Shop *s) {
    FengAnim *a = s->paint_anim;
    int hl = feng_find(s->pk[PK_PAINT], O_PAINT_HL);
    if (!a || hl < 0 || s->paint_sel < 0 || s->paint_sel >= s->paint_n) return;
    const FengObj *o = &s->pk[PK_PAINT]->obj[s->paint_obj[s->paint_sel]];
    feng_anim_pos(a, hl, o->pos[0] + 152.0f, o->pos[1] + 165.0f);
}

/* 0x55c1f0: the colour on the car */
static void paint_preview(Shop *s) {
    *paint_ref(s, &s->preview) = s->paint_n ? s->paint_hash[s->paint_sel] :
                                 s->vmode >= 0 ? s->vcol_open : s->fitted.paint[s->paintcat.sel];
    request_preview(s);
}

/* 0x548630: the swatches of the category's brand, in database order */
static void paint_build(Shop *s) {
    const FengPackage *pk = s->pk[PK_PAINT];
    FengAnim *a = s->paint_anim;
    s->paint_n = 0; s->paint_sel = 0;
    int slot = paint_slot(s), ci = s->paintcat.sel;
    uint32_t inst = s->vmode >= 0 ? *paint_ref(s, &s->preview) : s->fitted.paint[ci];
    if (!inst && slot == 0x3f) inst = g_shop_stock_paint;   /* the stock base paint is a part too */
    if (!inst && s->vmode >= 0) inst = vinyl_default_col(ci);
    int pgroup = s->vmode >= 0 ? 49 : 48;                  /* VINYL_PAINT / PAINT */
    uint32_t kr = 0xd99au, kg = 0x2ddc8f0u, kb = 0x136707u,
             dr = feng_hash("DISPRED"), dg = feng_hash("DISPGREEN"), db = feng_hash("DISPBLUE");
    for (long q = s->db ? s->db_parts : 0; s->db && q + 14 <= s->db_parts + s->db_parts_len && s->paint_n < 60; q += 14) {
        const unsigned char *r = s->db + q;
        uint32_t hash = n2_u32(r), br = 0, v;
        unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
        if (!hash && !soff) break;
        if (r[4] != pgroup || !db_attr(s, aoff, 0xebb03e66u, &br) || br != s->paint_brand) continue;
        int o = pk ? paint_swatch_obj(s, s->paint_n + 1) : -1;
        if (o < 0) break;
        if (hash == inst) s->paint_sel = s->paint_n;
        uint32_t rgb[3] = { 0, 0, 0 };
        if (db_attr(s, aoff, kr, &v)) rgb[0] = v; if (db_attr(s, aoff, kg, &v)) rgb[1] = v;
        if (db_attr(s, aoff, kb, &v)) rgb[2] = v;
        if (db_attr(s, aoff, dr, &v)) rgb[0] = v; if (db_attr(s, aoff, dg, &v)) rgb[1] = v;
        if (db_attr(s, aoff, db, &v)) rgb[2] = v;
        if (a) {
            feng_anim_hide(a, o, 0);
            feng_anim_color(a, o, 0xff000000u | (rgb[0] & 255) << 16 | (rgb[1] & 255) << 8 | (rgb[2] & 255));
        }
        s->paint_hash[s->paint_n] = hash; s->paint_obj[s->paint_n] = o;
        s->paint_n++;
    }
    for (int n = s->paint_n + 1; a && n <= 60; n++) {   /* the rest hidden */
        int o = paint_swatch_obj(s, n);
        if (o >= 0) feng_anim_hide(a, o, 1);
    }
    int lk = pk ? feng_find(pk, O_PAINT_LOCK) : -1;
    if (a && lk >= 0) feng_anim_set(a, lk, feng_hash_upper(s->paint_n ? "HIDE" : "SHOW"), 1);
    if (s->paint_n) paint_cursor(s);
}

/* 0x5484e0: the L / R tokens for the brand categories */
static void paint_strip_update(Shop *s) {
    const FengPackage *pk = s->pk[PK_PAINT];
    FengAnim *a = s->paint_anim;
    if (!pk || !a) return;
    int slot = paint_slot(s), base = slot == 0x3f || slot == 0x45 || slot == 0x4b || slot == 0x4c, o;
    if ((o = feng_find(pk, 0x9de5ba07u)) >= 0) feng_anim_hide(a, o, !base);
    if ((o = feng_find(pk, 0x7a1ff30du)) >= 0) feng_anim_hide(a, o, !base);
    /* Customize, not the career: no cost (0x545460) */
    if ((o = feng_find(pk, 0x9ff84a88u)) >= 0) { feng_anim_hide(a, o, 1); feng_anim_set(a, o, feng_hash_upper("HIDE"), 1); }
    if ((o = feng_find(pk, 0x0013c45eu)) >= 0) feng_anim_hide(a, o, 1);   /* the colour's price: career only (0x55d0d0) */
}

static void paint_enter_with(Shop *s, const ShopEntry *e, int n, int vmode) {
    const FengPackage *pk = s->pk[PK_PAINT];
    s->screen = SC_PAINT;
    s->vmode = vmode;
    scroller_set(&s->paintcat, e, n);
    s->paint_mode = 0; s->paint_n = 0;
    s->paint_brand = paint_cat_brand(paint_slot(s));
    if (!pk) return;
    if (!s->paint_anim) s->paint_anim = (FengAnim *)malloc(sizeof *s->paint_anim);
    FengAnim *a = s->paint_anim;
    if (!a) return;
    feng_anim_init(a, pk);
    anim_fadein(a);
    int o;   /* the ctor: the cursor and the brand arrows hidden */
    if ((o = feng_find(pk, O_PAINT_HL)) >= 0) feng_anim_hide(a, o, 1);
    if ((o = feng_find(pk, O_PAINT_ARROW_L)) >= 0) feng_anim_hide(a, o, 1);
    if ((o = feng_find(pk, O_PAINT_ARROW_R)) >= 0) feng_anim_hide(a, o, 1);
    paint_strip_update(s);
}
static void paint_enter(Shop *s) { paint_enter_with(s, k_paint_cat, N2_PAINT_COUNT, -1); }

/* 0x55cc50: to the swatches */
static void paint_swatches_open(Shop *s) {
    const FengPackage *pk = s->pk[PK_PAINT];
    FengAnim *a = s->paint_anim;
    int slot = paint_slot(s), ci = s->paintcat.sel;
    /* the brand of what is installed (+0x18c), else the category's */
    uint32_t inst = s->vmode >= 0 ? 0 : s->fitted.paint[ci], br;
    if (!inst && slot == 0x3f) inst = g_shop_stock_paint;
    if (s->vmode >= 0) s->vcol_open = *paint_ref(s, &s->preview);
    const unsigned char *r = db_part(s, inst);
    if (r && db_attr(s, r[10] | r[11] << 8, 0xebb03e66u, &br)) s->paint_brand = br;
    paint_build(s);
    if ((!s->paint_n && slot == 0x3f) || (slot == 0x45 && s->paint_brand != BR_GLOSS)) {
        s->paint_brand = BR_GLOSS;
        paint_build(s);
    }
    if (!s->paint_n || !pk || !a) return;
    s->paint_mode = 1;
    int o;
    if ((o = feng_find(pk, O_ICON_HL)) >= 0) feng_anim_hide(a, o, 1);
    if ((o = feng_find(pk, O_PAINT_HL)) >= 0) feng_anim_hide(a, o, 0);
    if ((o = feng_find(pk, O_PAINT_GRID)) >= 0) feng_anim_set(a, o, feng_hash_upper("FadeIn"), 1);
    if (slot == 0x3f || slot == 0x45 || slot == 0x4b || slot == 0x4c)
        for (int k = 0; k < 2; k++)
            if ((o = feng_find(pk, k ? O_PAINT_ARROW_R : O_PAINT_ARROW_L)) >= 0) {
                feng_anim_set(a, o, feng_hash_upper("FadeIn"), 1);
                feng_anim_hide(a, o, 0);
            }
    feng_anim_set(a, s->paint_obj[s->paint_sel], feng_hash_upper("Highlight"), 1);
    paint_cursor(s);
    paint_preview(s);
}

/* 0x55ce90: back to the categories (back: FadeOut, the installed colour again) */
static void paint_swatches_close(Shop *s, int back) {
    const FengPackage *pk = s->pk[PK_PAINT];
    FengAnim *a = s->paint_anim;
    s->paint_mode = 0;
    int o;
    if (pk && a) {
        if ((o = feng_find(pk, O_ICON_HL)) >= 0) feng_anim_hide(a, o, 0);
        if ((o = feng_find(pk, O_PAINT_HL)) >= 0) feng_anim_hide(a, o, 1);
        if (back) {
            if ((o = feng_find(pk, O_PAINT_GRID)) >= 0) feng_anim_set(a, o, feng_hash_upper("FadeOut"), 1);
            for (int k = 0; k < 2; k++)
                if ((o = feng_find(pk, k ? O_PAINT_ARROW_R : O_PAINT_ARROW_L)) >= 0) {
                    feng_anim_set(a, o, feng_hash_upper("FadeOut"), 1);
                    feng_anim_hide(a, o, 1);
                }
        }
        if (s->paint_n) feng_anim_set(a, s->paint_obj[s->paint_sel], feng_hash_upper("UnHighlight"), 1);
    }
    {   /* 0x55c460 */
        uint32_t want = s->vmode >= 0 ? s->vcol_open : s->fitted.paint[s->paintcat.sel];
        if (*paint_ref(s, &s->preview) != want) { *paint_ref(s, &s->preview) = want; request_preview(s); }
    }
    s->paint_brand = paint_cat_brand(paint_slot(s));
}

/* 0x522240 / 0x522340: the cursor through the grid */
static int paint_move(const Shop *s, int key) {
    int i = s->paint_sel, n = s->paint_n, c = PAINT_COLS;
    if (key == SHOP_KEY_LEFT) {
        if (i % c) return i - 1;
        if (i == n - n % c) return n - 1;
        return i - 1 + c < n ? i - 1 + c : n - 1;
    }
    if (key == SHOP_KEY_RIGHT) {
        if (i == n - 1) return n % c == 0 ? n - c : n - n % c;
        if ((i + 1) % c) return i + 1;
        return i - c + 1;
    }
    if (n <= c) return i;
    if (key == SHOP_KEY_UP) {
        if (i < c) { int j = n - n % c + i; return j >= n ? j - c : j; }
        return i - c;
    }
    if (key == SHOP_KEY_DOWN) {
        if (i < n - c) return i + c;
        int j = n % c - n + i; return j < 0 ? j + c : j;
    }
    return i;
}

static void vinyl_colours_done(Shop *s);
static void paint_input(Shop *s, int key) {
    int slot = paint_slot(s);
    if (!s->paint_mode) {
        if (key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) {
            scroller_move(&s->paintcat, key == SHOP_KEY_LEFT ? -1 : 1);
            s->paint_brand = paint_cat_brand(paint_slot(s));
            paint_strip_update(s);
        } else if (key == SHOP_KEY_OK) {
            if (!paint_can(s, slot)) {   /* 0x563410: 0x558430, sized by its length */
                dialog(s, DLG_AUTO, 1, paint_cant_msg(slot), DA_NONE, 0, 0);
                return;
            }
            paint_swatches_open(s);
        } else if (key == SHOP_KEY_BACK) {
            if (s->vmode >= 0) vinyl_colours_done(s);
            else s->screen = SC_GRAPHICS;
        }
        return;
    }
    if (key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT || key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) {
        int j = paint_move(s, key);
        if (j == s->paint_sel || j < 0 || j >= s->paint_n) return;
        feng_anim_set(s->paint_anim, s->paint_obj[s->paint_sel], feng_hash_upper("UnHighlight"), 1);
        s->paint_sel = j;
        feng_anim_set(s->paint_anim, s->paint_obj[j], feng_hash_upper("Highlight"), 1);
        paint_cursor(s);
        paint_preview(s);
    } else if (key == SHOP_KEY_LTRIG || key == SHOP_KEY_RTRIG) {
        /* 0x55d0d0: gloss / metallic / pearlescent */
        if (!(slot == 0x3f || slot == 0x45 || slot == 0x4b || slot == 0x4c)) return;
        uint32_t b = s->paint_brand, nb = b;
        if (key == SHOP_KEY_LTRIG) nb = b == BR_GLOSS ? BR_PEARL : b == BR_METAL ? BR_GLOSS : BR_METAL;
        else nb = b == BR_GLOSS ? BR_METAL : b == BR_METAL ? BR_PEARL : BR_GLOSS;
        if (b != BR_GLOSS && b != BR_METAL && b != BR_PEARL) return;
        if (s->paint_n) feng_anim_set(s->paint_anim, s->paint_obj[s->paint_sel], feng_hash_upper("UnHighlight"), 1);
        s->paint_brand = nb;
        paint_build(s);
        const FengPackage *pk = s->pk[PK_PAINT];
        int hl = pk ? feng_find(pk, O_PAINT_HL) : -1;
        if (hl >= 0 && s->paint_anim) {
            feng_anim_hide(s->paint_anim, hl, s->paint_n == 0);
            if (s->paint_n) feng_anim_set(s->paint_anim, hl, feng_hash_upper("BLINK"), 1);
        }
        if (s->paint_n) {
            feng_anim_set(s->paint_anim, s->paint_obj[s->paint_sel], feng_hash_upper("Highlight"), 1);
            paint_preview(s);
        }
        audio_fe_play(key == SHOP_KEY_LTRIG ? FE_SND_LEFT : FE_SND_RIGHT); s->snd_done = 1;
    } else if (key == SHOP_KEY_OK) {
        /* 0x55d410: "Add this paint to your current selection?" */
        if (!s->paint_n) return;
        /* a vinyl's: "Choose this vinyl color?" */
        dialog(s, PK_DLG_S, 2, s->vmode >= 0 ? 0x2a2c6aefu : 0xc4c5d271u, DA_PAINT_ADD, 0, 0);
    } else if (key == SHOP_KEY_BACK) {
        paint_swatches_close(s, 1);
    }
}
/* its yes (0xd05fc3a3): installed */
static void paint_add(Shop *s) {
    {
        if (s->vmode >= 0) s->vcol_open = s->paint_hash[s->paint_sel];   /* kept until the vinyl is added */
        else s->fitted.paint[s->paintcat.sel] = s->paint_hash[s->paint_sel];
        *paint_ref(s, &s->preview) = s->paint_hash[s->paint_sel];
        paint_swatches_close(s, 0);
        const FengPackage *pk = s->pk[PK_PAINT];
        int o;
        for (int k = 0; pk && s->paint_anim && k < 2; k++)
            if ((o = feng_find(pk, k ? O_PAINT_ARROW_R : O_PAINT_ARROW_L)) >= 0) feng_anim_hide(s->paint_anim, o, 1);
    }
}

/* ---------------------------------------------------------------- vinyls */

/* Graphics Shop -> Vinyls. UI_ChooseVinylLayer (0x548450, entries 0x522130,
   handler 0x56ab00 / 0x563100): the four layers VINYL_LAYER0..3 (car slots
   0x40..0x43), each shown with its vinyl's category icon (0x50e190), all
   with the Unique icon while a Unique vinyl is fitted. A layer with a vinyl
   asks "change or remove?" (0x6d7902c); L1 / R1 swap the layer with its
   neighbour (0x563290), not with a Unique fitted. Then the categories
   (0x546030, a category without vinyls locked) and a category's vinyls in
   UI_GenericParts_Browser, the cursor's vinyl on the car. A REMAP vinyl goes
   to its colours first (UI_Paint in vinyl mode: rows 1..NUMREMAPCOLOURS,
   PAINT_ICON_n, "Choose Vinyl Colors", the VINYL_PAINT swatches); the others
   ask "add this vinyl to your setup?" (0x559240, GenericDialog_MED). A
   Unique vinyl takes all four layers (0x547570). */
#define STR_VLAYER_TITLE 0x2ac7d02du   /* "Choose Vinyl Layer" */
#define STR_VCOL_TITLE   0xc034ce3du   /* "Choose Vinyl Colors" */
#define K_ICON_UNIQUE    0x1b3a8dd3u
#define O_VL_ARROW_L     0xcf6f0439u
#define O_VL_ARROW_R     0xaba93d3fu
#define O_VL_LTRIG       0x76bd10f1u
#define O_VL_RTRIG       0x00480e37u
#define O_VL_REORDER     0xd999b6e7u

/* the layer of a fitted Unique vinyl, -1 none */
static int vinyl_unique_at(const N2CarConfig *c) {
    for (int l = 0; l < VINYL_LAYERS; l++) {
        const VinylPart *v = vinyl_find(c->vinyl[l]);
        if (v && v->unique) return l;
    }
    return -1;
}
/* 0x501d70: the price row of a vinyl group */
static int vinyl_price_row(int g) {
    switch (g) {
    case 0x0e: return 0x38; case 0x0f: return 0x39; case 0x10: return 0x3a; case 0x11: return 0x3b;
    case 0x12: return 0x41; case 0x13: return 0x42; case 0x14: return 0x43; case 0x15: return 0x3c;
    case 0x16: return 0x3d; case 0x17: return 0x40; case 0x18: return 0x3e; case 0x19: return 0x37;
    case 0x1a: return 0x3f; case 0x1b: return 0x44;
    default: return 0x29 + g;
    }
}
static uint32_t vinyl_group_icon(int g) {
    for (int i = 0; i < N_VCAT; i++) if (k_vinyl_cat[i].group == g) return k_vinyl_cat[i].tex;
    return 0;
}

static void vlay_build(Shop *s) {
    int uq = vinyl_unique_at(&s->fitted);
    for (int l = 0; l < VINYL_LAYERS; l++) {
        ShopEntry *e = &s->vlay_e[l];
        e->str = 0x27b8f0ddu + (uint32_t)l; e->tex = e->str; e->group = 0x40 + l;
        const VinylPart *v = vinyl_find(s->fitted.vinyl[l]);
        if (uq >= 0) e->tex = K_ICON_UNIQUE;
        else if (v && vinyl_group_icon(v->group)) e->tex = vinyl_group_icon(v->group);
    }
    int sel = s->vlay.sel;
    float sc = s->vlay.scroll;
    scroller_set(&s->vlay, s->vlay_e, VINYL_LAYERS);
    s->vlay.sel = sel; s->vlay.scroll = sc;
}

static void vlayer_enter(Shop *s) {
    s->vmode = -1;
    memset(&s->vlay, 0, sizeof s->vlay);
    vlay_build(s);
    s->screen = SC_VLAYER;
}

static void vcat_enter(Shop *s) {
    for (int i = 0; i < N_VCAT; i++) s->vcat_e[i] = k_vinyl_cat[i];
    scroller_set(&s->vcat, s->vcat_e, N_VCAT);
    int first = -1;
    for (int i = 0; i < N_VCAT; i++) {
        int n = 0;
        for (int k = 0; k < vinyl_count() && !n; k++) n = vinyl_at(k)->group == k_vinyl_cat[i].group;
        s->vcat.locked[i] = !n;
        if (n && first < 0) first = i;
    }
    /* start on the fitted vinyl's category */
    const VinylPart *v = vinyl_find(s->fitted.vinyl[s->vlayer]);
    s->vcat.sel = first < 0 ? 0 : first;
    for (int i = 0; v && i < N_VCAT; i++) if (k_vinyl_cat[i].group == v->group) s->vcat.sel = i;
    s->vcat.scroll = (float)s->vcat.sel;
    s->screen = SC_VCAT;
}

/* the vinyl under the cursor on the car (0x55bd60 / 0x547330) */
static void vlist_preview(Shop *s) {
    N2CarConfig next = s->fitted;
    if (s->nparts > 0) {
        const ShopPart *q = &s->parts[s->part_sel];
        const VinylPart *v = vinyl_find(q->hash);
        int l = s->vlayer;
        if (q->hash != s->fitted.vinyl[l]) memset(next.vinyl_col[l], 0, sizeof next.vinyl_col[l]);
        next.vinyl[l] = q->hash;
        int uq = vinyl_unique_at(&s->fitted);
        if (uq >= 0 && uq != l) { next.vinyl[uq] = 0; memset(next.vinyl_col[uq], 0, sizeof next.vinyl_col[uq]); }
        if (v && v->unique)   /* it takes all four layers */
            for (int k = 0; k < VINYL_LAYERS; k++) if (k != l) { next.vinyl[k] = 0; memset(next.vinyl_col[k], 0, sizeof next.vinyl_col[k]); }
    }
    if (memcmp(&next, &s->preview, sizeof next)) { s->preview = next; request_preview(s); }
}

static void vlist_build(Shop *s) {
    int g = k_vinyl_cat[s->vcat.sel].group, row = vinyl_price_row(g);
    s->nparts = 0; s->part_sel = 0; s->part_group = -1;
    for (int i = 0; i < vinyl_count(); i++) {
        const VinylPart *v = vinyl_at(i);
        if (v->group != g) continue;
        if (s->nparts >= SHOP_MAX_PARTS) break;
        ShopPart *q = &s->parts[s->nparts];
        const char *nm = v->lang ? str_get(s, v->lang) : "";
        snprintf(q->name, sizeof q->name, "%s", nm[0] ? nm : v->name);
        q->value = i; q->hash = v->hash; q->available = 1;
        q->level = v->level;
        q->price = v->level >= 1 && v->level <= 3 && row < 96 ? s->price[row][v->level - 1] : 0;
        if (q->price < 0) q->price = 0;
        if (v->hash == s->fitted.vinyl[s->vlayer]) s->part_sel = s->nparts;
        s->nparts++;
    }
    s->part_scroll = (float)s->part_sel;
    printf("shop: vinyls group %d: %d\n", g, s->nparts);
}

static void vlist_enter(Shop *s) {
    vlist_build(s);
    s->screen = SC_VLIST;
    vlist_preview(s);
}

/* back out of the vinyls: the car shows what is fitted */
static void vinyl_revert(Shop *s) {
    if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
        s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
    }
}

/* the colours of the vinyl under the cursor (0x56ab00 0xc519bfc0) */
static void vinyl_colours_enter(Shop *s, const VinylPart *v) {
    int n = v->ncol < 1 ? 1 : v->ncol > 3 ? 3 : v->ncol;
    for (int c = 0; c < n; c++) {
        char pi[24]; snprintf(pi, sizeof pi, "PAINT_ICON_%d", c + 1);
        s->vcol_e[c].str = STR_VCOL_TITLE; s->vcol_e[c].tex = feng_tex_key(pi);
        s->vcol_e[c].group = 0x4d + s->vlayer * 4 + c;
    }
    paint_enter_with(s, s->vcol_e, n, s->vlayer);
}
/* out of the colours: add the vinyl with them? */
static void vinyl_colours_done(Shop *s) {
    s->vmode = -1;
    s->screen = SC_VLIST;
    dialog(s, PK_DLG_M, 2, 0x31fbc167u, DA_VINYL_ADD, 0, 0);
}

static void vinyl_install(Shop *s) {   /* 0x547570 */
    memcpy(s->fitted.vinyl, s->preview.vinyl, sizeof s->fitted.vinyl);
    memcpy(s->fitted.vinyl_col, s->preview.vinyl_col, sizeof s->fitted.vinyl_col);
    printf("shop: vinyl layer %d = %08x\n", s->vlayer + 1, (unsigned)s->fitted.vinyl[s->vlayer]);
    vlay_build(s);
    s->screen = SC_VLAYER;
}

static void vinyl_remove(Shop *s, int all) {
    for (int l = 0; l < VINYL_LAYERS; l++) {
        if (!all && l != s->vlayer) continue;
        s->fitted.vinyl[l] = 0; memset(s->fitted.vinyl_col[l], 0, sizeof s->fitted.vinyl_col[l]);
    }
    s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
    vlay_build(s);
}

/* 0x563290: L1 / R1 swap the layer with its neighbour */
static void vlay_swap(Shop *s, int step) {
    int a = s->vlay.sel, b = a + step;
    if (b < 0 || b >= VINYL_LAYERS || vinyl_unique_at(&s->fitted) >= 0 || !s->fitted.vinyl[a]) return;
    uint32_t t = s->fitted.vinyl[a]; s->fitted.vinyl[a] = s->fitted.vinyl[b]; s->fitted.vinyl[b] = t;
    uint32_t c[4]; memcpy(c, s->fitted.vinyl_col[a], sizeof c);
    memcpy(s->fitted.vinyl_col[a], s->fitted.vinyl_col[b], sizeof c); memcpy(s->fitted.vinyl_col[b], c, sizeof c);
    s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
    s->vlay.sel = b; s->vlay.pulse = 0.0f;
    vlay_build(s);
    audio_fe_play(step < 0 ? FE_SND_LEFT : FE_SND_RIGHT); s->snd_done = 1;
}

static void vinyl_input(Shop *s, int key) {
    switch (s->screen) {
    case SC_VLAYER:
        if (key == SHOP_KEY_LEFT) scroller_move(&s->vlay, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->vlay, 1);
        else if (key == SHOP_KEY_LTRIG || key == SHOP_KEY_RTRIG) vlay_swap(s, key == SHOP_KEY_LTRIG ? -1 : 1);
        else if (key == SHOP_KEY_OK) {   /* 0x563100 */
            s->vlayer = s->vlay.sel;
            if (vinyl_unique_at(&s->fitted) >= 0) {
                /* "Unique vinyls require all four vinyl layers ... change or remove this vinyl?" */
                s->vlayer = vinyl_unique_at(&s->fitted);
                dialog(s, DLG_AUTO, 2, 0x80c151d5u, DA_VINYL_CHANGE, 0x548b4385u, 0x7758f62du);
                s->dlg_act2 = DA_VINYL_UNIQUE;
            } else if (s->fitted.vinyl[s->vlayer]) {
                dialog(s, DLG_AUTO, 2, 0x06d7902cu, DA_VINYL_CHANGE, 0x548b4385u, 0x7758f62du);
                s->dlg_act2 = DA_VINYL_REMOVE;
            } else vcat_enter(s);
        } else if (key == SHOP_KEY_BACK) { s->vmode = -1; s->screen = SC_GRAPHICS; }
        break;
    case SC_VCAT:
        if (key == SHOP_KEY_LEFT) scroller_move(&s->vcat, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->vcat, 1);
        else if (key == SHOP_KEY_OK && !s->vcat.locked[s->vcat.sel]) vlist_enter(s);
        else if (key == SHOP_KEY_BACK) s->screen = SC_VLAYER;
        break;
    case SC_VLIST:
        if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && s->nparts > 0) {
            s->part_sel = (s->part_sel + (key == SHOP_KEY_UP ? -1 : 1) + s->nparts) % s->nparts;
            vlist_preview(s);
        } else if (key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) {
            int step = key == SHOP_KEY_LEFT ? -1 : 1;
            for (int t = 0; t < s->vcat.n; t++) {
                scroller_move(&s->vcat, step);
                if (!s->vcat.locked[s->vcat.sel]) break;
            }
            vlist_build(s);
            vlist_preview(s);
        } else if (key == SHOP_KEY_OK && s->nparts > 0) {
            const VinylPart *v = vinyl_find(s->parts[s->part_sel].hash);
            if (!v) break;
            if (v->remap) { vinyl_colours_enter(s, v); break; }
            if (v->hash == s->fitted.vinyl[s->vlayer]) { dialog(s, PK_DLG_S, 1, 0xda15d7e5u, DA_NONE, 0, 0); break; }
            int others = 0;
            for (int l = 0; l < VINYL_LAYERS; l++) others |= l != s->vlayer && s->fitted.vinyl[l];
            dialog(s, PK_DLG_M, 2, v->unique && others ? 0xec8616fdu : 0x31fbc167u, DA_VINYL_ADD, 0, 0);
        } else if (key == SHOP_KEY_BACK) {
            vinyl_revert(s);
            s->screen = SC_VCAT;
        }
        break;
    }
}

/* ---------------------------------------------------------------- window tint */

/* Car Specialties -> Window Tint: the WINDOW_TINT parts (group 53) in a
   parts browser over the Specialties strip; the car wears the one under
   the cursor. A part's value is its light material (0x6ba02c05); the
   stock one (WINDSHIELD) is the car's own glass, 0. */
#define G_WINDOW_TINT 53
static uint32_t tint_of(const ShopPart *q) { return q && q->hash != 0x471a1dcau /* WINDSHIELD */ ? q->hash : 0; }
static void tint_preview(Shop *s) {
    N2CarConfig next = s->fitted;
    if (s->nparts > 0) next.tint = tint_of(&s->parts[s->part_sel]);
    if (memcmp(&next, &s->preview, sizeof next)) { s->preview = next; request_preview(s); }
}
static void tint_enter(Shop *s) {
    s->nparts = 0; s->part_sel = 0; s->part_group = G_WINDOW_TINT;
    if (!s->db || !s->db_parts) return;
    for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len && s->nparts < SHOP_MAX_PARTS; p += 14) {
        const unsigned char *r = s->db + p;
        uint32_t hash = n2_u32(r);
        unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
        if (!hash && !soff) break;
        if (r[4] != G_WINDOW_TINT || db_has_attr(s, aoff, 0xF7934316u)) continue;   /* EXCLUDE_UG2 */
        uint32_t mat = 0, lang = 0;
        if (!db_attr(s, aoff, 0x6ba02c05u, &mat)) continue;
        db_attr(s, aoff, 0x4732da07u, &lang);
        ShopPart *q = &s->parts[s->nparts];
        memset(q, 0, sizeof *q);
        const char *nm = lang ? str_get(s, lang) : "";
        snprintf(q->name, sizeof q->name, "%s", nm[0] ? nm : s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?");
        q->hash = mat; q->available = 1;
        int lvl = ((r[5] | r[6] << 8) >> 5) & 7;
        q->level = lvl > 3 ? 3 : lvl;
        if (tint_of(q) == s->fitted.tint) s->part_sel = s->nparts;
        s->nparts++;
    }
    s->part_scroll = (float)s->part_sel;
    s->screen = SC_TINT;
    printf("shop: window tint: %d parts\n", s->nparts);
    tint_preview(s);
}
static void tint_input(Shop *s, int key) {
    if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && s->nparts > 0) {
        s->part_sel = (s->part_sel + (key == SHOP_KEY_UP ? -1 : 1) + s->nparts) % s->nparts;
        tint_preview(s);
    } else if (key == SHOP_KEY_OK && s->nparts > 0) {   /* as the parts browser: 0x566490 */
        if (tint_of(&s->parts[s->part_sel]) == s->fitted.tint) dialog(s, DLG_AUTO, 1, 0xda15d7e5u, DA_NONE, 0, 0);
        else dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_TINT, 0, 0);
    } else if (key == SHOP_KEY_BACK) {
        if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
            s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
        }
        s->screen = SC_SPECIALTIES;
    }
}

/* ---------------------------------------------------------------- lights */

/* Car Specialties -> Lights (slot 0x9f = HEADLIGHT_BULB, the same parts
   browser as Window Tint, 0x5597b0): the HEADLIGHT_BULB parts (group 56).
   A part's value is its colour packed as 0x634800 packs it for the
   headlight (RED | GREEN << 8 | BLUE << 16); STOCK is 0. */
#define G_HEADLIGHT_BULB 56
static void bulb_preview(Shop *s) {
    N2CarConfig next = s->fitted;
    if (s->nparts > 0) next.bulb = s->parts[s->part_sel].hash;
    if (memcmp(&next, &s->preview, sizeof next)) { s->preview = next; request_preview(s); }
}
static void bulb_enter(Shop *s) {
    s->nparts = 0; s->part_sel = 0; s->part_group = G_HEADLIGHT_BULB;
    if (!s->db || !s->db_parts) return;
    for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len && s->nparts < SHOP_MAX_PARTS; p += 14) {
        const unsigned char *r = s->db + p;
        uint32_t hash = n2_u32(r);
        unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
        if (!hash && !soff) break;
        if (r[4] != G_HEADLIGHT_BULB || db_has_attr(s, aoff, 0xF7934316u)) continue;   /* EXCLUDE_UG2 */
        uint32_t red = 0, green = 0, blue = 0, lang = 0;
        db_attr(s, aoff, 0x0000d99au, &red);
        db_attr(s, aoff, 0x02ddc8f0u, &green);
        db_attr(s, aoff, 0x00136707u, &blue);
        db_attr(s, aoff, 0x4732da07u, &lang);
        ShopPart *q = &s->parts[s->nparts];
        memset(q, 0, sizeof *q);
        const char *nm = lang ? str_get(s, lang) : "";
        snprintf(q->name, sizeof q->name, "%s", nm[0] ? nm : s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?");
        q->hash = lang == 0x03b83203u ? 0 : (red & 0xff) | (green & 0xff) << 8 | (blue & 0xff) << 16;   /* STOCK: 0 */
        q->available = 1;
        int lvl = ((r[5] | r[6] << 8) >> 5) & 7;
        q->level = lvl > 3 ? 3 : lvl;
        if (q->hash == s->fitted.bulb) s->part_sel = s->nparts;
        s->nparts++;
    }
    s->part_scroll = (float)s->part_sel;
    s->screen = SC_BULB;
    printf("shop: lights: %d bulbs\n", s->nparts);
    bulb_preview(s);
}
static void bulb_input(Shop *s, int key) {
    if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && s->nparts > 0) {
        s->part_sel = (s->part_sel + (key == SHOP_KEY_UP ? -1 : 1) + s->nparts) % s->nparts;
        bulb_preview(s);
    } else if (key == SHOP_KEY_OK && s->nparts > 0) {
        if (s->parts[s->part_sel].hash == s->fitted.bulb) dialog(s, DLG_AUTO, 1, 0xda15d7e5u, DA_NONE, 0, 0);
        else dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_BULB, 0, 0);
    } else if (key == SHOP_KEY_BACK) {
        if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
            s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
        }
        s->screen = SC_SPECIALTIES;
    }
}

/* ---------------------------------------------------------------- specialties */

/* Car Specialties -> Nitrous Purge (slot 0xa2), Hydraulics (0xa1), Spinners
   (0x1d), Split Hoods (9) and Doors (0xa0): the parts browser class
   0x53a9d0 over the Specialties strip, its list from 0x539960:
   slot 9 = the car's hoods of upgrade group 5 (upg & 0x1f); slot 0x1d = the
   wheels whose BRAND_NAME is SPINNER and whose OUTER_RADIUS is the car's
   rim size (record +0xdc); the others every part of the slot (groups 59
   NOS_PURGE, 58 HYDRAULICS, 57 DOOR_STYLE). An entry is locked (0x539d00)
   when the list holds no more than one part (none for slot 9); Nitrous
   Purge also needs a nitrous package of level 1 or 2 fitted. */
#define G_DOOR_STYLE 57
#define G_HYDRAULICS 58
#define G_NOS_PURGE  59
#define G_TRUNK_AUDIO 60   /* Audio (ICE, slot 0x22): the car's STOCK / STANDARD / TUNED / CUSTOM LAYOUT */
#define SPINNER_FOLDER "SPINNER"
static unsigned char g_shop_rec[N2_REC_SIZE]; static int g_shop_rec_ok;   /* the car record (shop_set_car_rec) */
/* ------------------------------------------------------------ trunk audio */

/* Car Specialties -> Trunk Audio (UI_ICEMAIN 0x5593e0, mode 0x838998;
   Audio entry 0x53aac0 / 0x55bfb0): with no layout or the STOCK one, mode 2
   -- the Trunk Layout list (slot 0x22) at once; else mode 3, the strip
   {Audio Components (class 0x79be4c -> mode 4), Trunk Layout}; mode 4 the
   strip "Trunk Slot %d" of every component slot 0x23..0x2e the layout
   offers (0x6103f0), each a parts list (UI_IcePartsOverlay) of its size's
   group (0x802960: S 61 / M 62 / L 63) from the generic AUDIO model, "NO
   COMPONENTS" (level 0) first. Back (0x50e460): mode 4 -> 3 -> the
   Specialties. */
#define STR_TRUNK_AUDIO  0x02aadac5u   /* "TRUNK AUDIO" (obj 0x42adb44c in modes 2..5) */
#define STR_ICE_SLOT  0x3f4b9e43u      /* "Trunk Slot %d" */
static int ice_comp_slot(int slot) { return slot >= 0x23 && slot <= 0x2e; }
static int ice_size(int slot) {        /* 0x630c39 / 0x802960 */
    static const unsigned char t[12] = { 0, 0, 1, 1, 2, 2, 0, 0, 0, 0, 1, 1 };
    return ice_comp_slot(slot) ? t[slot - 0x23] : 0;
}
static int ice_layout_level(const N2CarConfig *c) { return c->parts[N2_PART_AUDIO] > 0 ? c->parts[N2_PART_AUDIO] - 1 : 0; }
/* 0x6103f0: the slots a layout offers on a car (config +0 = the car type) */
static int ice_slot_ok(const char *car, int level, int slot) {
    if (!ice_comp_slot(slot) || level == 0) return 0;
    int suv = !strcmp(car, "HUMMER") || !strcmp(car, "NAVIGATOR") || !strcmp(car, "ESCALADE");
    static const char *const six[] = { "CIVIC", "CELICA", "SUPRA", "RSX", "350Z", "GTO", "MUSTANGGT", "LANCEREVO8", "IMPREZAWRX" };
    switch (level) {
    case 1: return slot <= 0x23 + (suv ? 3 : 1);
    case 2: return slot < (suv ? 0x29 : 0x27);
    case 3:
        if (suv) return slot < 0x2f;
        for (int i = 0; i < NELEM(six); i++) if (!strcmp(car, six[i])) return slot < 0x29;
        return slot < 0x28;
    default: return slot < 0x24;
    }
}
/* 0x53aea0: a slot's default, its first level-0 part ("NO COMPONENTS") */
static uint32_t ice_default(const Shop *s, int slot) {
    int model = db_model(s, "AUDIO"), group = 61 + ice_size(slot);
    for (long p = s->db_parts; model >= 0 && p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
        const unsigned char *r = s->db + p;
        if (!n2_u32(r) && !(r[8] | r[9] << 8)) break;
        if (r[4] == group && r[7] == model && (r[5] >> 5) == 0) return n2_u32(r);
    }
    return 0;
}
static void ice_menu(Shop *s, int mode);
static int spec_group(int slot) {
    switch (slot) {
    case 0xa2: return G_NOS_PURGE; case 0xa1: return G_HYDRAULICS; case 0xa0: return G_DOOR_STYLE;
    case 9: return G_HOOD; case 0x1d: return G_WHEEL; case 0x22: return G_TRUNK_AUDIO;
    default: return ice_comp_slot(slot) ? 61 + ice_size(slot) : -1;   /* TRUNK_AUDIO_COMP_SMALL/MEDIUM/LARGE */
    }
}
/* what a part stands for in N2CarConfig (purge: its hash; spinners: the rim style) */
static int spec_part_val(int slot, const ShopPart *q) { return slot == 0xa2 || ice_comp_slot(slot) ? (int)q->hash : q->value; }
static int spec_cur(const Shop *s, const N2CarConfig *c, int slot) {
    switch (slot) {
    case 0xa2: return (int)c->purge;
    case 0xa1: return (int)c->hydraulics;
    case 0xa0: return (int)c->doors;
    case 9:    return c->parts[N2_PART_HOOD];
    case 0x1d: return strcmp(s->rim_folder, SPINNER_FOLDER) ? -1 : s->rim_style;
    case 0x22: return c->parts[N2_PART_AUDIO] > 0 ? c->parts[N2_PART_AUDIO] - 1 : 0;
    }
    if (ice_comp_slot(slot))
        return (int)(c->audio_comp[slot - 0x23] ? c->audio_comp[slot - 0x23] : ice_default(s, slot));
    return -1;
}
static void spec_put(Shop *s, N2CarConfig *c, int slot, const ShopPart *q) {
    switch (slot) {
    case 0xa2: c->purge = q->hash; break;
    case 0xa1: c->hydraulics = (unsigned)q->value; break;
    case 0xa0: c->doors = (unsigned)q->value; break;
    case 9:    c->parts[N2_PART_HOOD] = q->value; c->carbon &= ~1u; break;
    case 0x22:   /* level n: the car's KIT0n_TRUNK_AUDIO; 0x5474a0: every component back to "NO COMPONENTS" */
        c->parts[N2_PART_AUDIO] = 1 + q->value;
        for (int i = 0; i < 12; i++) c->audio_comp[i] = ice_default(s, 0x23 + i);
        break;
    case 0x1d:   /* rims go through the rim loader (prev_folder / prev_style) */
        snprintf(s->prev_folder, sizeof s->prev_folder, "%s", SPINNER_FOLDER);
        s->prev_style = q->value;
        break;
    default:
        if (ice_comp_slot(slot)) c->audio_comp[slot - 0x23] = q->hash;
        break;
    }
}
/* the list of one slot; returns the count (also the lock test's) */
static int spec_build(Shop *s, int slot) {
    s->nparts = 0; s->part_sel = 0;
    int group = spec_group(slot), model = -1, rim = g_shop_rec_ok ? g_shop_rec[0xdc] : 0;
    if (group < 0 || !s->db || !s->db_parts) return 0;
    if (slot == 9 || slot == 0x22) model = db_model(s, s->car);
    if (slot == 0x1d) model = db_model(s, "WHEELS");
    if (ice_comp_slot(slot)) model = db_model(s, "AUDIO");   /* the generic components (0x6101e0) */
    if ((slot == 9 || slot == 0x1d || slot >= 0x22 && slot <= 0x2e) && model < 0) return 0;
    for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len && s->nparts < SHOP_MAX_PARTS; p += 14) {
        const unsigned char *r = s->db + p;
        uint32_t hash = n2_u32(r);
        int upg = r[5] | r[6] << 8;
        unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
        if (!hash && !soff) break;
        if (r[4] != group || db_has_attr(s, aoff, 0xF7934316u)) continue;   /* EXCLUDE_UG2 */
        if (model >= 0 && r[7] != model) continue;
        uint32_t lang = 0, v = 0;
        db_attr(s, aoff, 0x4732da07u, &lang);
        ShopPart *q = &s->parts[s->nparts];
        memset(q, 0, sizeof *q);
        q->hash = hash;
        switch (slot) {
        case 9:
            if ((upg & 0x1f) != 5) continue;
            q->value = part_value(s->car, G_HOOD, hash);
            if (q->value <= 0 || !menu_has(&s->menus[N2_PART_HOOD], q->value)) continue;   /* no geometry */
            break;
        case 0x1d: {
            uint32_t brand = 0, outer = 0;
            db_attr(s, aoff, 0xEBB03E66u, &brand); db_attr(s, aoff, 0xCE7D8DB5u, &outer);
            if (brand != 0xc8d1a75eu /* "SPINNER" */ || (rim && (int)outer != rim)) continue;
            int style, inner, out;   /* SPINNER_STYLEnn_<inner>_<outer> */
            if (!rim_part_parse(SPINNER_FOLDER, hash, &style, &inner, &out)) continue;
            int dup = 0;
            for (int k = 0; k < s->nparts; k++) if (RIM_STYLE(s->parts[k].value) == style) dup = 1;
            if (dup) continue;
            /* the style's first size, or the fitted one */
            q->value = !strcmp(s->rim_folder, SPINNER_FOLDER) && RIM_STYLE(s->rim_style) == style
                       ? s->rim_style : RIM_VALUE(style, inner);
            break;
        }
        case 0xa1: q->value = (upg >> 5) & 7; break;
        case 0x22: q->value = (upg >> 5) & 7; break;
        case 0xa0: db_attr(s, aoff, 0x38A30E75u, &v); q->value = (int)v; break;   /* ANIMSTYLE */
        default: q->value = (upg >> 5) & 7; break;
        }
        const char *nm = lang ? str_get(s, lang) : "";
        snprintf(q->name, sizeof q->name, "%s", nm[0] ? nm : s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?");
        int lvl = (upg >> 5) & 7;
        q->level = lvl > 3 ? 3 : lvl;
        q->available = 1;
        s->nparts++;
    }
    return s->nparts;
}
/* 0x539d00 */
static int spec_locked(Shop *s, int slot) {
    if (spec_group(slot) < 0) return 1;
    if (slot == 0xa2) {                   /* a nitrous package, level 1 or 2 (0x52a110) */
        PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit);
        if (perf_cat_level(&pp, 2) < 1) return 1;
    }
    int n = spec_build(s, slot);
    s->nparts = 0;
    return n <= (slot == 9 ? 0 : 1);
}
static void spec_preview(Shop *s) {
    if (!s->nparts) return;
    const ShopPart *q = &s->parts[s->part_sel];
    if (s->spec_slot == 0x1d) {
        spec_put(s, &s->preview, 0x1d, q);
        s->rim_pending = 1; s->rim_debounce = 0.35f;
        return;
    }
    N2CarConfig next = s->fitted;
    spec_put(s, &next, s->spec_slot, q);
    if (memcmp(&next, &s->preview, sizeof next)) { s->preview = next; request_preview(s); }
}
static void spec_revert(Shop *s) {
    if (s->spec_slot == 0x1d) {
        if (strcmp(s->prev_folder, s->rim_folder) || s->prev_style != s->rim_style) {
            memcpy(s->prev_folder, s->rim_folder, sizeof s->prev_folder);
            s->prev_style = s->rim_style; s->rim_pending = 1; s->rim_debounce = 0.0f;
        }
    } else if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
        s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
    }
}
static void spec_enter(Shop *s, int slot) {
    s->spec_slot = slot; s->spec_open = 0;
    s->part_group = spec_group(slot);
    spec_build(s, slot);
    int cur = spec_cur(s, &s->fitted, slot);
    for (int k = 0; k < s->nparts; k++) if (spec_part_val(slot, &s->parts[k]) == cur) s->part_sel = k;
    s->part_scroll = (float)s->part_sel;
    s->screen = SC_SPEC;
    printf("shop: specialties slot 0x%x: %d parts\n", slot, s->nparts);
    spec_preview(s);
}
static int spec_is_fitted(const Shop *s, const ShopPart *q) {
    return spec_part_val(s->spec_slot, q) == spec_cur(s, &s->fitted, s->spec_slot);
}
static void spec_input(Shop *s, int key) {
    if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && s->nparts > 0) {
        s->part_sel = (s->part_sel + (key == SHOP_KEY_UP ? -1 : 1) + s->nparts) % s->nparts;
        spec_preview(s);
    } else if (key == SHOP_KEY_OK && s->nparts > 0) {   /* as the parts browser: 0x566490 */
        if (spec_is_fitted(s, &s->parts[s->part_sel])) dialog(s, DLG_AUTO, 1, 0xda15d7e5u, DA_NONE, 0, 0);
        else if (s->spec_slot == 0x22)   /* 0x56a300: from STOCK / none; to a layout; to STOCK */
            dialog(s, DLG_AUTO, 2, ice_layout_level(&s->fitted) == 0 ? 0x113eab80u :
                   s->parts[s->part_sel].value ? 0x8e4312b0u : 0x266131ceu, DA_SPEC, 0, 0);
        else dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, s->spec_slot == 0x1d ? DA_RIM : DA_SPEC, 0, 0);
    } else if (key == SHOP_KEY_SQ && (s->spec_slot == 9 || s->spec_slot == 0xa0 || s->spec_slot == 0xa2)) {
        /* 0x568d30 0xc519bfc0: Split Hoods / Doors open or close the part
           (browser +0x54: 0 the hood, 2 both doors -> 0x521040); Nitrous
           Purge starts / stops its preview (+0x116: "SND: FEPurge" 0x458b30
           / 0x458c40, and car effect 13 on every update, 0x4c1920) */
        s->spec_open = !s->spec_open;
    } else if (key == SHOP_KEY_BACK) {
        spec_revert(s);
        s->spec_open = 0;
        s->screen = s->spec_ret;
    }
}
static void spec_install(Shop *s) {
    spec_put(s, &s->fitted, s->spec_slot, &s->parts[s->part_sel]);
    s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
    printf("shop: specialties slot 0x%x = %s (purge %08x hydraulics %u doors %u hood %d)\n", s->spec_slot,
           s->parts[s->part_sel].name, (unsigned)s->fitted.purge, s->fitted.hydraulics, s->fitted.doors,
           s->fitted.parts[N2_PART_HOOD]);
    s->screen = s->spec_ret;
    if (s->screen == SC_ICE) {          /* the strip follows the layout */
        int sel = s->icem.sel;
        ice_menu(s, s->ice_mode);
        if (sel < s->icem.n) s->icem.sel = sel;
    }
}

static void ice_menu(Shop *s, int mode) {
    s->ice_mode = mode;
    if (mode == 3) {
        memcpy(s->ice_e, k_ice_mode3, sizeof k_ice_mode3);
        scroller_set(&s->icem, s->ice_e, NELEM(k_ice_mode3));
    } else {
        int n = 0, lvl = ice_layout_level(&s->fitted);
        for (int slot = 0x23; slot <= 0x2e; slot++)
            if (ice_slot_ok(s->car, lvl, slot)) s->ice_e[n++] = (ShopEntry){ STR_ICE_SLOT, ICE_SLOT_ICON, slot };
        scroller_set(&s->icem, s->ice_e, n);
    }
    s->screen = SC_ICE;
}
static void ice_audio_enter(Shop *s) {                                   /* 0x55bfb0 */
    if (ice_layout_level(&s->fitted) == 0) {   /* mode 2: the one entry, Trunk Layout */
        s->ice_mode = 2; s->ice_e[0] = k_ice_mode3[1]; scroller_set(&s->icem, s->ice_e, 1);
        s->spec_ret = SC_SPECIALTIES; spec_enter(s, 0x22);
    }
    else ice_menu(s, 3);
}
static void ice_input(Shop *s, int key) {
    if (key == SHOP_KEY_LEFT) scroller_move(&s->icem, -1);
    else if (key == SHOP_KEY_RIGHT) scroller_move(&s->icem, 1);
    else if (key == SHOP_KEY_OK && s->icem.n) {
        int g = s->icem.e[s->icem.sel].group;
        if (g == ICE_GROUP_COMP) ice_menu(s, 4);
        else { s->spec_ret = SC_ICE; spec_enter(s, g); }
    } else if (key == SHOP_KEY_BACK) {
        if (s->ice_mode == 4) ice_menu(s, 3);
        else s->screen = SC_SPECIALTIES;
    }
}
/* "Trunk Slot %d": the slot's number from 1 */
static const char *ice_slot_label(Shop *s, int slot) {
    static char b[48];
    char f[48]; snprintf(f, sizeof f, "%s", str_get(s, STR_ICE_SLOT));
    char *pc = strstr(f, "%d");
    if (!pc) return str_get(s, STR_ICE_SLOT);
    *pc = 0;
    snprintf(b, sizeof b, "%s%d%s", f, slot - 0x22, pc + 2);
    return b;
}

/* ---------------------------------------------------------------- neon */

/* Car Specialties -> Neon (class 0x53aa80, UI_CustomNeonMain): Underglow
   Neon (slot 0x9a), Engine Neon (0x9b) and Trunk Neon (0x9d). Picking one
   (0x56a931): engine neon needs the engine (car slot 0xd) at level 3
   (0xd649be08) -- here the Performance Shop's engine category at level 3 --
   and trunk neon the trunk audio (slot 0x22) at level 3, the CUSTOM LAYOUT
   (0xa9e966d7). The parts browser (handler
   0x56a300) lists the NEON parts (group 54) for each; a part's colours as
   0x61fc20 reads them (RED .. ALPHA, RED2 .. ALPHA2; "* PULSE" parts fade
   their alpha between the two). Installing goes back to the strip
   (0x9f00f07 -> 0x6691c08e). */
#define G_NEON 54
static void neon_menu(Shop *s, int keep) {
    int sel = keep ? s->neonm.sel : 0;
    scroller_set(&s->neonm, k_neon_shop, NELEM(k_neon_shop));
    s->neonm.sel = sel;
    s->screen = SC_NEON;
}
static int neon_slot(const Shop *s) {
    return s->neonm.n ? s->neonm.e[s->neonm.sel].group : 0;
}
static void neon_preview(Shop *s) {
    N2CarConfig next = s->fitted;
    if (s->nparts > 0) {
        next.neon[neon_slot(s)][0] = s->parts[s->part_sel].hash;
        next.neon[neon_slot(s)][1] = (unsigned)s->parts[s->part_sel].value;
    }
    if (memcmp(&next, &s->preview, sizeof next)) { s->preview = next; request_preview(s); }
}
static void neon_enter(Shop *s) {
    int slot = neon_slot(s);
    s->nparts = 0; s->part_sel = 0; s->part_group = G_NEON;
    if (!s->db || !s->db_parts) return;
    for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len && s->nparts < SHOP_MAX_PARTS; p += 14) {
        const unsigned char *r = s->db + p;
        uint32_t hash = n2_u32(r);
        unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
        if (!hash && !soff) break;
        if (r[4] != G_NEON || db_has_attr(s, aoff, 0xF7934316u)) continue;   /* EXCLUDE_UG2 */
        static const uint32_t ak[8] = { 0x0000d99au, 0x02ddc8f0u, 0x00136707u, 0x026e1ac5u,     /* RED GREEN BLUE ALPHA */
                                        0x001c0d0cu, 0x5e96e722u, 0x02804819u, 0x50317397u };   /* RED2 .. ALPHA2 */
        uint32_t c[8], lang = 0, col[2] = { 0, 0 };
        for (int i = 0; i < 8; i++) { c[i] = 0; db_attr(s, aoff, ak[i], &c[i]); col[i / 4] |= (c[i] & 0xff) << (8 * (i % 4)); }
        db_attr(s, aoff, 0x4732da07u, &lang);
        ShopPart *q = &s->parts[s->nparts];
        memset(q, 0, sizeof *q);
        const char *nm = lang ? str_get(s, lang) : "";
        snprintf(q->name, sizeof q->name, "%s", nm[0] ? nm : s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?");
        q->hash = col[0]; q->value = (int)col[1];
        q->available = 1;
        int lvl = ((r[5] | r[6] << 8) >> 5) & 7;
        q->level = lvl > 3 ? 3 : lvl;
        if (q->hash == s->fitted.neon[slot][0] && (unsigned)q->value == s->fitted.neon[slot][1]) s->part_sel = s->nparts;
        s->nparts++;
    }
    s->part_scroll = (float)s->part_sel;
    s->screen = SC_NEON_LIST;
    printf("shop: neon slot %d: %d parts\n", slot, s->nparts);
    neon_preview(s);
}
static void neon_input(Shop *s, int key) {
    if (s->screen == SC_NEON) {
        if (key == SHOP_KEY_LEFT) scroller_move(&s->neonm, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->neonm, 1);
        else if (key == SHOP_KEY_OK) {
            int slot = neon_slot(s);
            PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit);
            if (slot == 1 && perf_cat_level(&pp, 3) < 3) dialog(s, DLG_AUTO, 1, 0xd649be08u, DA_NONE, 0, 0);
            else if (slot == 3 && (s->fitted.parts[N2_PART_AUDIO] > 0 ? s->fitted.parts[N2_PART_AUDIO] - 1 : 0) < 3)
                dialog(s, DLG_AUTO, 1, 0xa9e966d7u, DA_NONE, 0, 0);
            else neon_enter(s);
        } else if (key == SHOP_KEY_BACK) s->screen = SC_SPECIALTIES;
        return;
    }
    if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && s->nparts > 0) {
        s->part_sel = (s->part_sel + (key == SHOP_KEY_UP ? -1 : 1) + s->nparts) % s->nparts;
        neon_preview(s);
    } else if (key == SHOP_KEY_OK && s->nparts > 0) {
        const ShopPart *q = &s->parts[s->part_sel];
        int slot = neon_slot(s);
        if (q->hash == s->fitted.neon[slot][0] && (unsigned)q->value == s->fitted.neon[slot][1])
            dialog(s, DLG_AUTO, 1, 0xda15d7e5u, DA_NONE, 0, 0);
        else dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_NEON, 0, 0);
    } else if (key == SHOP_KEY_BACK) {
        if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
            s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
        }
        s->screen = SC_NEON;
    }
}

/* ---------------------------------------------------------------- custom gauges */

/* Car Specialties -> Custom Gauges (UI_ChooseCustomHUD under the part list
   UI_CustomHUDOverlay, handler 0x563d00): the CUSTOM_HUD styles (group 64,
   a part's HUDINDEX picks GLOBAL/HUD_CustomTextures_NN.bin); the preview
   swaps the tach art for the style's <NAME>_CUSTOM_NN textures (0x549000).
   Picking one asks for the colours layer by layer (0x5504d0: "Choose
   Backing Color", "Choose Dial Color", "Choose Needle Color") from the 12
   CUSTOM_HUD_PAINT parts (group 65); the preview paints the backing on
   Tach_Fill + Turbofill, the dial on 10500lines + Turbolines and the needle
   on rpmdial + TurboDial (0x53c620). The last one asks to install
   (0x51123592 / 0x9f00f07: slots 0xa3, 0xa4 backing, 0xa6 dial, 0xa5
   needle). */
#define G_CUSTOM_HUD 64
#define G_CUSTOM_HUD_PAINT 65
#define O_CHUD_PALETTE 0xdb285767u   /* the swatches, their bars and the prompt */
#define O_CHUD_PROMPT  0xdc0eba9cu
#define O_CHUD_HL      0x9ba6d658u   /* U2_Paint_HighLight */
#define O_CHUD_ARROW_L 0xd1cda594u
#define O_CHUD_ARROW_R 0x9345e4a7u
static const uint32_t k_chud_swatch[12] = {
    0x8d4e68d4u, 0x8d4e68d5u, 0x8d4e68d6u, 0x8d4e68d7u, 0x8d4e68d8u, 0x8d4e68d9u,
    0x8d4e68dau, 0x8d4e68dbu, 0x8d4e68dcu, 0x371b8384u, 0x371b8385u, 0x371b8386u };
/* the preview objects in the order of chud_tex, and their art's names */
static const uint32_t k_chud_obj[6] = { 0x05d19f25u, 0x6d5fe871u, 0x1d9051aau, 0x19c99185u, 0xf0250dacu, 0x6d5ece44u };
static const char *const k_chud_art[6] = { "TACH_FILL", "3RDPERSON_TURBOFILL", "3RDPERSON_10500LINES",
                                           "3RDPERSON_TURBOLINES", "3RDPERSON_RPMDIAL", "3RDPERSON_TURBODIAL" };
static int hud_style_sel(const Shop *s) { return s->nparts ? s->parts[s->part_sel].value : 0; }

/* the style's six textures, from its pack */
static void chud_load_style(Shop *s, int style) {
    if (style == s->chud_style && s->chud_tex[0]) return;
    glDeleteTextures(6, s->chud_tex); memset(s->chud_tex, 0, sizeof s->chud_tex);
    s->chud_style = style;
    char path[600];
    snprintf(path, sizeof path, "%s/GLOBAL/HUD_CustomTextures_%02d.bin", s->dataroot, style);
    long len = 0;
    unsigned char *d = n2_read_file(path, &len);
    if (!d) { printf("shop: %s not loaded\n", path); return; }
    uint32_t keys[6]; FengImage img[6];
    memset(img, 0, sizeof img);
    for (int k = 0; k < 6; k++) {
        char nm[48]; snprintf(nm, sizeof nm, "%s_CUSTOM_%02d", k_chud_art[k], style);
        keys[k] = feng_tex_key(nm);
    }
    int got = feng_tpk_load(d, len, keys, 6, img);
    for (int k = 0; k < 6; k++) {
        if (img[k].rgba) s->chud_tex[k] = upload_rgba(img[k].w, img[k].h, img[k].rgba);
        feng_image_free(&img[k]);
    }
    free(d);
    printf("shop: custom gauges style %02d: %d/6 textures\n", style, got);
}

/* the preview's colours: the layers picked so far, the one being chosen,
   the installed ones for the rest */
static void chud_paint(Shop *s) {
    const FengPackage *pk = s->pk[PK_CHUD];
    FengAnim *a = s->chud_anim;
    if (!pk || !a) return;
    unsigned col[3];
    for (int l = 0; l < 3; l++) {
        col[l] = s->screen == SC_HUD_COL && l < s->hud_layer ? s->hud_pick[l] : s->fitted.hud_col[l];
        if (s->screen == SC_HUD_COL && l == s->hud_layer && s->hud_npal) col[l] = s->hud_pal[s->hud_csel];
    }
    for (int k = 0; k < 6; k++) {
        int o = feng_find(pk, k_chud_obj[k]);
        unsigned c = col[k / 2];
        if (o >= 0) feng_anim_color(a, o, 0xff000000u | (c ? c & 0xffffffu : 0xffffffu));
    }
    chud_load_style(s, hud_style_sel(s));
}

/* the cursor on the colour, the prompt for the layer */
static void chud_cursor(Shop *s) {
    const FengPackage *pk = s->pk[PK_CHUD];
    FengAnim *a = s->chud_anim;
    if (!pk || !a) return;
    int hl = feng_find(pk, O_CHUD_HL), sw = feng_find(pk, k_chud_swatch[s->hud_csel]);
    if (hl < 0 || sw < 0) return;
    FengPlace pl; feng_anim_place(a, sw, &pl);
    /* the highlight is a root object: its data position is its place */
    feng_anim_pos(a, hl, pl.x, pl.y);
}

static void chud_palette(Shop *s, int show) {
    const FengPackage *pk = s->pk[PK_CHUD];
    FengAnim *a = s->chud_anim;
    if (!pk || !a) return;
    int o;
    /* 0x53c4a0 / 0x53c560: the palette's Show / Hide script */
    if ((o = feng_find(pk, O_CHUD_PALETTE)) >= 0) { feng_anim_hide(a, o, 0); feng_anim_set(a, o, feng_hash_upper(show ? "Show" : "Hide"), 1); }
    if ((o = feng_find(pk, O_CHUD_HL)) >= 0) feng_anim_hide(a, o, !show);
    if ((o = feng_find(pk, O_CHUD_ARROW_L)) >= 0) feng_anim_hide(a, o, !show);
    if ((o = feng_find(pk, O_CHUD_ARROW_R)) >= 0) feng_anim_hide(a, o, !show);
    for (int k = 0; show && k < 12; k++)
        if ((o = feng_find(pk, k_chud_swatch[k])) >= 0) {
            feng_anim_hide(a, o, k >= s->hud_npal);
            if (k < s->hud_npal) feng_anim_color(a, o, 0xff000000u | (s->hud_pal[k] & 0xffffffu));
        }
}

static void hud_enter(Shop *s) {
    s->nparts = 0; s->part_sel = 0; s->part_group = G_CUSTOM_HUD; s->hud_npal = 0;
    if (!s->db || !s->db_parts) return;
    for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
        const unsigned char *r = s->db + p;
        uint32_t hash = n2_u32(r);
        unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
        if (!hash && !soff) break;
        if ((r[4] != G_CUSTOM_HUD && r[4] != G_CUSTOM_HUD_PAINT) || db_has_attr(s, aoff, 0xF7934316u)) continue;
        if (r[4] == G_CUSTOM_HUD_PAINT) {   /* RED GREEN BLUE */
            uint32_t c[3] = { 0, 0, 0 };
            db_attr(s, aoff, 0x0000d99au, &c[0]); db_attr(s, aoff, 0x02ddc8f0u, &c[1]); db_attr(s, aoff, 0x00136707u, &c[2]);
            if (s->hud_npal < 12) s->hud_pal[s->hud_npal++] = 0x01000000u | (c[0] & 255) << 16 | (c[1] & 255) << 8 | (c[2] & 255);
            continue;
        }
        if (s->nparts >= SHOP_MAX_PARTS) continue;
        uint32_t idx = 0, lang = 0;
        db_attr(s, aoff, 0x311151f8u, &idx);   /* HUDINDEX */
        db_attr(s, aoff, 0x4732da07u, &lang);
        ShopPart *q = &s->parts[s->nparts];
        memset(q, 0, sizeof *q);
        const char *nm = lang ? str_get(s, lang) : "";
        snprintf(q->name, sizeof q->name, "%s", nm[0] ? nm : s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?");
        q->value = (int)idx; q->available = 1;
        int lvl = ((r[5] | r[6] << 8) >> 5) & 7;
        q->level = lvl > 3 ? 3 : lvl;
        if ((unsigned)q->value == s->fitted.hud) s->part_sel = s->nparts;
        s->nparts++;
    }
    s->part_scroll = (float)s->part_sel;
    s->screen = SC_HUD;
    const FengPackage *pk = s->pk[PK_CHUD];
    if (pk && !s->chud_anim) s->chud_anim = (FengAnim *)malloc(sizeof *s->chud_anim);
    if (pk && s->chud_anim) {
        feng_anim_init(s->chud_anim, pk); anim_fadein(s->chud_anim);
        int o = feng_find(pk, 0xed03d1d3u);   /* 0x549000: the preview shown */
        if (o >= 0) { feng_anim_hide(s->chud_anim, o, 0); feng_anim_set(s->chud_anim, o, feng_hash_upper("Show"), 1); }
    }
    chud_palette(s, 0);
    chud_paint(s);
    printf("shop: custom gauges: %d styles, %d colours\n", s->nparts, s->hud_npal);
}

/* the layer's colour list, the cursor on what it has */
static void hud_layer_open(Shop *s, int layer) {
    s->hud_layer = layer;
    unsigned cur = s->fitted.hud_col[layer];
    s->hud_csel = 0;
    for (int k = 0; k < s->hud_npal; k++) if (s->hud_pal[k] == cur) s->hud_csel = k;
    s->screen = SC_HUD_COL;
    chud_palette(s, 1);
    chud_cursor(s);
    chud_paint(s);
}

static void hud_input(Shop *s, int key) {
    if (s->screen == SC_HUD) {
        if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && s->nparts > 0) {
            s->part_sel = (s->part_sel + (key == SHOP_KEY_UP ? -1 : 1) + s->nparts) % s->nparts;
            chud_paint(s);
        } else if (key == SHOP_KEY_OK && s->nparts > 0 && hud_style_sel(s) == 0) {
            memset(s->hud_pick, 0, sizeof s->hud_pick);
            if (s->fitted.hud == 0 && !memcmp(s->hud_pick, s->fitted.hud_col, sizeof s->hud_pick))
                dialog(s, DLG_AUTO, 1, 0xda15d7e5u, DA_NONE, 0, 0);
            else dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_HUD, 0, 0);
        } else if (key == SHOP_KEY_OK && s->nparts > 0 && s->hud_npal > 0) {
            hud_layer_open(s, 0);
        } else if (key == SHOP_KEY_BACK) {
            chud_palette(s, 0);
            s->screen = SC_SPECIALTIES;
        }
        return;
    }
    if (key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) {
        s->hud_csel = (s->hud_csel + (key == SHOP_KEY_LEFT ? -1 : 1) + s->hud_npal) % s->hud_npal;
        chud_cursor(s);
        chud_paint(s);
    } else if (key == SHOP_KEY_OK) {
        s->hud_pick[s->hud_layer] = s->hud_pal[s->hud_csel];
        if (s->hud_layer < 2) hud_layer_open(s, s->hud_layer + 1);
        else if (hud_style_sel(s) == (int)s->fitted.hud && !memcmp(s->hud_pick, s->fitted.hud_col, sizeof s->hud_pick))
            dialog(s, DLG_AUTO, 1, 0xda15d7e5u, DA_NONE, 0, 0);
        else dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_HUD, 0, 0);
    } else if (key == SHOP_KEY_BACK) {
        if (s->hud_layer > 0) hud_layer_open(s, s->hud_layer - 1);
        else { s->screen = SC_HUD; chud_palette(s, 0); chud_paint(s); }
    }
}

/* ---------------------------------------------------------------- decals */

/* Graphics Shop -> Decals. UI_DecalMain (0x5530f0, handler 0x56b3b0 /
   0x5682a0, levels 0x560020): a strip of locations, then a door's slots or
   a quarter panel's two layouts (Layout 1: one MEDIUM slot, Layout 2: two
   SMALL; another layout asks first when the panel has decals, 0x10b83327),
   then its slots. A slot opens UI_DecalsOverlay (0x568450, list 0x560250,
   handler 0x56b960): "No Decal" then every decal, the cursor's on the car
   (0x631d20); X installs it ("install this part now?") or empties the slot
   ("remove this decal?" / "already empty"); Square on a location or a
   layout empties it ("remove all decals in this layout?"). */
static int decal_loc_of(int slot) { return (slot - 0x61) / 8; }
static int decal_n_of(int slot) { return (slot - 0x61) % 8; }
/* the layout a quarter-panel level stands for: (loc, 1 / 2), 0 none */
static int decal_layout_of(int id, int *loc) {
    switch (id) {
    case 0x20006: *loc = 5; return 1; case 0x20007: *loc = 5; return 2;
    case 0x20008: *loc = 6; return 1; case 0x20009: *loc = 6; return 2;
    }
    return 0;
}
/* the location a menu entry stands for, -1 none */
static int decal_entry_loc(int group) {
    if (group >= 0x61 && group < 0x99) return decal_loc_of(group);
    switch (group) {
    case 0x10001: return 3; case 0x10002: return 4; case 0x10003: case 0x20006: case 0x20007: return 5;
    case 0x10004: case 0x20008: case 0x20009: return 6;
    }
    return -1;
}
static int decal_loc_used(const N2CarConfig *c, int loc) {
    for (int n = 0; n < 8; n++) if (c->decal[loc][n]) return 1;
    return 0;
}
static void dloc_level(Shop *s, int id, int sel) {
    for (int l = 0; l < NELEM(k_decal_level); l++)
        if (k_decal_level[l].id == id) {
            scroller_set(&s->dloc, k_decal_level[l].e, k_decal_level[l].n);
            s->dloc.sel = sel < s->dloc.n ? sel : 0; s->dloc.scroll = (float)s->dloc.sel;
            s->dlev = id;
            return;
        }
}
static void dloc_enter(Shop *s) {
    s->ndst = 0;
    dloc_level(s, DL_ROOT, 0);
    s->screen = SC_DLOC;
}
static void dloc_down(Shop *s, int id) {
    if (s->ndst < 4) { s->dstack[s->ndst] = s->dlev; s->dsel[s->ndst++] = s->dloc.sel; }
    dloc_level(s, id, 0);
}
static void dloc_up(Shop *s) {
    if (!s->ndst) { s->screen = SC_GRAPHICS; return; }
    s->ndst--;
    dloc_level(s, s->dstack[s->ndst], s->dsel[s->ndst]);
}

/* the decal under the cursor on the car */
static void dpick_preview(Shop *s) {
    N2CarConfig next = s->fitted;
    int loc = decal_loc_of(s->dslot), n = decal_n_of(s->dslot);
    if (s->nparts > 0) next.decal[loc][n] = s->parts[s->part_sel].hash;
    if (memcmp(&next, &s->preview, sizeof next)) { s->preview = next; request_preview(s); }
}
static void dpick_enter(Shop *s, int slot) {   /* 0x560250 */
    s->dslot = slot;
    s->nparts = 0; s->part_sel = 0; s->part_group = -1;
    ShopPart *q = &s->parts[s->nparts++];
    memset(q, 0, sizeof *q);
    snprintf(q->name, sizeof q->name, "%s", str_get(s, 0x2b5abcf4u));   /* "No Decal" */
    q->available = 1;
    uint32_t cur = s->fitted.decal[decal_loc_of(slot)][decal_n_of(slot)];
    for (int i = 0; i < decal_count() && s->nparts < SHOP_MAX_PARTS; i++) {
        const DecalPart *d = decal_at(i);
        q = &s->parts[s->nparts];
        memset(q, 0, sizeof *q);
        const char *nm = d->lang ? str_get(s, d->lang) : "";
        snprintf(q->name, sizeof q->name, "%s", nm[0] ? nm : d->dbg);
        q->hash = d->name; q->value = i; q->available = 1;
        if (cur && d->name == cur) s->part_sel = s->nparts;
        s->nparts++;
    }
    s->part_scroll = (float)s->part_sel;
    s->screen = SC_DPICK;
    printf("shop: decal slot %02x: %d decals\n", slot, s->nparts - 1);
    dpick_preview(s);
}
static void decal_revert(Shop *s) {
    if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
        s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
    }
}
static void decal_set(Shop *s, int slot, uint32_t name) {
    s->fitted.decal[decal_loc_of(slot)][decal_n_of(slot)] = name;
    s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
    printf("shop: decal %02x = %08x\n", slot, (unsigned)name);
}
/* a layout chosen: its location's part (0x521940), the old one's decals off */
static void decal_layout_take(Shop *s) {
    int loc = s->dlay_loc;
    if ((s->fitted.decal_layout[loc] ? s->fitted.decal_layout[loc] : 1) != s->dlay_want)
        memset(s->fitted.decal[loc], 0, sizeof s->fitted.decal[loc]);
    s->fitted.decal_layout[loc] = (unsigned char)s->dlay_want;
    s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
    dloc_down(s, s->dlay_next);
}
static void decal_input(Shop *s, int key) {
    switch (s->screen) {
    case SC_DLOC: {
        const ShopEntry *e = s->dloc.n ? &s->dloc.e[s->dloc.sel] : NULL;
        if (key == SHOP_KEY_LEFT) scroller_move(&s->dloc, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->dloc, 1);
        else if (key == SHOP_KEY_OK && e) {
            if (e->group < 0x10000) dpick_enter(s, e->group);
            else {
                int loc, lay = decal_layout_of(e->group, &loc);
                if (lay) {   /* 0x53a7c0 / 0x53a890 */
                    s->dlay_loc = loc; s->dlay_want = lay; s->dlay_next = e->group;
                    int cur = s->fitted.decal_layout[loc] ? s->fitted.decal_layout[loc] : 1;
                    if (cur != lay && decal_loc_used(&s->fitted, loc))
                        dialog(s, PK_DLG_S, 2, 0x10b83327u, DA_DECAL_LAYOUT, 0, 0);
                    else decal_layout_take(s);
                } else dloc_down(s, e->group);
            }
        } else if (key == SHOP_KEY_SQ && e && e->group >= 0x10000) {   /* 0xc519bfc0 */
            int loc = decal_entry_loc(e->group);
            if (loc >= 0 && decal_loc_used(&s->fitted, loc)) {
                s->dlay_loc = loc;
                dialog(s, PK_DLG_S, 2, 0x30f7037fu, DA_DECAL_CLEAR, 0, 0);
            }
        } else if (key == SHOP_KEY_BACK) dloc_up(s);
        break;
    }
    case SC_DPICK:
        if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && s->nparts > 0) {
            s->part_sel = (s->part_sel + (key == SHOP_KEY_UP ? -1 : 1) + s->nparts) % s->nparts;
            dpick_preview(s);
        } else if (key == SHOP_KEY_OK && s->nparts > 0) {   /* 0x56b960 0xc407210 */
            uint32_t cur = s->fitted.decal[decal_loc_of(s->dslot)][decal_n_of(s->dslot)];
            uint32_t want = s->parts[s->part_sel].hash;
            if (s->part_sel == 0) {
                if (!cur) dialog(s, PK_DLG_S, 1, 0x091bb4eau, DA_NONE, 0, 0);       /* already empty */
                else dialog(s, PK_DLG_S, 2, 0x74e34714u, DA_DECAL_REMOVE, 0, 0);    /* remove this decal? */
            } else if (want == cur) dialog(s, PK_DLG_S, 1, 0xda15d7e5u, DA_NONE, 0, 0);
            else dialog(s, PK_DLG_S, 2, 0xf5ac2b2au, DA_DECAL_ADD, 0, 0);
        } else if (key == SHOP_KEY_BACK) {
            decal_revert(s);
            s->screen = SC_DLOC;
        }
        break;
    }
}

/* ---------------------------------------------------------------- performance shop */

/* UI_ChoosePerformancePackage over a category of UI_ChoosePerformanceCategory
   (0x5304f0, handler 0x55c520). The package screen: ctor 0x559c90, build
   0x556e60, handler 0x561d10, update 0x5620d0. Its entries: Choose
   Individual Parts (type 0), the Street / Pro / Extreme Packages (type 1:
   0x500100(cat, level 1..3)) and Return To Stock (type 2). A package
   highlighted shows its contents (0x547b60: up to four PartName_n with
   PartPrice_n "Installed" -- Purchased script -- or "Not Installed",
   0x53b7b0 outside the career) and the stat sliders with the package added
   to the installed parts (0x521340 on a copy); Return To Stock shows them
   with the category's parts off (0x521a20). Customize is no career: the
   cost, discount and total strings are hidden (0x5620d0). */
#define O_PERF_HEADER   0x42adb44cu   /* "Performance Header": the category (0x50f1e0) */
#define O_PERF_TITLE    0x0666176bu   /* the entry's name */
#define O_PERF_EVENTS   0x47ff4e7cu   /* the Event Handler: PackageFadeIn / Out */
#define O_PERF_CONTENTS 0x3d3f9f04u   /* the package contents group */

/* The garage camera's view for what the shop shows (0x4c2100 with the
   highlighted entry's car slot, +0x48 of the menu item): 0 the default
   view; a vinyl's view also carries its group (0x4a5510: the Hoods and
   the Sponsor..Contest groups have their own views). */
static int view_body_slot(int group) {
    switch (group) {
    case G_LEFT_MIRROR: return 0x20;   /* Side Mirror */
    case G_WHEEL: return 0x1d;         /* Front Wheel */
    case G_CARBON: return 0x5d;        /* Kit Carbon Fibre */
    default: return group;             /* the part groups are the slots */
    }
}
static int view_vinyl(uint32_t part) {
    const VinylPart *v = vinyl_find(part);
    return 0x40 | (v ? (v->group + 1) << 16 : 0);
}
unsigned shop_part_attrs(const char *dataroot, uint32_t part, const uint32_t *keys, int n, uint32_t *vals) {
    static Shop *db;   /* the database alone, loaded on first use */
    if (!db) {
        db = (Shop *)calloc(1, sizeof *db);
        if (!db) return 0;
        snprintf(db->dataroot, sizeof db->dataroot, "%s", dataroot);
        load_db(db);
    }
    const unsigned char *r = db_part(db, part);
    if (!r) return 0;
    unsigned aoff = r[10] | r[11] << 8, mask = 0;
    for (int i = 0; i < n && i < 32; i++) { vals[i] = 0; if (db_attr(db, aoff, keys[i], &vals[i])) mask |= 1u << i; }
    return mask;
}
/* 0x6337e0: a part's model name from its model-table record (chunk
   0x3460a, 0x24 bytes: u16 1 = string names, u16 base, u32 names[4] of
   model 0, names[4] of model 1 at +0x14), without the LOD letter: the
   part's model (+7, when +6 is 1) or the car, + base + names[model]. */
static Shop *g_db_only;
int shop_part_model(const char *dataroot, uint32_t part, int model, char *out, int cap) {
    if (!g_db_only) {
        g_db_only = (Shop *)calloc(1, sizeof *g_db_only);
        if (!g_db_only) return 0;
        snprintf(g_db_only->dataroot, sizeof g_db_only->dataroot, "%s", dataroot);
        load_db(g_db_only);
    }
    const Shop *db = g_db_only;
    const unsigned char *r = db_part(db, part);
    long tab = -1, tlen = 0;
    for (long o = 0; db->db && o + 8 <= db->dblen; ) {   /* the model table */
        uint32_t id = n2_u32(db->db + o), cs = n2_u32(db->db + o + 4);
        if (id == 0x0003460Au) { tab = o + 8; tlen = cs; break; }
        o += 8 + (long)cs;
    }
    if (!r || tab < 0 || !db->db_str || r[6] != 1) return 0;
    long m = tab + 0x24L * (r[12] | r[13] << 8);
    if (m + 0x24 > tab + tlen || (db->db[m] | db->db[m + 1] << 8) != 1) return 0;
    unsigned base = db->db[m + 2] | db->db[m + 3] << 8, nm = n2_u32(db->db + m + 4 + (model ? 0x10 : 0));
    if (!nm || r[7] >= db->nmodels) return 0;
    char lib[32] = "";   /* the 0x3460b entry is a hash: the generic models by name */
    static const char *const names[] = { "AUDIO", "WHEELS", "SPOILER", "SPOILER_HATCH", "SPOILER_SUV", "EXHAUST", "ROOF" };
    for (int i = 0; i < NELEM(names); i++)
        if (feng_hash(names[i]) == n2_u32(db->db + db->db_models + 4L * r[7])) snprintf(lib, sizeof lib, "%s", names[i]);
    if (!lib[0]) return 0;
    snprintf(out, (size_t)cap, "%s%s%s", lib, (const char *)db->db + db->db_str + base * 4L,
             (const char *)db->db + db->db_str + nm * 4L);
    return 1;
}
unsigned shop_purge_preview(Shop *s) {
    return s && s->screen == SC_SPEC && s->spec_open && s->spec_slot == 0xa2 ? s->preview.purge : 0;
}
int shop_parts_open(Shop *s, int *hood_style, int *hood_flag, int *door_style) {
    *hood_style = 0; *hood_flag = 0; *door_style = 0;
    if (!s) return 0;
    int m = 0;
    if (s->screen == SC_SPEC && s->spec_open && s->spec_slot == 9) m |= 1;
    if (s->screen == SC_SPEC && s->spec_open && s->spec_slot == 0xa0) m |= 4;
    if (s->screen == SC_SPEC && s->spec_slot >= 0x22 && s->spec_slot <= 0x2e) m |= 2;
    if (s->screen == SC_ICE) m |= 2;   /* UI_ICEMAIN modes 2..5: 0x4c1860(1, 1, 1.0) */
    if (s->screen == SC_NEON_LIST && neon_slot(s) == 1) m |= 1;
    if (s->screen == SC_NEON_LIST && neon_slot(s) == 3) m |= 2;
    if ((shop_view(s) & 0xffff) == 0x44) m |= 1;
    /* 0x61f820: the hood part's upgrade group 5 = split; VERTSPLIT front/back */
    int hv = s->preview.parts[N2_PART_HOOD], model = hv > 0 ? db_model(s, s->car) : -1;
    static int cache_v = -1, cache_style, cache_flag; static char cache_car[32];
    if (hv > 0 && (hv != cache_v || strcmp(cache_car, s->car))) {
        cache_v = hv; snprintf(cache_car, sizeof cache_car, "%s", s->car);
        cache_style = 0; cache_flag = 0;
        for (long p = s->db_parts; model >= 0 && p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
            const unsigned char *r = s->db + p;
            uint32_t hash = n2_u32(r);
            unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
            if (!hash && !soff) break;
            if (r[4] != G_HOOD || r[7] != model || ((r[5] | r[6] << 8) & 0x1f) != 5) continue;
            if (part_value(s->car, G_HOOD, hash) != hv) continue;
            uint32_t vs = 0;
            cache_style = db_attr(s, aoff, 0x3da5adacu, &vs) && vs ? 4 : 2;   /* VERTSPLIT */
            char nm[64]; snprintf(nm, sizeof nm, "%s_STYLE22_HOOD", s->car);
            cache_flag = hash == feng_hash(nm) || hash == feng_hash(strcat(nm, "_CF"));
            break;
        }
    }
    if (hv > 0) { *hood_style = cache_style; *hood_flag = cache_flag; }
    *door_style = (int)s->preview.doors;
    if (*door_style == 1 && s->preview.parts[N2_PART_WIDE] > 1) *door_style = s->preview.parts[N2_PART_WIDE] + 1;
    return m;
}

int shop_view(const Shop *s) {
    if (!s) return 0;
    int v = 0;
    switch (s->screen) {
    case SC_BODY: v = s->body.n ? view_body_slot(s->body.e[s->body.sel].group) : 0; break;
    case SC_PARTS: v = view_body_slot(s->part_group); break;
    case SC_RIM_BRAND: case SC_RIMS: v = 0x1d; break;
    case SC_PAINT:
        v = s->vmode >= 0 ? view_vinyl(s->fitted.vinyl[s->vmode])
          : s->paintcat.n ? s->paintcat.e[s->paintcat.sel].group : 0x3f;
        break;
    case SC_VLAYER: v = s->vlay.n ? view_vinyl(s->fitted.vinyl[s->vlay.sel]) : 0x40; break;
    case SC_VCAT: v = 0x40 | (s->vcat.n ? (s->vcat.e[s->vcat.sel].group + 1) << 16 : 0); break;
    case SC_VLIST: v = s->nparts ? view_vinyl(s->parts[s->part_sel].hash) : 0x40; break;
    case SC_DLOC: {
        const ShopEntry *e = s->dloc.n ? &s->dloc.e[s->dloc.sel] : NULL;
        int loc = !e ? -1 : e->group < 0x10000 ? decal_loc_of(e->group) : decal_entry_loc(e->group);
        v = loc >= 0 ? 0x34 + loc : 0;
        break;
    }
    case SC_DPICK: v = 0x34 + decal_loc_of(s->dslot); break;
    case SC_SPECIALTIES: v = s->cat.n ? s->cat.e[s->cat.sel].group : 0; break;
    case SC_TINT: v = 0x99; break;
    case SC_BULB: v = 0x9f; break;
    case SC_SPEC: v = s->spec_slot > 0x22 && s->spec_slot <= 0x2e ? 0x22 : s->spec_slot; break;
    case SC_ICE: v = 0x22; break;
    case SC_NEON: case SC_NEON_LIST: v = 0x9a + neon_slot(s); break;
    case SC_CARBON: v = s->cfm.n ? view_body_slot(s->cfm.e[s->cfm.sel].group) : 0x5d; break;
    default: v = 0; break;
    }
    return v;
}

static unsigned char g_shop_rec[N2_REC_SIZE]; static int g_shop_rec_ok;
void shop_set_car_rec(const unsigned char *rec) {
    g_shop_rec_ok = rec != NULL;
    if (rec) memcpy(g_shop_rec, rec, sizeof g_shop_rec);
}

static uint32_t perf_cat_label(int cat) {                                 /* 0x50f1e0 */
    static const uint32_t l[9] = { 0x758d5e3cu, 0x73ad2f72u, 0xa7886f38u, 0x026eea5eu, 0xabd0ff14u,
                                   0x9c34c75fu, 0xaa8959adu, 0xa7884765u, 0xabca6aefu };
    return cat >= 0 && cat < 9 ? l[cat] : 0;
}

/* the three sliders (0x547a00): backing, handle, cur, end cap, preview */
static const uint32_t k_perf_slider[3][5] = {
    { 0x70187fe5u, 0xdb1ce262u, 0x30cf3780u, 0x75773b40u, 0xcd99bd38u },   /* accel_ */
    { 0x2c2f6ab0u, 0xd14c1fcdu, 0x164274cbu, 0x318e260bu, 0x89b0a803u },   /* top_speed_ */
    { 0xaeee7572u, 0x1b13da4fu, 0x4ff66e8du, 0xb44d30cdu, 0x0c6fb2c5u },   /* handling_ */
};

/* 0x54ff90's three values of a parts set: acceleration 0x5b53a0, top speed
   0x5b6a20, handling 0x5b5490 of the record rebuilt with them */
static void perf_stats_of(const unsigned char *fit, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    if (!g_shop_rec_ok || !perf_ready()) return;
    static unsigned char built[N2_REC_SIZE];
    PerfParts pp; perf_clear(&pp);
    memcpy(pp.fit, fit, sizeof pp.fit);
    perf_build(g_shop_rec, &pp, built);
    perf_car_stats(built, out);
}

/* 0x527170: one slider -- value (the previewed parts) against cur (the
   installed ones), 0..1. The fill runs from 46 + 8 in from the backing's
   left to the smaller of the two; the preview bar from there over the
   difference (ToGreater when the value is below, ToLesser above, hidden
   when they agree); the handle at the value. */
static void perf_slider_place(Shop *s, int p, FengAnim *a, const uint32_t ob[5], float value, float cur) {
    const FengPackage *pk = s->pk[p];
    if (!pk || !a) return;
    int ib = feng_find(pk, ob[0]), ih = ob[1] ? feng_find(pk, ob[1]) : -1,
        ic = feng_find(pk, ob[2]), ie = feng_find(pk, ob[3]), ip = feng_find(pk, ob[4]);
    if (ib < 0) return;
    const FengObj *b = &pk->obj[ib];
    float L = b->pos[0] - 0.5f * fabsf(b->size[0]), T = b->pos[1] - 0.5f * fabsf(b->size[1]);
    float len = fabsf(b->size[0]) - 92.0f - 8.0f;   /* +0x3c (width - 2 x 46) - 8 */
    if (len < 0.0f) len = 0.0f;
    if (value > 1.0f) value = 1.0f;                  /* 0x54ff90: clamped to the range */
    if (value < 0.0f) value = 0.0f;
    if (ie >= 0) {
        const FengObj *o = &pk->obj[ie];
        feng_anim_pos(a, ie, L + 46.0f - 4.0f + 0.5f * fabsf(o->size[0]), T + 4.0f + 0.5f * fabsf(o->size[1]));
    }
    float diff = value - cur;
    if (ip >= 0) {
        if (diff != 0.0f) {
            feng_anim_hide(a, ip, 0);
            feng_anim_set(a, ip, feng_hash_upper(diff < 0.0f ? "ToGreater" : "ToLesser"), 1);
        } else feng_anim_hide(a, ip, 1);
    }
    float f = value < cur ? value : cur;
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    float fw = f * len, fl = L + 46.0f + 8.0f, ft = T + 9.0f;
    if (ic >= 0) {
        float h = fabsf(pk->obj[ic].size[1]);
        feng_anim_size(a, ic, fw, h);
        feng_anim_pos(a, ic, fl + 0.5f * fw, ft + 0.5f * h);
    }
    float R = fl + fw, pw = fabsf(diff) * len;
    if (ip >= 0) {
        float h = fabsf(pk->obj[ip].size[1]);
        feng_anim_size(a, ip, pw, h);
        feng_anim_pos(a, ip, R + 0.5f * pw, ft + 0.5f * h);
    }
    if (ih >= 0) {
        const FengObj *o = &pk->obj[ih];
        float hx = (diff >= 0.0f ? R + pw : R) - 9.0f;
        feng_anim_pos(a, ih, hx + 0.5f * fabsf(o->size[0]), T + 7.0f + 0.5f * fabsf(o->size[1]));
    }
}

static int perf_obj(const Shop *s, const char *fmt, int n) {
    char nm[32]; snprintf(nm, sizeof nm, fmt, n);
    return feng_find(s->pk[PK_PERF_PKG], feng_hash_upper(nm));
}

/* 0x5620d0 / 0x54ff90: the highlighted entry -- the contents, the preview
   parts and the sliders */
static void perf_pkg_update(Shop *s) {
    const FengPackage *pk = s->pk[PK_PERF_PKG];
    FengAnim *a = s->perf_anim;
    if (!pk || !a) return;
    int type = k_perf_pkg[s->perfpkg.sel].group, cat = s->perf_cat, o;
    unsigned char prev[N2_PERF_PARTS];
    memcpy(prev, s->fitted.perf, sizeof prev);              /* 0x530140: the installed parts */
    PerfParts pp; perf_clear(&pp); memcpy(pp.fit, prev, sizeof pp.fit);
    int show = type >= 1 && type <= 3;
    if (show) perf_fit_pkg(&pp, cat, type);                  /* 0x521340 on the copy */
    else if (type == PP_STOCK) perf_unfit_cat(&pp, cat);    /* 0x521a20 on the copy */
    memcpy(prev, pp.fit, sizeof prev);
    if (show != s->perf_shown) {                              /* PackageFadeIn / PackageFadeOut */
        uint32_t sc = feng_hash_upper(show ? "PackageFadeIn" : "PackageFadeOut");
        if ((o = feng_find(pk, O_PERF_EVENTS)) >= 0) feng_anim_set(a, o, sc, 1);
        if ((o = feng_find(pk, O_PERF_CONTENTS)) >= 0) feng_anim_set(a, o, sc, 1);
        s->perf_shown = show;
    }
    /* 0x547b60: the rows */
    s->perf_nrow = 0;
    for (int n = 1; n <= 4; n++)
        if ((o = perf_obj(s, "PURCHASED_ICON_%d", n)) >= 0) feng_anim_set(a, o, feng_hash_upper("Hide"), 1);
    if (show) {
        int idx[4]; unsigned int name[4]; int price[4];
        int np = perf_pkg_parts(cat, type, idx, name, price);
        for (int n = 0; n < 4; n++) {
            int on = perf_obj(s, "PartName_%d", n + 1), op = perf_obj(s, "PartPrice_%d", n + 1);
            if (n >= np) {
                if (on >= 0) feng_anim_hide(a, on, 1);
                if (op >= 0) feng_anim_hide(a, op, 1);
                continue;
            }
            int inst = idx[n] >= 0 && idx[n] < N2_PERF_PARTS && s->fitted.perf[idx[n]];
            s->perf_row_name[n] = name[n];
            s->perf_row_price[n] = inst ? 0xed56be1fu : 0x4287206fu;   /* Installed / Not Installed */
            if (on >= 0) feng_anim_hide(a, on, 0);
            if (op >= 0) {
                feng_anim_hide(a, op, 0);
                feng_anim_set(a, op, feng_hash_upper(inst ? "Purchased" : "Init"), 1);
            }
            s->perf_nrow = n + 1;
        }
    }
    /* outside the career: no cost, discount or total */
    static const uint32_t cost[4] = { 0x06e0c9a8u, 0xbe8693e1u, 0x0380b632u, 0x89ca4fdbu };
    for (int i = 0; i < 4; i++) if ((o = feng_find(pk, cost[i])) >= 0) feng_anim_hide(a, o, 1);
    /* 0x54ff90: the sliders, the installed values under the preview */
    float cur[3], val[3];
    perf_stats_of(s->fitted.perf, cur);
    perf_stats_of(prev, val);
    for (int k = 0; k < 3; k++)
        perf_slider_place(s, PK_PERF_PKG, a, k_perf_slider[k], cur[k], type == 0 ? cur[k] : val[k]);
}

/* UI_BuyPerformanceParts (ctor 0x569cc0, list 0x562790, handler 0x569ee0,
   stats 0x562530): shown under the package screen while Choose Individual
   Parts is highlighted, focused by selecting it (0xc407210). The rows are
   every part of the category's packages, levels 1..3 (0x53b870: the name,
   "Installed" with the Purchased script or "Not Installed"), 4 a page
   (+0x134), the UI_Options row pools (OPTION_NAME_n / OPTION_DATA_n /
   OPTION_BACKING_n on TITLE_MASTER / DATA_MASTER). The stats box: the
   installed parts against them with the highlighted part added -- peak
   torque (0x5b5810) and power (0x5b56e0) sliders and the gains,
   "%2.2f %s" in Nm / kW (metric) or ft-lbs (x 0.7375621) / bhp
   (x 1.34102209). Selecting a part fits it (0x9f00f07: 0x5213d0). */
#define BUY_PAGE 4
#define O_BUY_TORQUE 0x18531358u
#define O_BUY_POWER  0x6bb32446u
static const uint32_t k_buy_slider[2][5] = {   /* 0x547e30: backing, -, current bar, left cap, preview bar */
    { 0x0aed16edu, 0, 0x44c7c775u, 0xdeb96fdcu, 0xe433adb4u },   /* torque_ */
    { 0x2b95a91bu, 0, 0xcea9c0a3u, 0x147447cau, 0x6e15a6e2u },   /* horsepower_ */
};

static void buy_build(Shop *s) {                                          /* 0x562790 */
    s->buy_n = 0; s->buy_sel = 0; s->buy_top = 0;
    for (int lvl = 1; lvl <= 3; lvl++) {
        int idx[4]; unsigned int name[4];
        int n = perf_pkg_parts(s->perf_cat, lvl, idx, name, NULL);
        for (int j = 0; j < n && s->buy_n < BUY_MAX; j++) {
            s->buy_idx[s->buy_n] = idx[j]; s->buy_name[s->buy_n] = name[j]; s->buy_n++;
        }
    }
}

static int buy_installed(const Shop *s, int k) {
    int i = s->buy_idx[k];
    return i >= 0 && i < N2_PERF_PARTS && s->fitted.perf[i];
}

/* which objects are list rows (draw_anim_package: rows are not pinned) */
static void buy_marks(Shop *s) {
    const FengPackage *pk = s->pk[PK_PERF_BUY];
    memset(s->qr_rowobj, 0, sizeof s->qr_rowobj);
    if (!pk) return;
    for (int n = 1; n <= BUY_MAX; n++)
        for (int part = 0; part < QP_N; part++) {
            int o = qr_pool(pk, part, n);
            if (o < 0) continue;
            s->qr_rowobj[o] = part == QP_LEFT || part == QP_RIGHT ? 2 : 1;
            for (int c = o + 1; c < pk->nobj; c++)
                if (feng_inside(pk, c, o)) s->qr_rowobj[c] = 1;
        }
}

/* the rows on the page, their scripts, the scroll tab, and 0x562530 */
static void buy_update(Shop *s) {
    const FengPackage *pk = s->pk[PK_PERF_BUY];
    FengAnim *a = s->buy_anim;
    if (!pk || !a) return;
    RowGeom g; row_geom(pk, &g);
    uint32_t hi = feng_hash_upper("Highlight"), un = feng_hash_upper("UnHighlight"),
             pur = feng_hash_upper("Purchased");
    for (int n = 1; n <= BUY_MAX; n++) {
        int k = n - 1, shown = k < s->buy_n && k >= s->buy_top && k < s->buy_top + BUY_PAGE;
        float y = (float)(k - s->buy_top) * g.step;
        int sel = s->buy_focus && k == s->buy_sel;
        for (int part = 0; part < QP_N; part++) {
            int o = qr_pool(pk, part, n);
            if (o < 0) continue;
            feng_anim_hide(a, o, !shown);
            if (!shown) continue;
            switch (part) {
            case QP_NAME: feng_anim_pos(a, o, g.tl, g.tt + y); break;
            case QP_DATA: feng_anim_pos(a, o, g.dcx, g.dt + y); break;
            case QP_BACK: feng_anim_pos(a, o, g.tl + 276.0f, g.tt + y + 10.0f); break;
            default: break;
            }
            uint32_t sc = part == QP_DATA && buy_installed(s, k) ? pur : sel ? hi : un;
            feng_anim_set(a, o, sc, 1);
        }
    }
    int tab = feng_find(pk, O_SCROLL_TAB), bar = feng_find(pk, O_SCROLL_BAR);
    if (tab >= 0) {
        int pages = s->buy_n > BUY_PAGE;
        feng_anim_hide(a, tab, !pages);
        if (pages && bar >= 0) {
            float blen = fabsf(pk->obj[bar].size[1]), btop = pk->obj[bar].ay - 0.5f * blen;
            float th = fabsf(pk->obj[tab].size[1]);
            float per = (blen - th) / (float)(s->buy_n - BUY_PAGE);
            feng_anim_pos(a, tab, pk->obj[tab].ax, btop + 0.5f * th + per * (float)s->buy_top);
        }
    }
    /* 0x562530: the installed parts (B) against them plus the highlighted part (A) */
    float rb[2] = { 0, 0 }, nb[2] = { 0, 0 }, ra[2] = { 0, 0 }, na[2] = { 0, 0 };
    if (g_shop_rec_ok && perf_ready()) {
        static unsigned char built[N2_REC_SIZE];
        PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit);
        perf_build(g_shop_rec, &pp, built);
        perf_power(built, rb, nb);
        if (s->buy_n) perf_fit_part(&pp, s->buy_idx[s->buy_sel]);
        perf_build(g_shop_rec, &pp, built);
        perf_power(built, ra, na);
    }
    for (int k = 0; k < 2; k++)
        perf_slider_place(s, PK_PERF_BUY, a, k_buy_slider[k], nb[k], na[k]);
    int metric = g_opt.kmh == 1;
    float tq = (ra[0] - rb[0]) * 1000.0f, pw = ra[1] - rb[1];
    if (!metric) { tq *= 0.7375621f; pw *= 1.34102209f; }
    snprintf(s->buy_txt[0], sizeof s->buy_txt[0], "%.2f %s", tq, str_get(s, metric ? 0x5b81d613u : 0xacb8dff3u));
    snprintf(s->buy_txt[1], sizeof s->buy_txt[1], "%.2f %s", pw, str_get(s, metric ? 0x5b81d5bau : 0xcbbc6512u));
}

static void buy_move(Shop *s, int step) {                                /* 0x72619778 / 0x911c0a4b */
    if (s->buy_n <= 0) return;
    int k = s->buy_sel + step;
    if (k < 0 || k >= s->buy_n) return;
    s->buy_sel = k;
    if (k < s->buy_top) s->buy_top = k;
    if (k >= s->buy_top + BUY_PAGE) s->buy_top = k - BUY_PAGE + 1;
    buy_update(s);
}


static void perf_pkg_enter(Shop *s, int cat) {
    const FengPackage *pk = s->pk[PK_PERF_PKG];
    s->screen = SC_PERF_PKG;
    s->perf_cat = cat;
    scroller_set(&s->perfpkg, k_perf_pkg, NELEM(k_perf_pkg));
    s->perf_shown = -1;
    if (!pk) return;
    if (!s->perf_anim) s->perf_anim = (FengAnim *)malloc(sizeof *s->perf_anim);
    if (!s->perf_anim) return;
    feng_anim_init(s->perf_anim, pk);
    anim_fadein(s->perf_anim);
    s->buy_focus = 0;
    buy_build(s);
    if (s->pk[PK_PERF_BUY]) {
        if (!s->buy_anim) s->buy_anim = (FengAnim *)malloc(sizeof *s->buy_anim);
        if (s->buy_anim) { feng_anim_init(s->buy_anim, s->pk[PK_PERF_BUY]); anim_fadein(s->buy_anim); buy_update(s); }
    }
    perf_pkg_update(s);
}

static void perf_pkg_input(Shop *s, int key) {
    int type = k_perf_pkg[s->perfpkg.sel].group;
    if (s->buy_focus) {   /* UI_BuyPerformanceParts has the pad */
        if (key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) buy_move(s, key == SHOP_KEY_UP ? -1 : 1);
        else if (key == SHOP_KEY_OK && s->buy_n) {
            /* 0x55a230: installed -> "already installed!", else "install
               this part now?" (worse than the fitted one: 0x521c10) */
            PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit);
            int i = s->buy_idx[s->buy_sel];
            if (perf_part_fitted(&pp, i)) dialog(s, DLG_AUTO, 1, 0xda15d7e5u, DA_NONE, 0, 0);
            else dialog(s, DLG_AUTO, 2, perf_part_worse(&pp, i) ? 0xf1bd1a8au : 0xf5ac2b2au, DA_PERF_PART, 0, 0);
        } else if (key == SHOP_KEY_BACK) {   /* 0x34dc1bec: the focus back to the packages */
            s->buy_focus = 0;
            buy_update(s);
        }
        return;
    }
    if (key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) {
        scroller_move(&s->perfpkg, key == SHOP_KEY_LEFT ? -1 : 1);
        perf_pkg_update(s);
    } else if (key == SHOP_KEY_OK) {
        if (type == 0) {   /* 0xc407210: UI_BuyPerformanceParts takes the pad */
            if (s->buy_n) { s->buy_focus = 1; buy_update(s); }
            return;
        }
        /* 0x559f80: installed -> "already installed!", else "install this
           performance package?" (a part worse than a fitted one: 0x5303c0);
           Return To Stock 0x55a140: stock -> "already stock!", else
           "Re-Install Stock Parts?" */
        PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit);
        if (type == PP_STOCK) {
            if (!perf_cat_level(&pp, s->perf_cat)) dialog(s, DLG_AUTO, 1, 0x89d4e03fu, DA_NONE, 0, 0);
            else dialog(s, DLG_AUTO, 2, 0x5d336f96u, DA_PERF_STOCK, 0, 0);
        } else if (perf_pkg_fitted(&pp, s->perf_cat, type)) dialog(s, DLG_AUTO, 1, 0xb84a37fau, DA_NONE, 0, 0);
        else dialog(s, DLG_AUTO, 2, perf_pkg_worse(&pp, s->perf_cat, type) ? 0x0217453fu : 0xf2d7ffabu, DA_PERF_PKG, 0, 0);
    } else if (key == SHOP_KEY_BACK) {
        s->screen = SC_PERFORMANCE;
    }
}
/* the dialog's yes (0x51123592 -> 0x5501e0 / 0xe75749d4): installed on the car */
static void perf_pkg_yes(Shop *s) {
    int type = k_perf_pkg[s->perfpkg.sel].group;
    {
        PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit);
        if (type == PP_STOCK) perf_unfit_cat(&pp, s->perf_cat);
        else perf_fit_pkg(&pp, s->perf_cat, type);
        memcpy(s->fitted.perf, pp.fit, sizeof s->fitted.perf);
        memcpy(s->preview.perf, pp.fit, sizeof s->preview.perf);
        request_preview(s); s->debounce = 0.0f;
        printf("shop: performance category %d %s\n", s->perf_cat,
               type == PP_STOCK ? "returned to stock" : type == 1 ? "street package" : type == 2 ? "pro package" : "extreme package");
        perf_pkg_update(s);
    }
}
static void perf_part_yes(Shop *s) {
    PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit);
    perf_fit_part(&pp, s->buy_idx[s->buy_sel]);
    memcpy(s->fitted.perf, pp.fit, sizeof s->fitted.perf);
    memcpy(s->preview.perf, pp.fit, sizeof s->preview.perf);
    request_preview(s); s->debounce = 0.0f;
    printf("shop: performance part %d fitted\n", s->buy_idx[s->buy_sel]);
    buy_update(s);
    perf_pkg_update(s);
}

/* ---------------------------------------------------------------- credits */

/* Credits.fng (factory 0x4b8720, ctor 0x49c9f0, handler 0x4b87a0). The
   Event Handler's script 0x13c37b sends at 320 ms 0x35f8620b -- load
   CREDITS\NA_ENGLISH.TXT (UTF-16; CR / LF end a line, empty ones are
   skipped, 0x5113f0 wraps lines at the last space before 45 characters) --
   and 0xe6e946b8: the next line (from the top again after the last) rolls
   in a copy of CreditsArea (0xe6093486) running RollCredit. RollCredit
   sends 0xe6e946b8 itself at 300 ms and deletes the copy (0x29161540) at
   its end (5200 ms). Times from the package's EV tags. */
#define O_CREDITS_AREA 0xe6093486u
#define CRED_START_MS 320
#define CRED_NEXT_MS 300
#define CRED_LEN_MS 5200

static void credits_free(Shop *s) {
    for (int i = 0; i < s->cred_n; i++) free(s->cred_line[i]);
    free(s->cred_line);
    s->cred_line = NULL; s->cred_n = 0; s->cred_nroll = 0; s->cred_loaded = 0;
}

static void credits_add(Shop *s, const uint16_t *w, int n) {
    char **nl = (char **)realloc(s->cred_line, (size_t)(s->cred_n + 1) * sizeof *nl);
    if (!nl) return;
    s->cred_line = nl;
    char *t = (char *)malloc((size_t)n + 1);
    if (!t) return;
    for (int i = 0; i < n; i++) t[i] = w[i] < 256 ? (char)w[i] : '?';   /* the fonts are Latin-1 */
    t[n] = 0;
    s->cred_line[s->cred_n++] = t;
}

static void credits_load(Shop *s) {
    credits_free(s);
    s->cred_loaded = 1;
    char path[600];
    snprintf(path, sizeof path, "%s/CREDITS/NA_ENGLISH.TXT", s->dataroot);
    long len = 0;
    unsigned char *b = n2_read_file(path, &len);
    if (!b || len < 2) { free(b); printf("credits: %s unreadable\n", path); return; }
    int n = (int)(len / 2);
    uint16_t *w = (uint16_t *)malloc((size_t)(n + 1) * 2);
    if (!w) { free(b); return; }
    for (int i = 0; i < n; i++) w[i] = (uint16_t)(b[i * 2] | b[i * 2 + 1] << 8);
    free(b);
    w[n] = 0;
    int i0 = w[0] == 0xfeff ? 1 : 0;
    for (int i = i0; i < n; i++) if (w[i] == 10 || w[i] == 13) w[i] = 0;
    /* 0x5113f0: within each line, a break at the last space (after a
       non-space, past the first character) once 45 characters are reached */
    for (int i = i0; i < n; ) {
        if (!w[i]) { i++; continue; }
        int cnt = 0, brk = -1, j = i;
        for (; j < n && w[j]; j++) {
            cnt++;
            if (cnt > 1 && w[j] == ' ' && w[j - 1] != ' ') brk = j;
            if (cnt >= 45 && brk >= 0) {
                int q = brk;
                while (q < n && w[q] == ' ') w[q++] = 0;
                cnt = j - brk;
                brk = -1;
            }
        }
        i = j;
    }
    for (int i = i0; i < n; ) {
        if (!w[i]) { i++; continue; }
        int j = i;
        while (j < n && w[j]) j++;
        credits_add(s, w + i, j - i);
        i = j;
    }
    free(w);
    s->cred_cur = -1;
    printf("credits: %d lines\n", s->cred_n);
}

/* 0xe6e946b8: the next line in a new copy of CreditsArea */
static void credits_roll_next(Shop *s) {
    if (!s->cred_n || s->cred_nroll >= (int)(sizeof s->cred_roll / sizeof s->cred_roll[0])) return;
    s->cred_cur = s->cred_cur + 1 < s->cred_n ? s->cred_cur + 1 : 0;
    s->cred_roll[s->cred_nroll].line = s->cred_cur;
    s->cred_roll[s->cred_nroll].t = 0;
    s->cred_nroll++;
}

static void credits_tick(Shop *s, int ms) {
    if (s->cred_anim) feng_anim_tick(s->cred_anim, ms);
    int old = s->cred_ms;
    s->cred_ms += ms;
    if (old < CRED_START_MS && s->cred_ms >= CRED_START_MS) {
        if (!s->cred_loaded) credits_load(s);
        credits_roll_next(s);
    }
    int spawn = 0, k = 0;
    for (int i = 0; i < s->cred_nroll; i++) {
        int t0 = s->cred_roll[i].t, t1 = t0 + ms;
        if (t0 < CRED_NEXT_MS && t1 >= CRED_NEXT_MS) spawn++;
        if (t1 > CRED_LEN_MS) continue;   /* 0x29161540: the copy is deleted */
        s->cred_roll[k] = s->cred_roll[i];
        s->cred_roll[k].t = t1;
        k++;
    }
    s->cred_nroll = k;
    while (spawn-- > 0) credits_roll_next(s);
}

static void credits_enter(Shop *s) {
    const FengPackage *pk = s->pk[PK_CREDITS];
    s->screen = SC_CREDITS;
    credits_free(s);
    s->cred_ms = 0;
    if (!pk) return;
    if (!s->cred_anim) s->cred_anim = (FengAnim *)malloc(sizeof *s->cred_anim);
    if (s->cred_anim) { feng_anim_init(s->cred_anim, pk); anim_fadein(s->cred_anim); }
}

static void opt_input(Shop *s, int key) {
    if (s->screen == SC_OPT_MAIN) {
        if (key == SHOP_KEY_LEFT) scroller_move(&s->opt_main, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->opt_main, 1);
        else if (key == SHOP_KEY_OK && !s->opt_main.locked[s->opt_main.sel]) {
            if (k_opt_main[s->opt_main.sel].group == OC_CONTROLS) ctl_enter(s);
            else if (k_opt_main[s->opt_main.sel].group == OC_EATRAX) juke_enter(s);
            else if (k_opt_main[s->opt_main.sel].group == OC_CREDITS) credits_enter(s);
            else opt_enter(s, k_opt_main[s->opt_main.sel].group);
        }
        else if (key == SHOP_KEY_BACK) s->screen = SC_MAIN;
        return;
    }
    if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && s->opt_nrow > 0) {
        int step = key == SHOP_KEY_UP ? -1 : 1, k = s->opt_rsel;
        for (int t = 0; t < s->opt_nrow; t++) {
            k = (k + step + s->opt_nrow) % s->opt_nrow;
            if (!opt_row_disabled(s->opt_row[k])) break;
        }
        s->opt_rsel = k;
        if (k < s->opt_top) s->opt_top = k;
        if (k >= s->opt_top + OPT_PAGE) s->opt_top = k - OPT_PAGE + 1;
        opt_layout(s);
        opt_scripts(s, 0);
    } else if ((key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) && s->opt_nrow > 0) {
        int r = s->opt_row[s->opt_rsel], step = key == SHOP_KEY_LEFT ? -1 : 1;
        if (opt_row_disabled(r)) return;
        float *v = opt_slider(r);
        if (v) {   /* 0x4ff480: +- 0.05 within 0..1; at an end nothing moves */
            float nv = *v + 0.05f * (float)step;
            if (nv < 0.0f) nv = 0.0f; if (nv > 1.0f) nv = 1.0f;
            if (nv == *v) return;
            *v = nv;
            opt_slider_place(s, s->opt_rsel);
            audio_fe_play(step < 0 ? FE_SND_SLIDER_LEFT : FE_SND_SLIDER_RIGHT);   /* type SLIDER */
        } else {
            opt_row_change(r, step);
            audio_fe_play(step < 0 ? FE_SND_LEFT : FE_SND_RIGHT);
            opt_scripts(s, 0);   /* a row another greys */
        }
        s->snd_done = 1;
        const FengPackage *pk = s->pk[PK_OPTIONS];
        int o = pk && s->opt_anim ? qr_pool(pk, step < 0 ? QP_LEFT : QP_RIGHT, s->opt_rsel + 1) : -1;
        if (o >= 0) feng_anim_set(s->opt_anim, o, feng_hash_upper("Blink"), 1);   /* 0x51e6b0 */
    } else if (key == SHOP_KEY_BACK) {
        options_save();
        s->screen = SC_OPT_MAIN;
    }
}

/* the car select's list under the filter (0x4e22e0 / 0x4eed10, no sort) */
static void qr_build_cars(Shop *s) {
    int fid = k_qr_filters[s->qr_filter].id, n = 0, sel = 0;
    for (int i = 0; i < s->ncars && n < QR_MAX_CARS; i++) {
        if (fid == 2 && !s->cars[i].mine) continue;           /* My Cars */
        if (!strcmp(s->cars[i].folder, s->qr_car_cur)) sel = n;
        s->qr_car_list[n++] = (short)i;
    }
    s->qr_ncar = n; s->qr_car_sel = sel;
}

static void qr_car_moved(Shop *s) {
    if (!s->qr_ncar) return;
    const ShopCar *c = &s->cars[s->qr_car_list[s->qr_car_sel]];
    if (strcmp(c->folder, s->qr_car_cur)) {
        snprintf(s->qr_car_cur, sizeof s->qr_car_cur, "%s", c->folder);
        s->qr_car_pending = 1; s->qr_car_debounce = 0.35f;
    }
}

void shop_set_tracks(Shop *s, const unsigned char *recs, int n) {
    if (!s) return;
    s->trk = recs; s->ntrk = recs ? n : 0;
}

void shop_set_cars(Shop *s, const ShopCar *cars, int n, const char *cur) {
    if (!s) return;
    if (n > QR_MAX_CARS) n = QR_MAX_CARS;
    if (n > 0 && cars) memcpy(s->cars, cars, (size_t)n * sizeof *cars);
    s->ncars = n > 0 ? n : 0;
    if (cur) snprintf(s->qr_car_cur, sizeof s->qr_car_cur, "%s", cur);
    qr_build_cars(s);
}

static void qr_enter_options(Shop *s) {
    qr_build_rows(s);
    s->screen = SC_QR_OPTIONS;
    qr_options_layout(s);
}

static void qr_enter_cars(Shop *s) {
    s->screen = SC_QR_CAR;
    qr_build_cars(s);
    qr_car_moved(s);          /* a car not in the list: the list's first goes on the platform */
}

static void qr_input(Shop *s, int key) {
    switch (s->screen) {
    case SC_QR_MODE:
        if (key == SHOP_KEY_LEFT) scroller_move(&s->qr_mode, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->qr_mode, 1);
        else if (key == SHOP_KEY_OK) {
            /* 0x4b2c00: the mode, one human, the opponents (URL 5, else 3) */
            s->qr_type = k_qr_modes[s->qr_mode.sel].group;
            s->set[s->qr_type].opp = s->qr_type == QR_URL ? 5 : 3;
            if (s->qr_type == QR_FREERUN) { s->set[QR_FREERUN].ev = 4000; qr_enter_cars(s); }
            else { qr_build_tracks(s); s->screen = SC_QR_TRACK; }
        } else if (key == SHOP_KEY_BACK) s->screen = SC_MAIN;
        break;
    case SC_QR_TRACK:   /* the list stops at its ends (the scroll arrows hide there) */
        if ((key == SHOP_KEY_LEFT && s->qr_track.sel > 0) ||
            (key == SHOP_KEY_RIGHT && s->qr_track.sel < s->qr_track.n - 1)) {
            scroller_move(&s->qr_track, key == SHOP_KEY_LEFT ? -1 : 1);
            s->set[s->qr_type].ev = s->qr_track_e[s->qr_track.sel].group;
            qr_map_request(s);
        } else if (key == SHOP_KEY_OK && s->qr_track.n) qr_enter_options(s);
        else if (key == SHOP_KEY_BACK) s->screen = SC_QR_MODE;
        break;
    case SC_QR_OPTIONS:
        if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && s->qr_nrow > 0) {
            int step = key == SHOP_KEY_UP ? -1 : 1, k = s->qr_rsel;
            for (int t = 0; t < s->qr_nrow; t++) {           /* disabled rows are skipped */
                k = (k + step + s->qr_nrow) % s->qr_nrow;
                if (!qr_row_disabled(s, s->qr_row[k])) break;
            }
            s->qr_rsel = k;
            qr_options_scripts(s, 0);
        } else if ((key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) && s->qr_nrow > 0) {
            int row = s->qr_row[s->qr_rsel];
            if (qr_row_disabled(s, row)) break;
            qr_row_change(s, row, key == SHOP_KEY_LEFT ? -1 : 1);
            const FengPackage *pk = s->pk[PK_QR_OPTIONS];
            int o = pk && s->qr_anim ? qr_pool(pk, key == SHOP_KEY_LEFT ? QP_LEFT : QP_RIGHT, s->qr_rsel + 1) : -1;
            if (o >= 0) feng_anim_set(s->qr_anim, o, feng_hash_upper("Blink"), 1);   /* 0x51e6b0 */
            qr_options_scripts(s, 0);                       /* Lap KO enables / disables Laps */
        } else if (key == SHOP_KEY_OK) qr_enter_cars(s);
        else if (key == SHOP_KEY_BACK) s->screen = SC_QR_TRACK;
        break;
    case SC_QR_CAR:
        if ((key == SHOP_KEY_LEFT && s->qr_car_sel > 0) ||
            (key == SHOP_KEY_RIGHT && s->qr_car_sel < s->qr_ncar - 1)) {
            s->qr_car_sel += key == SHOP_KEY_LEFT ? -1 : 1;
            qr_car_moved(s);
        } else if (key == SHOP_KEY_LTRIG || key == SHOP_KEY_RTRIG) {
            int nf = NELEM(k_qr_filters);
            s->qr_filter = (s->qr_filter + (key == SHOP_KEY_LTRIG ? nf - 1 : 1)) % nf;
            qr_build_cars(s);
            qr_car_moved(s);
        } else if (key == SHOP_KEY_OK && s->qr_ncar) {
            if (s->qr_car_pending) s->qr_car_debounce = 0.0f;   /* the car on screen first */
            s->qr_go = 1;
        } else if (key == SHOP_KEY_BACK)                    /* 0x4fa470 */
            s->screen = s->qr_type == QR_FREERUN ? SC_QR_MODE : SC_QR_OPTIONS;
        break;
    }
}

/* per frame: the map's rest timer and fade, the option scripts, the car debounce */
static void qr_update(Shop *s, float dt) {
    if (s->screen == SC_QR_TRACK && s->map_want && s->map_want != s->map_ev) {
        s->map_alpha = 0.0f;
        if ((s->map_wait -= dt) <= 0.0f) {
            char path[600];
            snprintf(path, sizeof path, "%s/TRACKS/TRACKMAP%d.BIN", s->dataroot, s->map_want);
            long len = 0;
            unsigned char *d = n2_read_file(path, &len);
            char nm[24]; snprintf(nm, sizeof nm, "TRACKMAP%d", s->map_want);
            uint32_t key = feng_tex_key(nm);
            FengImage img; memset(&img, 0, sizeof img);
            if (s->map_tex) { glDeleteTextures(1, &s->map_tex); s->map_tex = 0; }
            if (d && feng_tpk_load(d, len, &key, 1, &img) && img.rgba) {
                /* TRACKMAP's DXT3 alpha is all zero and its record's blend
                   code is 5, not the FE's 1: the map is drawn opaque */
                for (long q = 0; q < (long)img.w * img.h; q++) img.rgba[q * 4 + 3] = 255;
                s->map_tex = upload_rgba(img.w, img.h, img.rgba);
            } else printf("shop: %s not loaded\n", path);
            feng_image_free(&img);
            free(d);
            s->map_ev = s->map_want;
        }
    } else if (s->map_tex && s->map_alpha < 1.0f) {
        s->map_alpha += dt * 4.0f;
        if (s->map_alpha > 1.0f) s->map_alpha = 1.0f;
    }
    if (s->screen == SC_QR_OPTIONS && s->qr_anim) {
        s->qr_anim_ms += (int)(dt * 1000.0f + 0.5f);
        feng_anim_tick(s->qr_anim, (int)(dt * 1000.0f + 0.5f));
    }
    if (s->qr_car_pending && s->qr_car_debounce > 0.0f) s->qr_car_debounce -= dt;
}

/* the race to start, from the picked mode's settings record (0x53fc20) */
static void qr_fill(const Shop *s, ShopQuickRace *q) {
    const QrSet *r = &s->set[s->qr_type];
    memset(q, 0, sizeof *q);
    q->type = s->qr_type; q->event = r->ev;
    const unsigned char *rec = qr_rec(s, r->ev);
    if (rec) {
        int k = 0;
        while (k < 7 && rec[0x40 + k]) { q->stem[k] = (char)rec[0x40 + k]; k++; }
        q->stem[k] = 0;
    }
    q->lap_ko = s->qr_type == QR_CIRCUIT && r->ko;
    q->laps = q->lap_ko ? 3 : r->laps;
    q->opponents = q->lap_ko ? 3 : r->opp;
    q->difficulty = r->diff; q->traffic = r->traffic; q->catchup = r->catchup;
    q->reverse = rec && rec[0x86] == 1 ? r->dir : 0;
}


static void shop_input_screen(Shop *s, int key);
/* what a key can change on the screens (a changed value = the screen took the key) */
static uint32_t shop_sig(const Shop *s) {
    uint32_t h = 2166136261u;
#define SIG(v) do { const unsigned char *b_ = (const unsigned char *)&(v); \
                    for (size_t i_ = 0; i_ < sizeof(v); i_++) h = (h ^ b_[i_]) * 16777619u; } while (0)
    SIG(s->screen); SIG(s->top.sel); SIG(s->body.sel); SIG(s->cat.sel); SIG(s->rims.sel);
    SIG(s->main.sel); SIG(s->part_sel); SIG(s->fe_action); SIG(s->exit_req);
    SIG(s->rim_style); SIG(s->prev_style); SIG(s->qr_type); SIG(s->qr_mode.sel);
    SIG(s->qr_track.sel); SIG(s->qr_rsel); SIG(s->qr_car_sel); SIG(s->qr_filter); SIG(s->qr_go);
    SIG(s->set); SIG(s->pending); SIG(s->fitted);
    SIG(s->opt_main.sel); SIG(s->opt_rsel); SIG(g_opt);
    SIG(s->ctl_sel); SIG(s->ctl_col); SIG(s->ctl_cap);
    SIG(s->juke_sel); SIG(s->juke_top); SIG(s->juke_grab);
    SIG(s->paintcat.sel); SIG(s->paint_mode); SIG(s->paint_sel); SIG(s->paint_brand);
    SIG(s->vlay.sel); SIG(s->vcat.sel); SIG(s->vmode);
    SIG(s->dloc.sel); SIG(s->dlev);
#undef SIG
    return h;
}
/* The front-end sound of a handled key: SoundMgr 0x458520 with the generic
   control's ids from the 0x837fe0 table (UP 0, DOWN 1, LEFT 2, RIGHT 3,
   SELECT 4, BACK 5; L1/R1 = LTRIGGER / RTRIGGER 2 / 3). */
void shop_input(Shop *s, int key) {
    if (!s) return;
    uint32_t before = shop_sig(s);
    s->snd_done = 0;
    shop_input_screen(s, key);
    if (s->snd_done || shop_sig(s) == before) return;
    static const signed char snd[] = { -1, FE_SND_UP, FE_SND_DOWN, FE_SND_LEFT, FE_SND_RIGHT,
                                       FE_SND_SELECT, FE_SND_BACK, FE_SND_LEFT, FE_SND_RIGHT,
                                       FE_SND_BACK };
    if (key > 0 && key < (int)sizeof snd) audio_fe_play(snd[key]);
}
/* ---------------------------------------------------------- visual rating */

/* The Visual Rating box (UI_Menu_Asset_Reputation, widget 0x5104d0 / setup
   0x53d8f0 / update 0x54b000): the installed setup's rating (config B) and,
   when a preview is up, the preview's (config A, 0x55b1a0), shown on every
   Customize screen but the browsers, the paint and decal screens, the rims
   and spinners (0x55bc90). 0x5234c0: every car slot 0..0xa9 but the vinyl
   colours (0x4e..0x5c) and 0x1e -- and the bumpers / skirt under a wide
   body -- adds its part's points (0x5142e0: the GCareer 0x34A1C row of the
   slot (0x501d70) at the part's level, 0x501860; the car's own base paint
   0); points / 500 is the rating, 10 from 5000 (table 0x7fa238). */
#define O_VR_TEXT   0x171d00cbu   /* "0.0" */
#define O_VR_STAR   0x6ec2f0deu
#define O_VR_BASE   0xb5f1ffe1u   /* vr_base */
#define O_VR_SLIDER 0x22dac589u   /* vr_slider: the tab */
#define O_VR_HANDLE 0x086db7d2u   /* vr_handle: the fill */
#define O_VR_BEG    0x81a27c94u   /* vr_Beg / vr_End: hidden by the vertical slider */
#define O_VR_END    0x81a28a7du

/* 0x837d30 (filled by 0x501920): a car slot's GCareer row */
static int rate_row_of_slot(int slot) {
    static const unsigned char t[0xaa] = {
        [0x1] = 18, [0x2] = 17, [0x3] = 19, [0x4] = 19, [0x6] = 22, [0x7] = 20, [0x9] = 11, [0xb] = 16,
        [0xc] = 12, [0xe] = 15, [0xf] = 14, [0x10] = 21, [0x1d] = 13, [0x1e] = 13, [0x1f] = 33, [0x20] = 19,
        [0x22] = 30, [0x23] = 31, [0x24] = 31, [0x25] = 31, [0x26] = 31, [0x27] = 31, [0x28] = 31,
        [0x29] = 31, [0x2a] = 31, [0x2b] = 31, [0x2c] = 31, [0x2d] = 31, [0x2e] = 31,
        [0x34] = 76, [0x35] = 76, [0x36] = 76, [0x37] = 76, [0x38] = 76, [0x39] = 76, [0x3a] = 76,
        [0x44] = 40, [0x45] = 80, [0x46] = 79, [0x47] = 78, [0x48] = 82, [0x49] = 77, [0x4a] = 81,
        [0x4b] = 83, [0x4c] = 84, [0x5d] = 85, [0x60] = 86,
        [0x99] = 32, [0x9a] = 25, [0x9b] = 26, [0x9c] = 27, [0x9d] = 28, [0x9e] = 27, [0x9f] = 24,
        [0xa0] = 35, [0xa1] = 36, [0xa2] = 29, [0xa3] = 23, [0xa4] = 23, [0xa5] = 23, [0xa6] = 23,
    };
    if (slot >= 0x61 && slot <= 0x98) return 76;   /* the decals */
    return slot >= 0 && slot < 0xaa ? t[slot] : 0;
}
/* 0x501d70: the row of a slot's part (rec: its database record) */
static int rate_row(const Shop *s, int slot, const unsigned char *rec) {
    unsigned aoff = rec[10] | rec[11] << 8;
    uint32_t br = 0;
    if (slot == 0x3f) {   /* base paint by brand */
        db_attr(s, aoff, 0xebb03e66u, &br);
        if (br == BR_METAL) return 0x26;
        if (br == BR_GLOSS) return 0x25;
        if (br == BR_PEARL) return 0x27;
        if (br == feng_hash("MUFFLER")) return 0x4e;
        if (br == feng_hash("RIM")) return 0x4d;
        return br == feng_hash("HOSE") ? 0x28 : 0x26;
    }
    if (slot >= 0x40 && slot <= 0x43) {   /* a vinyl, by its group */
        static const unsigned char vg[0x1c] = { 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32,
            0x33, 0x34, 0x35, 0x36, 0x38, 0x39, 0x3a, 0x3b, 0x41, 0x42, 0x43, 0x3c, 0x3d, 0x40, 0x3e, 0x37,
            0x3f, 0x44 };
        int g = rec[5] & 0x1f;
        return g < 0x1c ? vg[g] : 0;
    }
    if (db_has_attr(s, aoff, 0x721aff7cu)) {   /* CARBONFIBRE */
        switch (slot) { case 7: return 0x46; case 9: return 0x4b; case 0xc: return 0x4a; case 0x20: return 0x45; }
        return 0;
    }
    if (slot == 0x1d && db_attr(s, aoff, 0xebb03e66u, &br) && br == 0xc8d1a75eu) return 0x21;   /* SPINNER */
    if (slot == 9 && (rec[5] & 0x1f) == 5) return 0x22;   /* a split hood */
    return rate_row_of_slot(slot);
}
/* 0x501860: the row's points at a level (4: fixed ones) */
static int rate_points(const Shop *s, int row, int lvl) {
    if (!lvl || row <= 0 || row >= 0x57) return 0;
    if (lvl == 4) {
        switch (row) {
        case 0xb: case 0xc: case 0xd: case 0x22: case 0x4a: case 0x4b: return 400;
        case 0x16: return 1000;
        case 0x3e: return 100;
        }
        return 0;
    }
    if (lvl > 3 || row >= 96) return 0;
    return s->rep[row][lvl - 1];
}
static int rate_part(const Shop *s, int slot, const unsigned char *rec) {
    if (!rec) return 0;
    int p = rate_points(s, rate_row(s, slot, rec), rec[5] >> 5);
    return p > 0 ? p : 0;
}
/* the database record of a part in a model and group by our config value
   (the Body Shop's parts, 0x545700 lists) */
static const unsigned char *rate_body_rec(const Shop *s, int group, int value, int carbon) {
    static const char *const libs[] = { NULL, "SPOILER", "SPOILER_HATCH", "SPOILER_SUV", "EXHAUST", "ROOF",
                                        "MIRRORS_BODY", "MIRRORS_POST", "MIRRORS_SUV", "MIRRORS_HUMMER" };
    int models[6], nm = 0;
    models[nm++] = db_model(s, s->car);
    for (int l = 1; l <= 5; l++)
        if ((group == G_SPOILER && l <= 3) || (group == G_EXHAUST && l == 4) || (group == G_ROOF && l == 5))
            models[nm++] = db_model(s, libs[l]);
    if (group == G_LEFT_MIRROR) models[nm++] = db_model(s, libs[n2_mirror_lib(s->car)]);
    for (int mi = 0; mi < nm; mi++) {
        if (models[mi] < 0) continue;
        for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
            const unsigned char *r = s->db + p;
            uint32_t hash = n2_u32(r);
            unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
            if (!hash && !soff) break;
            if (r[7] != models[mi] || r[4] != group) continue;
            if (db_has_attr(s, aoff, 0xF7934316u)) continue;   /* EXCLUDE_UG2 */
            if (db_has_attr(s, aoff, 0x721AFF7Cu) != (carbon != 0)) continue;
            int v = part_value(s->car, group, hash);
            if (v < 0 && mi > 0 && (r[5] | r[6] << 8) == 0) v = 0;
            if (v == value) return r;
        }
    }
    return NULL;
}
/* the first record of `group` whose attribute `key` is `val` */
static const unsigned char *rate_attr_rec(const Shop *s, int group, uint32_t key, uint32_t val) {
    for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
        const unsigned char *r = s->db + p;
        unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
        uint32_t v = 0;
        if (!n2_u32(r) && !soff) break;
        if (r[4] == group && !db_has_attr(s, aoff, 0xF7934316u) && db_attr(s, aoff, key, &v) && v == val) return r;
    }
    return NULL;
}
/* RED GREEN BLUE (ALPHA, RED2 .. ALPHA2) of a record packed as the shop keeps them */
static uint32_t rate_col(const Shop *s, unsigned aoff, int second, int alpha) {
    static const uint32_t ak[8] = { 0x0000d99au, 0x02ddc8f0u, 0x00136707u, 0x026e1ac5u,
                                    0x001c0d0cu, 0x5e96e722u, 0x02804819u, 0x50317397u };
    uint32_t col = 0;
    for (int i = 0; i < (alpha ? 4 : 3); i++) { uint32_t c = 0; db_attr(s, aoff, ak[second * 4 + i], &c); col |= (c & 0xff) << (8 * i); }
    return col;
}
static uint32_t hud_rgb(uint32_t c) { return 0x01000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff); }
static int rate_rims(const Shop *s, const char *folder, int style) {
    if (!folder[0] || (!strcmp(folder, STOCK_RIM_FOLDER) && style == STOCK_RIM_STYLE)) return 0;
    int model = db_model(s, "WHEELS");
    if (model < 0) return 0;
    uint32_t key = feng_hash(folder);
    int rim_outer = g_shop_rec_ok ? (signed char)g_shop_rec[0xdc] : 0;
    for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
        const unsigned char *r = s->db + p;
        unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
        uint32_t br = 0, hash = n2_u32(r);
        if (!hash && !soff) break;
        if (r[7] != model || r[4] != G_WHEEL || !db_attr(s, aoff, 0xEBB03E66u, &br) || br != key) continue;
        int st, in, out;   /* the fitted part: <folder>_STYLEnn_<inner>_<outer> */
        if (!rim_part_parse(folder, hash, &st, &in, &out) || st != RIM_STYLE(style)) continue;
        if ((RIM_INNER(style) && in != RIM_INNER(style)) || (rim_outer && out != rim_outer)) continue;
        return rate_part(s, 0x1d, r);
    }
    return 0;
}
/* 0x5234c0 over our configuration (rims as the shop holds them) */
static float shop_rating(const Shop *s, const N2CarConfig *c, const char *rim_folder, int rim_style) {
    if (!s->db || !s->db_parts) return 0.0f;
    int pts = 0;
    static const struct { int slot, group, part; } body[] = {
        { 0x1, G_FRONT_BUMPER, N2_PART_FRONT }, { 0x2, G_REAR_BUMPER, N2_PART_REAR }, { 0xb, G_SKIRT, N2_PART_SKIRT },
        { 0x9, G_HOOD, N2_PART_HOOD }, { 0xe, G_HEADLIGHT, N2_PART_HEADLIGHT }, { 0xf, G_BRAKELIGHT, N2_PART_TAILLIGHT },
        { 0xc, G_SPOILER, N2_PART_SPOILER }, { 0x10, G_EXHAUST, N2_PART_EXHAUST }, { 0x7, G_ROOF, N2_PART_SCOOP },
        { 0x20, G_LEFT_MIRROR, N2_PART_MIRROR }, { 0x6, G_WIDE_BODY, N2_PART_WIDE },
    };
    int wide = 0;
    {   const unsigned char *r = c->parts[N2_PART_WIDE] ? rate_body_rec(s, G_WIDE_BODY, c->parts[N2_PART_WIDE], 0) : NULL;
        wide = r && (r[5] >> 5) != 0; }
    for (int i = 0; i < NELEM(body); i++) {
        int v = c->parts[body[i].part];
        if (!v || (wide && (body[i].slot == 1 || body[i].slot == 2 || body[i].slot == 0xb))) continue;
        int cf = body[i].slot == 9 ? (c->carbon & 1) : (v % 1000) >= 500;
        pts += rate_part(s, body[i].slot, rate_body_rec(s, body[i].group, v, cf));
    }
    pts += rate_rims(s, rim_folder, rim_style);
    /* KIT_CARBON 0x5d / TRUNK_CARBON 0x60 (table 0x802960: group 51) hold
       "CARBON FIBRE" (level 3) when converted, else "CARBON FIBRE NONE" */
    if (c->carbon & 2) pts += rate_part(s, 0x5d, db_part(s, 0x5ca4321cu));
    if (c->carbon & 4) pts += rate_part(s, 0x60, db_part(s, 0x5ca4321cu));
    for (int i = 0; i < N2_PAINT_COUNT; i++)   /* 0x3f: the car's own paint (0) adds nothing */
        if (c->paint[i]) pts += rate_part(s, k_paint_cat[i].group, db_part(s, c->paint[i]));
    for (int l = 0; l < VINYL_LAYERS; l++)
        if (c->vinyl[l]) pts += rate_part(s, 0x40 + l, db_part(s, c->vinyl[l]));
    for (int loc = 0; loc < 7; loc++)
        for (int n = 0; n < 8; n++) {
            const DecalPart *d = c->decal[loc][n] ? decal_by_name(c->decal[loc][n]) : NULL;
            if (d) pts += rate_part(s, 0x61 + loc * 8 + n, db_part(s, d->hash));
        }
    if (c->tint) pts += rate_part(s, 0x99, rate_attr_rec(s, G_WINDOW_TINT, 0x6ba02c05u, c->tint));
    if (c->hud) pts += rate_part(s, 0xa3, rate_attr_rec(s, G_CUSTOM_HUD, 0x311151f8u, c->hud));
    if (c->purge) pts += rate_part(s, 0xa2, db_part(s, c->purge));
    if (c->parts[N2_PART_AUDIO] > 1) {   /* the trunk layout: its level */
        int model = db_model(s, s->car);
        for (long p = s->db_parts; model >= 0 && p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
            const unsigned char *r = s->db + p;
            if (!n2_u32(r) && !(r[8] | r[9] << 8)) break;
            if (r[4] == G_TRUNK_AUDIO && r[7] == model && (r[5] >> 5) == c->parts[N2_PART_AUDIO] - 1) { pts += rate_part(s, 0x22, r); break; }
        }
    }
    for (int i = 0; i < 12; i++)   /* the components (level 0 "NO COMPONENTS" counts 0) */
        if (c->audio_comp[i] && ice_slot_ok(s->car, ice_layout_level(c), 0x23 + i))
            pts += rate_part(s, 0x23 + i, db_part(s, c->audio_comp[i]));
    unsigned done = 0;
    for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len; p += 14) {   /* the colour parts */
        const unsigned char *r = s->db + p;
        unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
        if (!n2_u32(r) && !soff) break;
        if (db_has_attr(s, aoff, 0xF7934316u)) continue;
        if (r[4] == G_HEADLIGHT_BULB && c->bulb && !(done & 0x100u) && rate_col(s, aoff, 0, 0) == c->bulb) {
            pts += rate_part(s, 0x9f, r); done |= 0x100u;
        }
        if (r[4] == G_NEON)
            for (int k = 0; k < 4; k++)
                if ((c->neon[k][0] || c->neon[k][1]) && !(done & (1u << k)) && rate_col(s, aoff, 0, 1) == c->neon[k][0] &&
                    rate_col(s, aoff, 1, 1) == c->neon[k][1]) { pts += rate_part(s, 0x9a + k, r); done |= 1u << k; }
        if (r[4] == G_CUSTOM_HUD_PAINT)
            for (int k = 0; k < 3; k++)
                if (c->hud_col[k] && !(done & (16u << k)) && hud_rgb(rate_col(s, aoff, 0, 0)) == c->hud_col[k]) {
                    pts += rate_part(s, k == 0 ? 0xa4 : k == 1 ? 0xa6 : 0xa5, r); done |= 16u << k;
                }
        if (r[4] == G_HYDRAULICS && c->hydraulics && (int)(r[5] >> 5) == (int)c->hydraulics) pts += rate_part(s, 0xa1, r);
        if (r[4] == G_DOOR_STYLE && c->doors) {
            uint32_t st = 0;
            if (db_attr(s, aoff, 0x38A30E75u, &st) && st == c->doors)   /* ANIMSTYLE */ pts += rate_part(s, 0xa0, r);
        }
    }
    if (pts >= 5000) return 10.0f;
    return (float)pts / 500.0f;
}

/* ------------------------------------------------- graphics setup (0x562c50) */

/* The Graphics Shop's pending changes by kind (0x561a40): 0 vinyls (slots
   0x40..0x43, 0x547540; their colours with them), 1 decals (0x34..0x3e and
   0x61..0x98, 0x530240), 2 paint (0x3f and 0x44..0x4a: base paint, spoiler,
   rims, spinners, brakes, engine, muffler -- roof scoop and mirrors stay). */
enum { GFX_VINYL = 1, GFX_DECAL = 2, GFX_PAINT = 4, GFX_ALL = 7 | 8 };
static int gfx_paint_cancels(int i) { int sl = k_paint_cat[i].group; return sl == 0x3f || (sl >= 0x44 && sl <= 0x4a); }
static int gfx_changed(const Shop *s) {
    const N2CarConfig *a = &s->fitted, *b = &s->gfx_base;
    return memcmp(a->paint, b->paint, sizeof a->paint) || memcmp(a->vinyl, b->vinyl, sizeof a->vinyl) ||
           memcmp(a->vinyl_col, b->vinyl_col, sizeof a->vinyl_col) ||
           memcmp(a->decal_layout, b->decal_layout, sizeof a->decal_layout) || memcmp(a->decal, b->decal, sizeof a->decal);
}
/* back to the installed setup: `what` GFX_* (8 = roof scoop / mirror paint too) */
static void gfx_revert(Shop *s, int what) {
    N2CarConfig *a = &s->fitted; const N2CarConfig *b = &s->gfx_base;
    if (what & GFX_VINYL) { memcpy(a->vinyl, b->vinyl, sizeof a->vinyl); memcpy(a->vinyl_col, b->vinyl_col, sizeof a->vinyl_col); }
    if (what & GFX_DECAL) { memcpy(a->decal_layout, b->decal_layout, sizeof a->decal_layout); memcpy(a->decal, b->decal, sizeof a->decal); }
    for (int i = 0; i < N2_PAINT_COUNT; i++)
        if (((what & GFX_PAINT) && gfx_paint_cancels(i)) || (what & 8)) a->paint[i] = b->paint[i];
    s->preview = *a; request_preview(s); s->debounce = 0.0f;
}
/* 0x547570: the pending list into the installed setup */
static void gfx_install(Shop *s) {
    s->gfx_base = s->fitted;
    printf("shop: graphics setup installed\n");
}
/* 0x562c50 0x911ab364 (not career): changes ask "install your new graphics
   setup?" (0x511cca10): yes 0x51a33f36 installs, no 0x562f5dac puts config B
   back; either way to the Car Shop */
static void gfx_back(Shop *s) {
    if (gfx_changed(s)) {
        dialog(s, PK_DLG_S, 2, 0x511cca10u, DA_GFX_LEAVE, 0, 0);
        s->dlg_act2 = DA_GFX_DISCARD;
    } else s->screen = SC_CAR_SHOP;
}
/* 0x562b90 (Install Setup, class 0x79b6ec): with changes "purchase this
   graphics setup for your car?" (0x7f24787; yes 0xd05fc3a3 = 0x547570),
   else "make a selection to install before checking out!" (0x51e3f647) */
static void gfx_install_ask(Shop *s) {
    if (gfx_changed(s)) dialog(s, PK_DLG_S, 2, 0x07f24787u, DA_GFX_INSTALL, 0, 0);
    else dialog(s, PK_DLG_S, 1, 0x51e3f647u, DA_NONE, 0, 0);
}
/* 0xc519bfc0 on Paint / Vinyls / Decals: "cancel the changes to your ...
   setup?" (entry +0x48: 0 vinyl 0x6ee64972, 1 decal 0x449a9dd9, else paint
   0x1a311489) -> 0xe75749d4 -> 0x561a40 */
static void gfx_cancel_ask(Shop *s) {
    int sel = s->cat.sel;
    if (sel > 2) return;
    s->gfx_cancel = sel == 0 ? GFX_PAINT : sel == 1 ? GFX_VINYL : GFX_DECAL;
    dialog(s, PK_DLG_S, 2, sel == 1 ? 0x6ee64972u : sel == 2 ? 0x449a9dd9u : 0x1a311489u, DA_GFX_CANCEL, 0, 0);
}

/* a dialog's first button */
static void dlg_act(Shop *s, int act) {
    switch (act) {
    case DA_PAINT_ADD: paint_add(s); break;
    case DA_PART:                               /* install what is shown */
        s->fitted = s->preview;
        if (s->pending) s->debounce = 0.0f;
        break;
    case DA_RIM:
        memcpy(s->rim_folder, s->prev_folder, sizeof s->rim_folder);
        s->rim_style = s->prev_style;
        if (s->rim_pending) s->rim_debounce = 0.0f;
        s->size_mode = 0;   /* 0x9f00f07 installs, then 0x55fce0(0) leaves the size list */
        break;
    case DA_PERF_PKG: case DA_PERF_STOCK: perf_pkg_yes(s); break;
    case DA_PERF_PART: perf_part_yes(s); break;
    case DA_CTL_RESET: options_ctl_preset(&g_opt, 0); break;
    case DA_VINYL_ADD: vinyl_install(s); break;
    case DA_VINYL_CHANGE: vcat_enter(s); break;
    case DA_VINYL_REMOVE: vinyl_remove(s, 0); break;
    case DA_VINYL_UNIQUE: vinyl_remove(s, 1); break;
    case DA_DECAL_LAYOUT: decal_layout_take(s); break;
    case DA_TINT:
        s->fitted.tint = tint_of(&s->parts[s->part_sel]);
        s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
        printf("shop: window tint = %08x\n", (unsigned)s->fitted.tint);
        s->screen = SC_SPECIALTIES;
        break;
    case DA_CF_TRUNK: cf_toggle(s, 4u); break;
    case DA_SPEC: spec_install(s); break;
    case DA_CF_KIT: cf_toggle(s, 2u); break;
    case DA_BULB:
        s->fitted.bulb = s->parts[s->part_sel].hash;
        s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
        printf("shop: headlight bulb = %06x\n", (unsigned)s->fitted.bulb);
        s->screen = SC_SPECIALTIES;
        break;
    case DA_NEON: {
        int slot = neon_slot(s);
        s->fitted.neon[slot][0] = s->parts[s->part_sel].hash;
        s->fitted.neon[slot][1] = (unsigned)s->parts[s->part_sel].value;
        s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
        printf("shop: neon slot %d = %08x %08x\n", slot, (unsigned)s->fitted.neon[slot][0], (unsigned)s->fitted.neon[slot][1]);
        s->screen = SC_NEON;
        break;
    }
    case DA_HUD:
        s->fitted.hud = (unsigned)hud_style_sel(s);
        memcpy(s->fitted.hud_col, s->hud_pick, sizeof s->fitted.hud_col);
        s->preview = s->fitted;
        printf("shop: custom gauges = style %02u, colours %06x %06x %06x\n", s->fitted.hud,
               s->fitted.hud_col[0] & 0xffffffu, s->fitted.hud_col[1] & 0xffffffu, s->fitted.hud_col[2] & 0xffffffu);
        s->screen = SC_HUD;
        chud_palette(s, 0);
        chud_paint(s);
        break;
    case DA_DECAL_ADD: decal_set(s, s->dslot, s->parts[s->part_sel].hash); s->screen = SC_DLOC; break;
    case DA_DECAL_REMOVE: decal_set(s, s->dslot, 0); s->screen = SC_DLOC; break;
    case DA_DECAL_CLEAR:
        memset(s->fitted.decal[s->dlay_loc], 0, sizeof s->fitted.decal[s->dlay_loc]);
        s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
        break;
    case DA_GFX_INSTALL: gfx_install(s); break;
    case DA_GFX_LEAVE: gfx_install(s); s->screen = SC_CAR_SHOP; break;
    case DA_GFX_DISCARD: gfx_revert(s, GFX_ALL); s->screen = SC_CAR_SHOP; break;
    case DA_GFX_CANCEL: if (gfx_changed(s)) gfx_revert(s, s->gfx_cancel); break;
    default: break;
    }
}
static void ctl_input(Shop *s, int key);
static void shop_input_screen(Shop *s, int key) {
    if (dlg_open(s)) { dlg_input(s, key); return; }
    if (key == SHOP_KEY_TRI && s->screen != SC_CTL && s->screen != SC_JUKE) key = SHOP_KEY_BACK;   /* Triangle backs out */
    switch (s->screen) {
    case SC_SPLASH:
        if (key == SHOP_KEY_OK) s->screen = SC_MAIN;
        break;
    case SC_MAIN:
        if (key == SHOP_KEY_LEFT) scroller_move(&s->main, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->main, 1);
        else if (key == SHOP_KEY_OK && !s->main.locked[s->main.sel]) {
            if (k_main_menu[s->main.sel].group == MM_QUICK_RACE) s->screen = SC_QR_MODE;
            else if (k_main_menu[s->main.sel].group == MM_OPTIONS) s->screen = SC_OPT_MAIN;
            else s->fe_action = SHOP_ACT_CUSTOMIZE;
        }
        else if (key == SHOP_KEY_BACK) s->screen = SC_SPLASH;
        break;
    case SC_QR_MODE: case SC_QR_TRACK: case SC_QR_OPTIONS: case SC_QR_CAR:
        qr_input(s, key);
        break;
    case SC_OPT_MAIN: case SC_OPT:
        opt_input(s, key);
        break;
    case SC_CTL:
        ctl_input(s, key);
        break;
    case SC_JUKE:
        juke_input(s, key);
        break;
    case SC_CREDITS:   /* 0x911ab364: back to UI_OptionsMain (0xe1fde1d1 drops the text) */
        if (key == SHOP_KEY_BACK) { credits_free(s); s->screen = SC_OPT_MAIN; }
        break;
    case SC_CAR_SHOP:
        if (key == SHOP_KEY_LEFT) scroller_move(&s->top, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->top, 1);
        else if (key == SHOP_KEY_OK && !s->top.locked[s->top.sel]) {
            static const int to[CAT_COUNT] = { SC_BODY, SC_PERFORMANCE, SC_GRAPHICS, SC_SPECIALTIES };
            s->screen = to[s->top.sel];
            if (s->screen == SC_PERFORMANCE) scroller_set(&s->cat, k_perf_shop, NELEM(k_perf_shop));
            if (s->screen == SC_GRAPHICS) {
                scroller_set(&s->cat, k_graphics_shop, NELEM(k_graphics_shop));
                s->gfx_base = s->fitted;
            }
            if (s->screen == SC_SPECIALTIES) {
                scroller_set(&s->cat, k_special_shop, NELEM(k_special_shop));
                for (int i = 0; i < s->cat.n; i++)   /* Custom Gauges, Neon, Window Tint, Lights always; the rest 0x539d00 */
                    s->cat.locked[i] = i > 3 && spec_locked(s, s->cat.e[i].group);
            }
        } else if (key == SHOP_KEY_BACK) s->exit_req = 1;
        break;
    case SC_BODY:
        if (key == SHOP_KEY_LEFT) scroller_move(&s->body, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->body, 1);
        else if (key == SHOP_KEY_OK && !s->body.locked[s->body.sel]) {
            if (k_body_shop[s->body.sel].group == G_WHEEL) s->screen = SC_RIM_BRAND;
            else if (k_body_shop[s->body.sel].group == G_CARBON) cf_menu(s, 0);
            else open_parts(s);
        }
        else if (key == SHOP_KEY_BACK) s->screen = SC_CAR_SHOP;
        break;
    case SC_RIM_BRAND:
        if (key == SHOP_KEY_LEFT) scroller_move(&s->rims, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->rims, 1);
        else if (key == SHOP_KEY_OK) { build_rims(s, s->rims.sel); s->screen = SC_RIMS; }
        else if (key == SHOP_KEY_BACK) s->screen = SC_BODY;
        break;
    case SC_RIMS:
        if (s->size_mode) {   /* the size list (0x55fb00): previews each size, OK buys it */
            int n = s->rim_nsizes[s->part_sel];
            if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && n > 0) {   /* 0x55f740 */
                int k = s->size_sel + (key == SHOP_KEY_UP ? -1 : 1);
                if (k >= 0 && k < n) { s->size_sel = k; rim_preview(s, 0.35f); }
            } else if (key == SHOP_KEY_OK) {
                if (!strcmp(s->prev_folder, s->rim_folder) && s->prev_style == s->rim_style)
                    dialog(s, PK_DLG_S, 1, 0xda15d7e5u, DA_NONE, 0, 0);
                else dialog(s, PK_DLG_S, 2, 0xf5ac2b2au, DA_RIM, 0, 0);
            } else if (key == SHOP_KEY_BACK) {   /* 0x55fce0(1) */
                s->size_mode = 0; rim_preview(s, 0.0f);
            }
            break;
        }
        if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && s->nparts > 0) {
            s->part_sel = (s->part_sel + (key == SHOP_KEY_UP ? -1 : 1) + s->nparts) % s->nparts;
            rim_preview(s, 0.35f);
        } else if (key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) {
            scroller_move(&s->rims, key == SHOP_KEY_LEFT ? -1 : 1);
            if (strcmp(s->prev_folder, s->rim_folder) || s->prev_style != s->rim_style) {
                memcpy(s->prev_folder, s->rim_folder, sizeof s->prev_folder);
                s->prev_style = s->rim_style; s->rim_pending = 1; s->rim_debounce = 0.35f;
            }
            build_rims(s, s->rims.sel);
        } else if (key == SHOP_KEY_OK && s->nparts > 0 && s->rim_nsizes[s->part_sel] > 0) {
            /* 0x567a50 -> 0x55fb00: the style's sizes, on the fitted one */
            s->size_mode = 1; s->size_sel = 0;
            const RimBrand *r = &s->rb[s->rims.sel];
            if (r->folder && !strcmp(s->rim_folder, r->folder) && RIM_STYLE(s->rim_style) == s->parts[s->part_sel].value)
                for (int k = 0; k < s->rim_nsizes[s->part_sel]; k++)
                    if (s->rim_sizes[s->part_sel][k] == RIM_INNER(s->rim_style)) s->size_sel = k;
            s->size_scroll = (float)s->size_sel;
            rim_preview(s, 0.0f);
        } else if (key == SHOP_KEY_OK && s->nparts > 0) {
            /* 0x567a50: the same two, in GenericDialog_SMALL */
            if (!strcmp(s->prev_folder, s->rim_folder) && s->prev_style == s->rim_style)
                dialog(s, PK_DLG_S, 1, 0xda15d7e5u, DA_NONE, 0, 0);
            else dialog(s, PK_DLG_S, 2, 0xf5ac2b2au, DA_RIM, 0, 0);
        } else if (key == SHOP_KEY_BACK) {
            if (strcmp(s->prev_folder, s->rim_folder) || s->prev_style != s->rim_style) {
                memcpy(s->prev_folder, s->rim_folder, sizeof s->prev_folder);
                s->prev_style = s->rim_style; s->rim_pending = 1; s->rim_debounce = 0.0f;
            }
            s->screen = SC_RIM_BRAND;
        }
        break;
    case SC_PERFORMANCE: case SC_GRAPHICS: case SC_SPECIALTIES:
        if (key == SHOP_KEY_LEFT) scroller_move(&s->cat, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->cat, 1);
        else if (key == SHOP_KEY_OK && s->screen == SC_GRAPHICS && s->cat.sel == 0) paint_enter(s);
        else if (key == SHOP_KEY_OK && s->screen == SC_GRAPHICS && s->cat.sel == 1) vlayer_enter(s);
        else if (key == SHOP_KEY_OK && s->screen == SC_GRAPHICS && s->cat.sel == 2) dloc_enter(s);
        else if (key == SHOP_KEY_OK && s->screen == SC_GRAPHICS && s->cat.sel == 3) gfx_install_ask(s);
        else if (key == SHOP_KEY_SQ && s->screen == SC_GRAPHICS) gfx_cancel_ask(s);
        else if (key == SHOP_KEY_BACK && s->screen == SC_GRAPHICS) gfx_back(s);
        else if (key == SHOP_KEY_OK && s->screen == SC_SPECIALTIES && !s->cat.locked[s->cat.sel] &&
                 s->cat.e[s->cat.sel].group == 0x99) tint_enter(s);
        else if (key == SHOP_KEY_OK && s->screen == SC_SPECIALTIES && !s->cat.locked[s->cat.sel] &&
                 s->cat.e[s->cat.sel].group == 0x9f) bulb_enter(s);
        else if (key == SHOP_KEY_OK && s->screen == SC_SPECIALTIES && !s->cat.locked[s->cat.sel] &&
                 s->cat.e[s->cat.sel].group == 0x9e) neon_menu(s, 0);
        else if (key == SHOP_KEY_OK && s->screen == SC_SPECIALTIES && !s->cat.locked[s->cat.sel] &&
                 s->cat.e[s->cat.sel].group == 0xa3) hud_enter(s);
        else if (key == SHOP_KEY_OK && s->screen == SC_SPECIALTIES && !s->cat.locked[s->cat.sel] &&
                 s->cat.e[s->cat.sel].group == 0x22) ice_audio_enter(s);
        else if (key == SHOP_KEY_OK && s->screen == SC_SPECIALTIES && !s->cat.locked[s->cat.sel] &&
                 spec_group(s->cat.e[s->cat.sel].group) >= 0) { s->spec_ret = SC_SPECIALTIES; spec_enter(s, s->cat.e[s->cat.sel].group); }
        else if (key == SHOP_KEY_OK && s->screen == SC_PERFORMANCE && !s->cat.locked[s->cat.sel])
            perf_pkg_enter(s, k_perf_shop[s->cat.sel].group);
        else if (key == SHOP_KEY_BACK) s->screen = SC_CAR_SHOP;
        break;
    case SC_PAINT:
        paint_input(s, key);
        break;
    case SC_VLAYER: case SC_VCAT: case SC_VLIST:
        vinyl_input(s, key);
        break;
    case SC_DLOC: case SC_DPICK:
        decal_input(s, key);
        break;
    case SC_PERF_PKG:
        perf_pkg_input(s, key);
        break;
    case SC_TINT:
        tint_input(s, key);
        break;
    case SC_CARBON:
        cf_input(s, key);
        break;
    case SC_BULB:
        bulb_input(s, key);
        break;
    case SC_SPEC:
        spec_input(s, key);
        break;
    case SC_ICE:
        ice_input(s, key);
        break;
    case SC_NEON: case SC_NEON_LIST:
        neon_input(s, key);
        break;
    case SC_HUD: case SC_HUD_COL:
        hud_input(s, key);
        break;
    case SC_PARTS:
        if (key == SHOP_KEY_UP) parts_cursor(s, -1);
        else if (key == SHOP_KEY_DOWN) parts_cursor(s, 1);
        else if ((key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) && s->cf_mode) {
            int step = key == SHOP_KEY_LEFT ? -1 : 1;
            do scroller_move(&s->cfm, step); while (s->cfm.sel >= 4);   /* the lists only */
            if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
                s->preview = s->fitted; request_preview(s);
            }
            cf_open_parts(s);
        } else if (key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) {
            /* the top strip changes category without leaving the browser */
            int step = key == SHOP_KEY_LEFT ? -1 : 1;
            for (int t = 0; t < s->body.n; t++) {
                scroller_move(&s->body, step);
                if (!s->body.locked[s->body.sel] && s->body.e[s->body.sel].group != G_CARBON) break;
            }
            if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
                s->preview = s->fitted; request_preview(s);
            }
            open_parts(s);
        } else if (key == SHOP_KEY_OK) {
            /* 0x566490: the part fitted -> "already installed!", else
               "Would you like to install this part now?" */
            const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
            if (q && q->available) {
                if (!memcmp(&s->preview, &s->fitted, sizeof s->preview)) dialog(s, DLG_AUTO, 1, 0xda15d7e5u, DA_NONE, 0, 0);
                else dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_PART, 0, 0);
            }
        } else if (key == SHOP_KEY_BACK) {
            if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
                s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
            }
            if (s->cf_mode) cf_menu(s, 1);
            else s->screen = SC_BODY;
        }
        break;
    }
}

static int rating_shown(const Shop *s);
static void rating_update(Shop *s);
void shop_update(Shop *s, float dt) {
    if (!s) return;
    Scroller *list[14] = { &s->top, &s->body, &s->cat, &s->rims, &s->main, &s->qr_mode, &s->qr_track,
                           &s->opt_main, &s->paintcat, &s->perfpkg, &s->vlay, &s->vcat, &s->dloc, &s->cfm };
    s->blink += dt;
    if (rating_shown(s)) rating_update(s);
    {   /* DIM / UNDIM: 100 ms each way */
        float tgt = dlg_open(s) ? 1.0f : 0.0f, st = dt * 10.0f;
        s->dim = s->dim < tgt ? fminf(tgt, s->dim + st) : fmaxf(tgt, s->dim - st);
    }
    qr_update(s, dt);
    if (s->screen == SC_OPT && s->opt_anim) feng_anim_tick(s->opt_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_CTL && s->ctl_anim) feng_anim_tick(s->ctl_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_JUKE && s->juke_anim) feng_anim_tick(s->juke_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_CREDITS) credits_tick(s, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_PAINT && s->paint_anim) feng_anim_tick(s->paint_anim, (int)(dt * 1000.0f + 0.5f));
    if ((s->screen == SC_HUD || s->screen == SC_HUD_COL) && s->chud_anim) feng_anim_tick(s->chud_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_PERF_PKG && s->perf_anim) feng_anim_tick(s->perf_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_PERF_PKG && s->buy_anim) feng_anim_tick(s->buy_anim, (int)(dt * 1000.0f + 0.5f));
    if (dlg_open(s) && s->dlg_anim) feng_anim_tick(s->dlg_anim, (int)(dt * 1000.0f + 0.5f));
    for (int i = 0; i < 13; i++) {
        Scroller *sc = list[i];
        float target = (float)sc->sel;
        sc->scroll += (target - sc->scroll) * SCROLL_EASE;
        if (fabsf(target - sc->scroll) < 0.001f) sc->scroll = target;
        sc->pulse += PULSE_STEP;
        if (sc->pulse > 2.0f) sc->pulse = 0.0f;
    }
    s->part_scroll += ((float)s->part_sel - s->part_scroll) * SCROLL_EASE;
    s->size_scroll += ((float)s->size_sel - s->size_scroll) * SCROLL_EASE;
    if (s->pending && s->debounce > 0.0f) s->debounce -= dt;
    if (s->rim_pending && s->rim_debounce > 0.0f) s->rim_debounce -= dt;
}

int shop_poll(Shop *s, ShopAction *a) {
    if (!s || !a) return 0;
    if (s->fe_action) { a->kind = s->fe_action; s->fe_action = 0; return 1; }
    if (s->qr_car_pending && s->qr_car_debounce <= 0.0f) {   /* the car select's car on the platform */
        s->qr_car_pending = 0;
        a->kind = SHOP_ACT_QR_CAR;
        snprintf(a->car, sizeof a->car, "%s", s->qr_car_cur);
        return 1;
    }
    if (s->qr_go && !s->qr_car_pending) {
        s->qr_go = 0;
        memcpy(g_qr_set, s->set, sizeof g_qr_set); g_qr_set_ok = 1;
        g_qr_mode_sel = s->qr_mode.sel; g_qr_filter = s->qr_filter;
        a->kind = SHOP_ACT_QR_GO;
        snprintf(a->car, sizeof a->car, "%s", s->qr_car_cur);
        qr_fill(s, &a->qr);
        return 1;
    }
    if (s->exit_req && !s->rim_pending &&
        (strcmp(s->prev_folder, s->rim_folder) || s->prev_style != s->rim_style)) {
        memcpy(s->prev_folder, s->rim_folder, sizeof s->prev_folder);
        s->prev_style = s->rim_style; s->rim_pending = 1; s->rim_debounce = 0.0f;
    }
    if (s->rim_pending && s->rim_debounce <= 0.0f) {
        s->rim_pending = 0;
        a->kind = SHOP_ACT_RIM;
        snprintf(a->rim_folder, sizeof a->rim_folder, "%s", s->prev_folder);
        a->rim_style = s->prev_style;
        return 1;
    }
    if (s->exit_req && !s->pending && !s->rim_pending) {
        if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
            s->preview = s->fitted; s->pending = 1; s->debounce = 0.0f;   /* revert first */
        } else {
            s->exit_req = 0;
            a->kind = SHOP_ACT_EXIT; a->cfg = s->fitted;
            snprintf(a->rim_folder, sizeof a->rim_folder, "%s", s->rim_folder);
            a->rim_style = s->rim_style;
            return 1;
        }
    }
    if (s->pending && s->debounce <= 0.0f) {
        s->pending = 0;
        a->kind = SHOP_ACT_PREVIEW; a->cfg = s->preview;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- drawing */

/* pin: which screen edge the object hangs off (-1 left, 0 centre, 1 right);
   cover: a full-screen backdrop, scaled to fill the width */
/* fade: the DIM fade-out (0 none, 1 gone) */
typedef struct { Shop *s; const RProg *rp; float asp, pin; int cover; float fade; } DC;

/* FEng point (640x480, centre origin, y down) -> NDC. Widescreen without
   distortion (user choice 10-09): one FEng unit stays 1/480 of the screen
   height, and each element keeps its 4:3 offset from the edge its
   top-level group sits nearest (as the HUD does). Full-screen art is scaled
   up to cover the width instead, losing a little top and bottom. */
static void to_ndc(const DC *dc, float fx, float fy, float *nx, float *ny) {
    if (dc->cover) {
        *nx = fx / 320.0f;
        *ny = -fy * dc->asp / 320.0f;
        return;
    }
    *nx = dc->pin + (fx - dc->pin * 320.0f) / (240.0f * dc->asp);
    *ny = -fy / 240.0f;
}

/* edge of the screen an object's top-level group sits nearest. `attach`:
   a small image inside a centred panel's height (the strips' end arrows and
   scroll tab, the car strip's maker logo, panel scrollbars and end caps)
   stays with the panel instead of moving out to the screen edge -- on 4:3
   they are one piece. */
static float obj_pin(const FengPackage *pk, int i, int attach) {
    int r = i, guard = 0;
    while (pk->obj[r].parent >= 0 && pk->obj[r].parent < pk->nobj && guard++ < 64) r = pk->obj[r].parent;
    float x = pk->obj[r].ax;
    if (x >= -106.0f && x <= 106.0f) return 0.0f;
    const FengObj *o = &pk->obj[i];
    if (attach == 2) {   /* a panel screen: anything placed on a centred panel stays on it */
        for (int k = 0; k < pk->nobj; k++) {
            const FengObj *bx = &pk->obj[k];
            float bw = fabsf(bx->size[0]), bh = fabsf(bx->size[1]);
            if (bx->type != FENG_IMAGE || bw < 400.0f || bw >= 600.0f || bh < 20.0f || fabsf(bx->ax) > 106.0f) continue;
            if (fabsf(o->ax - bx->ax) <= bw * 0.5f + 24.0f && fabsf(o->ay - bx->ay) <= bh * 0.5f + 24.0f) return 0.0f;
        }
    }
    if (attach && o->type == FENG_IMAGE && fabsf(o->size[0]) <= 140.0f) {
        float oh = fabsf(o->size[1]) * 0.5f;
        for (int k = 0; k < pk->nobj; k++) {
            const FengObj *bx = &pk->obj[k];
            float bw = fabsf(bx->size[0]), bh = fabsf(bx->size[1]);
            if (bx->type != FENG_IMAGE || bw < 400.0f || bw >= 600.0f || bh < 20.0f || fabsf(bx->ax) > 106.0f) continue;
            if (o->ay - oh >= bx->ay - bh * 0.5f - 2.0f && o->ay + oh <= bx->ay + bh * 0.5f + 2.0f) return 0.0f;
        }
    }
    return x < -106.0f ? -1.0f : 1.0f;
}

static void put_vert(float *v, float x, float y, float u, float t) {
    v[0] = x; v[1] = y; v[2] = 0.0f; v[3] = u; v[4] = t;
}

/* `nq` quads (x,y,z,u,v per vertex, NDC) with one texture and colour, through
   the shop's own program: texture * colour, nothing else. */
static void draw_quads(const DC *dc, GLuint tex, const float *v, int nq, const float *c,
                       int additive) {
    Shop *s = dc->s;
    float al = c[3] * (1.0f - dc->fade);
    if (nq <= 0 || al <= 0.003f || !s->prog) return;
    glBindBuffer(GL_ARRAY_BUFFER, s->qb_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nq * 4 * 5 * sizeof(float)), v, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (const void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (const void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->qb_ibo);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1f(s->u_usetex, tex ? 1.0f : 0.0f);
    glUniform4f(s->u_color, c[0], c[1], c[2], al);
    if (additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDrawElements(GL_TRIANGLES, nq * 6, GL_UNSIGNED_SHORT, (const void *)0);
    if (additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* A sprite: FEng centre and size, FEng rotation (clockwise on screen) and
   texture rectangle uv = {u0, v0, u1, v1} (a flipped rectangle mirrors). */
static void blit_uv(const DC *dc, GLuint tex, float fw, float fh, float fx, float fy,
                    float angle, const float *uv, const float *c, int additive) {
    float cs = cosf(angle), sn = sinf(angle), hx = 0.5f * fw, hy = 0.5f * fh, v[20];
    static const float kx[4] = { -1, 1, 1, -1 }, ky[4] = { -1, -1, 1, 1 };
    float tu[4] = { uv[0], uv[2], uv[2], uv[0] }, tv[4] = { uv[1], uv[1], uv[3], uv[3] };
    for (int k = 0; k < 4; k++) {
        float px = kx[k] * hx, py = ky[k] * hy, nx, ny;
        to_ndc(dc, fx + px * cs - py * sn, fy + px * sn + py * cs, &nx, &ny);
        put_vert(v + 5 * k, nx, ny, tu[k], tv[k]);
    }
    draw_quads(dc, tex, v, 1, c, additive);
}

static void blit(const DC *dc, GLuint tex, float fw, float fh, float fx, float fy,
                 float angle, const float *c, int additive) {
    static const float full[4] = { 0, 0, 1, 1 };
    blit_uv(dc, tex, fw, fh, fx, fy, angle, full, c, additive);
}

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { printf("shop: shader compile failed\n"); glDeleteShader(sh); return 0; }
    return sh;
}

/* One program for every Shop, built once and kept for the process. Deleting
   it on shop_close made vitaGL force-unregister its sceGxmShaderPatcher id
   (release_shader -> sceGxmShaderPatcherForceUnregisterProgram), which the
   patcher shares between identical shader binaries and ignores the reference
   count of: the loading screen of an in-process track switch (a Shop opened
   and closed around the switch) left the patcher corrupt, and the next new
   vertex program crashed inside sceGxm (10-11 hwlog3/hwlog4). */
static GLuint g_shop_prog; static GLint g_shop_ucolor, g_shop_usetex, g_shop_utex;
static void make_program_once(Shop *s);
static void make_program(Shop *s) {
    if (!g_shop_prog) make_program_once(s);
    s->prog = g_shop_prog;
    s->u_color = g_shop_ucolor; s->u_usetex = g_shop_usetex; s->u_tex = g_shop_utex;
}
static void make_program_once(Shop *s) {
#if defined(__vita__) || defined(OPENUG2_GLES)
#define SHOP_GLSL "precision mediump float;\n"
#else
#define SHOP_GLSL "#version 120\n"
#endif
    static const char *vs = SHOP_GLSL
        "attribute vec3 aPos;\n"
        "attribute vec2 aUV;\n"
        "varying vec2 vUV;\n"
        "void main() { vUV = aUV; gl_Position = vec4(aPos, 1.0); }\n";
    static const char *fs = SHOP_GLSL
        "uniform sampler2D uTex;\n"
        "uniform vec4 uColor;\n"
        "uniform float uUseTex;\n"
        "varying vec2 vUV;\n"
        "void main() {\n"
        "  vec4 t = vec4(1.0);\n"
        "  if (uUseTex > 0.5) t = texture2D(uTex, vUV);\n"
        "  gl_FragColor = t * uColor;\n"
        "}\n";
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs), f = compile_shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return;
    GLuint pr = glCreateProgram();
    glAttachShader(pr, v); glAttachShader(pr, f);
    glBindAttribLocation(pr, 0, "aPos");
    glBindAttribLocation(pr, 1, "aUV");
    glLinkProgram(pr);
    GLint ok = 0; glGetProgramiv(pr, GL_LINK_STATUS, &ok);
    glDeleteShader(v); glDeleteShader(f);
    if (!ok) { printf("shop: program link failed\n"); glDeleteProgram(pr); return; }
    g_shop_prog = pr;
    g_shop_ucolor = glGetUniformLocation(pr, "uColor");
    g_shop_usetex = glGetUniformLocation(pr, "uUseTex");
    g_shop_utex = glGetUniformLocation(pr, "uTex");
    (void)s;
}

/* `txt` in the object's font and justification, anchored at (ax, ay) at
   scale sc. '^' is a line break. */
/* A string with a wrap width (Sw, in the font's own units): a line breaks
   at the last space before it runs past (the FEng string layout). '^' and
   newlines break as authored. */
static void wrap_text(const FengFont *f, const char *txt, float wrapw, char *out, int cap) {
    int n = 0, line0 = 0, sp = -1;
    for (const char *q = txt; *q && n < cap - 1; q++) {
        char ch = *q == '\n' ? '^' : *q;
        out[n++] = ch;
        if (ch == '^') { line0 = n; sp = -1; continue; }
        if (ch == ' ') sp = n - 1;
        out[n] = 0;
        if (sp >= 0 && feng_text_width(f, out + line0) > wrapw) {
            out[sp] = '^'; line0 = sp + 1; sp = -1;
        }
    }
    out[n] = 0;
}

static void text_at(Shop *s, const DC *dc, int p, int i, const char *txt, float tx, float ty,
                    float sc, const float *c) {
    const FengPackage *pk = s->pk[p];
    const FengObj *o = &pk->obj[i];
    if (!txt || !*txt || o->res < 0 || o->res >= pk->nres) return;
    int fi = s->font_of[p][o->res];
    if (fi < 0 || !s->font[fi].ok) return;
    const ShopFont *F = &s->font[fi];
    char line[160], wrapped[1024];
    if (o->wrapw > 0) { wrap_text(&F->f, txt, (float)o->wrapw, wrapped, (int)sizeof wrapped); txt = wrapped; }
    int nlines = 1;
    for (const char *q = txt; *q; q++) if (*q == '^') nlines++;
    /* 0x51c110: lines (leading Sl + the font's line) apart */
    float lh = ((float)o->lead + (float)F->f.line) * sc;
    float y = ty;
    if (o->just & FENG_JUST_VCENTER) y -= 0.5f * lh * nlines;
    else if (o->just & FENG_JUST_BOTTOM) y -= lh * nlines;
    const char *q = txt;
    while (*q) {
        int n = 0;
        while (q[n] && q[n] != '^' && n < (int)sizeof line - 1) { line[n] = q[n]; n++; }
        line[n] = 0;
        float x = tx, w = feng_text_width(&F->f, line) * sc;
        if (o->just & FENG_JUST_HCENTER) x -= 0.5f * w;
        else if (o->just & FENG_JUST_RIGHT) x -= w;
        float v[160 * 4 * 5]; int nq = 0;
        for (const char *ch = line; *ch && nq < 160; ch++) {
            const FengGlyph *g = &F->f.g[(unsigned char)*ch];
            if (!g->ok) continue;
            if (g->w && g->h) {
                float x0 = x + g->xo * sc, y0 = y + g->yo * sc;
                float x1 = x0 + g->w * sc, y1 = y0 + g->h * sc, ax, ay, bx, by;
                to_ndc(dc, x0, y0, &ax, &ay); to_ndc(dc, x1, y1, &bx, &by);
                float u0 = (float)g->u / F->aw, v0 = (float)g->v / F->ah;
                float u1 = (float)(g->u + g->w) / F->aw, v1 = (float)(g->v + g->h) / F->ah;
                float *qq = v + nq * 20;
                put_vert(qq, ax, ay, u0, v0); put_vert(qq + 5, bx, ay, u1, v0);
                put_vert(qq + 10, bx, by, u1, v1); put_vert(qq + 15, ax, by, u0, v1);
                nq++;
            }
            x += g->adv * sc;
        }
        draw_quads(dc, F->tex, v, nq, c, 0);
        y += lh;
        q += n;
        if (*q == '^') q++;
    }
}

/* `txt` in the object's font, size and justification, where it is authored. */
static void text(Shop *s, const DC *dc, int p, int i, const char *txt, const float *c) {
    const FengObj *o = &s->pk[p]->obj[i];
    text_at(s, dc, p, i, txt, o->ax + s->text_dx, o->ay + s->text_dy, o->size[0], c);
}

/* PC_Loading: how far a custom first line's right edge moves past the
   authored "LOADING", in FEng units (the circles move along by this much). */
static float loading_text_shift(Shop *s, const FengPackage *pk, int p) {
    if (!s->ls_line1[0]) return 0.0f;
    int i = feng_find(pk, 0xa96c3902u);
    if (i < 0) return 0.0f;
    const FengObj *o = &pk->obj[i];
    if (o->res < 0 || o->res >= pk->nres) return 0.0f;
    int fi = s->font_of[p][o->res];
    if (fi < 0 || !s->font[fi].ok) return 0.0f;
    const FengFont *f = &s->font[fi].f;
    float grow = (feng_text_width(f, s->ls_line1) - feng_text_width(f, o->text)) * o->size[0];
    /* "LOADING" is right-justified (Sj 6) with the circles after its right
       edge: a longer line grows to the left and the circles stay put */
    if (o->just & FENG_JUST_RIGHT) return 0.0f;
    return o->just & FENG_JUST_HCENTER ? 0.5f * grow : grow;
}

/* What each code-driven string shows; NULL = authored text, "" = hidden. */
typedef struct {
    const char *title, *option, *part, *price, *total;
    const uint32_t *xh; const char *const *xt; int nx;   /* more strings by object hash */
} Texts;


static const char *override_text(uint32_t h, const Texts *t) {
    for (int k = 0; k < t->nx; k++) if (t->xh[k] == h) return t->xt[k];
    switch (h) {
    case O_TITLE: return t->title;
    case O_OPTION_A: case O_OPTION_B: return t->option;
    case O_PART_NAME: return t->part ? t->part : "";
    case O_PART_PRICE: return t->price ? t->price : "";
    case O_TOTAL: return t->total ? t->total : "";
    /* no career yet: no bank and no profile name to show */
    case O_CASH: case O_PLAYER: return "";
    /* PC key prompts; the Vita has no icons for them in the game data */
    case O_HELP_LABEL: case O_HELP_TOKEN: return "";
    case O_SELECT_TOKEN: return "X";
    case 0x47ff4e7cu: return "";   /* "Event Handler" debug string */
    default: return NULL;
    }
}

/* PC prompts the packages author as tokens: accept / select is X on the
   Vita, the others (help, EA Messenger) have no function here. */
static const char *authored_text(const FengObj *o) {
    if (!strncmp(o->text, "$JOY_EVENT_FENG_", 16)) {
        if (!strncmp(o->text, "$JOY_EVENT_FENG_LTRIGGER$", 25)) return "L";
        if (!strncmp(o->text, "$JOY_EVENT_FENG_RTRIGGER$", 25)) return "R";
        return !strcmp(o->text, "$JOY_EVENT_FENG_SELECT$") || !strcmp(o->text, "$JOY_EVENT_FENG_ACCEPT$")
             ? "X" : "";
    }
    if (!strcmp(o->text, "EA Messenger")) return "";
    return o->text;
}

/* The horizontal icon strip: options in a row, SCROLL_SPACING apart, scrolled
   so the selected one sits in the centre slot; the others at SCROLL_UNSEL
   size, vertically centred; locked ones at LOCKED_ALPHA. */
static void draw_scroller(Shop *s, const DC *dc, const Scroller *sc, float cx, float cy,
                          float w, float h) {
    float step = w + SCROLL_SPACING;
    for (int i = 0; i < sc->n; i++) {
        float x = cx + ((float)i - sc->scroll) * step;
        int sel = i == sc->sel;
        float k = sel ? 1.0f : SCROLL_UNSEL;
        if (sel) {   /* 0x538760: alpha ramp -> scale 0.97..1.0 */
            float tri = sc->pulse < 1.0f ? sc->pulse : 2.0f - sc->pulse;
            float a = tri * 255.0f * 1.1f + 127.5f; if (a > 255.0f) a = 255.0f;
            k = 0.97f + a * 0.000117647f;
        }
        if (fabsf(x) + 0.5f * w * k > 282.0f) continue;   /* outside the strip box */
        const ShopTex *t = tex_find(s, sc->e[i].tex);
        float c[4] = { 1, 1, 1, sc->locked[i] ? LOCKED_ALPHA : 1.0f };
        if (t) blit(dc, t->tex, w * k, h * k, x, cy, 0.0f, c, t->additive);
    }
}

static void slot_geometry(const Shop *s, int p, uint32_t slot, float *x, float *y, float *w, float *h) {
    *x = 0.0f; *y = -150.0f; *w = 118.0f; *h = 59.0f;
    int i = s->pk[p] ? feng_find(s->pk[p], slot) : -1;
    if (i >= 0) {
        const FengObj *o = &s->pk[p]->obj[i];
        *x = o->ax; *y = o->ay; *w = fabsf(o->size[0]); *h = fabsf(o->size[1]);
    }
}

/* Part list: mini icons in a column, the selected one on the highlight. */
static void draw_part_list(Shop *s, const DC *dc, int p) {
    const FengPackage *pk = s->pk[p];
    int hb = feng_find(pk, O_MINI_BACK), up = feng_find(pk, O_MINI_UP), dn = feng_find(pk, O_MINI_DOWN);
    float x = -268.0f, y0 = 48.0f, top = -69.0f, bot = 168.0f;
    if (hb >= 0) { x = pk->obj[hb].ax; y0 = pk->obj[hb].ay; }
    if (up >= 0) top = pk->obj[up].ay;
    if (dn >= 0) bot = pk->obj[dn].ay;
    const ShopTex *mini = tex_find(s, K_MINI_ICON), *lock = tex_find(s, K_LOCK_ICON);
    float size = 16.0f, step = size + SCROLL_SPACING;
    /* the rim browser's size list (0x55f8f0) takes the column over */
    int sized = s->screen == SC_RIMS && s->size_mode;
    int n = sized ? s->rim_nsizes[s->part_sel] : s->nparts, cur = sized ? s->size_sel : s->part_sel;
    float scroll = sized ? s->size_scroll : s->part_scroll;
    for (int i = 0; i < n; i++) {
        float y = y0 + ((float)i - scroll) * step;
        if (y - 8.0f < top + 11.0f || y + 8.0f > bot - 11.0f) continue;
        int sel = i == cur, avail = sized || s->parts[i].available;
        float k = sel ? 1.0f : SCROLL_UNSEL;
        float c[4] = { 1, 1, 1, (avail ? 180.0f : 150.0f) / 255.0f };
        if (sel) c[3] = 1.0f;
        if (mini) blit(dc, mini->tex, size * k, size * k, x, y, 0.0f, c, 0);
        if (!avail && lock)
            blit(dc, lock->tex, 12.0f * k, 12.0f * k, x + 6.0f, y + 6.0f, 0.0f, c, 0);
    }
    /* slider tab along the column */
    int tab = feng_find(pk, O_MINI_TAB);
    if (tab >= 0 && n > 1) {
        const FengObj *o = &pk->obj[tab];
        float f = (float)cur / (float)(n - 1);
        GLuint tx = s->res_tex[p][o->res];
        if (tx) blit(dc, tx, fabsf(o->size[0]), fabsf(o->size[1]), o->ax,
                     top + 11.0f + f * (bot - top - 22.0f), 0.0f, s->rgba[p][tab], 0);
    }
}

/* The whole package in painter's order, with the code-drawn layers slotted in
   at their authored depth: the icon strip at its centre slot's z (so it sits
   on the backing and, per package, under or over the highlight) and the part
   list at the mini-icon pool's z (100). The scroll tabs follow the cursor. */
/* IconScrollerMenu StartDim / StopDim (0x538c00 / 0x538ca0) and the
   browsers' own: a dialog over the screen runs DIM on its Event Handler,
   whose event (a301c015 to the package's Targ list) fades those objects --
   and what is in them -- out in 100 ms; UNDIM (9f218e63) back in 100 ms.
   Packages whose DIM sends nothing (UI_ChoosePerformanceCategory /
   _Package, UI_ChooseVinylLayer) stay. */
#define O_EVENT_HANDLER 0x47ff4e7cu
#define FENG_SCR_DIM    0x00009e99u
static int dim_pk(int p) {
    return p == PK_CATEGORY || p == PK_PARTS || p == PK_RIMBRAND || p == PK_RIMS || p == PK_PAINT ||
           p == PK_PERF_BUY || p == PK_DECAL || p == PK_CHUD;
}
static int dim_obj(Shop *s, int p, int i) {
    const FengPackage *pk = s->pk[p];
    if (!pk || !dim_pk(p)) return 0;
    if (!s->dim_done[p]) {
        s->dim_done[p] = 1;
        memset(s->dim_mask[p], 0, sizeof s->dim_mask[p]);
        int eh = feng_find(pk, O_EVENT_HANDLER);
        const FengObj *e = eh >= 0 ? &pk->obj[eh] : NULL;
        for (int q = 0; e && q < e->nscr; q++) {
            const FengScript *sc = &pk->scr[e->scr0 + q];
            if (sc->hash != FENG_SCR_DIM) continue;
            for (int v = 0; v < sc->nev; v++) {
                const FengEvent *ev = &pk->ev[sc->ev0 + v];
                if (ev->target != -4) continue;
                for (int k = 0; k < pk->ntg; k++) {
                    if (pk->tg[k].msg != ev->msg) continue;
                    for (int o = 0; o < pk->nobj; o++)
                        if (pk->obj[o].guid == pk->tg[k].guid) s->dim_mask[p][o >> 3] |= (unsigned char)(1 << (o & 7));
                }
            }
        }
    }
    for (int q = i; q >= 0 && q < pk->nobj; q = pk->obj[q].parent)
        if (s->dim_mask[p][q >> 3] & (1 << (q & 7))) return 1;
    return 0;
}

static void draw_package(Shop *s, const DC *dc, int p, const Texts *t, int show_lock,
                         const Scroller *strip, int part_list) {
    const FengPackage *pk = s->pk[p];
    if (!pk) return;
    float cx, cy, w, h, slot_z = 10.0f;
    uint32_t slot = p == PK_PARTS ? O_SLOT_B : p == PK_QR_TRACK ? O_QR_TRACK_SLOT : O_SLOT_A;
    slot_geometry(s, p, slot, &cx, &cy, &w, &h);
    int si = feng_find(pk, slot);
    if (si >= 0) slot_z = pk->obj[si].az;
    /* the code-drawn strip and part list follow the objects they sit on */
    DC strip_dc = *dc, list_dc = *dc;
    strip_dc.cover = list_dc.cover = 0;
    strip_dc.pin = si >= 0 ? obj_pin(pk, si, 1) : 0.0f;
    {   int hb = feng_find(pk, O_MINI_BACK); list_dc.pin = hb >= 0 ? obj_pin(pk, hb, 1) : 0.0f; }
    int strip_done = !strip, list_done = !part_list;
    if (s->dim > 0.0f && dim_pk(p)) {   /* the strip's icons and the list rows are DIM targets too */
        dim_obj(s, p, 0);
        int any = 0;
        for (int q = 0; q < FENG_MAX_OBJ / 8; q++) any |= s->dim_mask[p][q];
        strip_dc.fade = list_dc.fade = any ? s->dim : 0.0f;
    }
    const DC *dc_in = dc;
    for (int k = 0; k < s->ndraw[p]; k++) {
        int i = s->draw[p][k];
        const FengObj *o = &pk->obj[i];
        if (!strip_done && o->az < slot_z) { draw_scroller(s, &strip_dc, strip, cx, cy, w, h); strip_done = 1; }
        if (!list_done && o->az < 100.0f) { draw_part_list(s, &list_dc, p); list_done = 1; }
        DC odc = *dc_in;
        odc.fade = s->dim > 0.0f && dim_obj(s, p, i) ? s->dim : 0.0f;
        odc.pin = obj_pin(pk, i, p == PK_QR_TRACK ? 2 : p != PK_SPLASH && p != PK_LOADING);
        /* PC_Loading: "LOADING" (group x -125), its gradient (-113) and the
           circles (-44) are one strip; the per-group pin split them on a wide
           screen (text on the left edge, circles centred) */
        if (p == PK_LOADING) odc.pin = -1.0f;
        odc.cover = o->type != FENG_STRING && fabsf(o->size[0]) >= 600.0f && fabsf(o->size[1]) >= 440.0f;
        dc = &odc;
        const float *c = s->rgba[p][i];
        if (p == PK_LOADING) {   /* PC_Loading: animated by script in the game */
            if (o->hash == 0x5b9d84dbu) continue;                    /* "EVENT HANDLER" debug text */
            if (o->hash >= 0x50d98304u && o->hash <= 0x50d98308u) { /* the five circles, lit in turn */
                float ph = fmodf(s->blink * 4.0f, 7.0f) - (float)(o->hash - 0x50d98304u);
                float a = 1.0f - fabsf(ph) / 1.5f; if (a < 0.15f) a = 0.15f;
                float b[4] = { c[0], c[1], c[2], a };
                GLuint tx = o->res > 0 && o->res < pk->nres ? s->res_tex[p][o->res] : 0;
                /* a longer first line pushes the circles along after it */
                float shift = loading_text_shift(s, pk, p);
                if (tx) blit(dc, tx, fabsf(o->size[0]), fabsf(o->size[1]), o->ax + shift, o->ay, o->aangle, b, 0);
                continue;
            }
            if (o->hash == 0xa96c3902u && s->ls_line1[0]) {           /* "LOADING" */
                text(s, dc, p, i, s->ls_line1, c);
                if (s->ls_line2[0]) {
                    float c2[4] = { c[0], c[1], c[2], c[3] * 0.85f };
                    s->text_dy = 22.0f;
                    text(s, dc, p, i, s->ls_line2, c2);
                    if (s->ls_line3[0]) {
                        s->text_dy = 44.0f;
                        text(s, dc, p, i, s->ls_line3, c2);
                    }
                    s->text_dy = 0.0f;
                }
                continue;
            }
        }
        if (o->hash == O_LOCK && !show_lock) continue;
        if (o->hash == O_PRESS_START) {   /* the script's fade, as a slow pulse */
            float b[4] = { c[0], c[1], c[2], 0.55f + 0.45f * cosf(s->blink * 3.0f) };
            text(s, dc, p, i, o->text, b);
            continue;
        }
        const ObjOv *ov_img = NULL;
        for (int k = 0; k < s->nov; k++) if (s->ov[k].hash == o->hash) ov_img = &s->ov[k];
        if (ov_img && ov_img->hide) continue;
        float occ[4];
        if (ov_img && ov_img->col) {   /* 0x51d7f0 */
            occ[0] = (float)(ov_img->col >> 16 & 255) / 255.0f; occ[1] = (float)(ov_img->col >> 8 & 255) / 255.0f;
            occ[2] = (float)(ov_img->col & 255) / 255.0f; occ[3] = (float)(ov_img->col >> 24) / 255.0f;
            c = occ;
        }
        {   /* a hidden group hides what is in it */
            int gone = 0;
            for (int q = o->parent; q >= 0 && !gone; q = pk->obj[q].parent)
                for (int k = 0; k < s->nov; k++) if (s->ov[k].hide && s->ov[k].hash == pk->obj[q].hash) gone = 1;
            if (gone) continue;
        }
        if (o->type == FENG_STRING) {
            const char *ov = override_text(o->hash, t);
            text(s, dc, p, i, ov ? ov : authored_text(o), c);
            continue;
        }
        if (ov_img && ov_img->tex) {   /* the code's texture (logos, the track map) */
            float b[4] = { c[0], c[1], c[2], c[3] * (ov_img->alpha > 0.0f ? ov_img->alpha : 1.0f) };
            blit(dc, ov_img->tex, ov_img->geom ? ov_img->w : fabsf(o->size[0]),
                 ov_img->geom ? ov_img->h : fabsf(o->size[1]), ov_img->geom ? ov_img->x : o->ax,
                 ov_img->geom ? ov_img->y : o->ay, o->aangle, b, 0);
            continue;
        }
        if (o->res <= 0 || o->res >= pk->nres) continue;   /* untextured = code slots */
        GLuint tex = s->res_tex[p][o->res];
        if (!tex) continue;
        const ShopTex *st = tex_find(s, feng_tex_key(pk->res[o->res].name));
        if (ov_img && ov_img->geom) {  /* a slider's handle / fill */
            blit(dc, tex, ov_img->w, ov_img->h, ov_img->x, ov_img->y, o->aangle, c, st && st->additive);
            continue;
        }
        float x = o->ax;
        if (o->hash == O_SCROLL_TAB && strip && strip->n > 1)   /* along the 562-wide bar */
            x = -273.0f + 546.0f * (float)strip->sel / (float)(strip->n - 1);
        if (o->hash == O_MINI_TAB) continue;                     /* drawn by the part list */
        float uv[4] = { o->uv[0], o->uv[1], o->uv[2], o->uv[3] };
        if (uv[0] == uv[2] || uv[1] == uv[3]) { uv[0] = 0; uv[1] = 0; uv[2] = 1; uv[3] = 1; }
        blit_uv(dc, tex, fabsf(o->size[0]), fabsf(o->size[1]), x, o->ay, o->aangle, uv, c,
                st && st->additive);
    }
    if (!strip_done) draw_scroller(s, &strip_dc, strip, cx, cy, w, h);
    if (!list_done) draw_part_list(s, &list_dc, p);
}

/* A package through its running scripts (UI_QRModeOptions): every object at
   its animated place, colour and size, far to near; otxt[i] replaces object
   i's string. The option rows hang off the centred panel. */
static void draw_anim_package(Shop *s, const DC *dc0, int p, const FengAnim *a, const Texts *t,
                              const char *const *otxt) {
    const FengPackage *pk = s->pk[p];
    if (!pk || !a) return;
    static short ord[FENG_MAX_OBJ];
    int n = 0;
    for (int i = 0; i < pk->nobj; i++) {
        if (pk->obj[i].type == FENG_GROUP) continue;
        int k = n++;
        while (k > 0 && pk->obj[ord[k - 1]].az < pk->obj[i].az) { ord[k] = ord[k - 1]; k--; }
        ord[k] = (short)i;
    }
    const ShopTex *arrow = tex_find(s, s->row_arrow ? s->row_arrow : K_MENU_ARROW);
    for (int k = 0; k < n; k++) {
        int i = ord[k];
        const FengObj *o = &pk->obj[i];
        FengPlace pl; feng_anim_place(a, i, &pl);
        if (pl.hidden || pl.rgba[3] <= 0.003f) continue;
        if (fabsf(pl.x) > 330.0f || fabsf(pl.y) > 250.0f) continue;   /* pools off screen */
        DC dc = *dc0; dc.cover = 0;
        dc.fade = s->dim > 0.0f && dim_obj(s, p, i) ? s->dim : 0.0f;
        dc.pin = s->qr_rowobj[i] ? 0.0f : obj_pin(pk, i, p == PK_QR_OPTIONS ? 1 : 2);
        if (o->type == FENG_STRING) {
            const char *tx = otxt ? otxt[i] : NULL;
            if (!tx) { const char *ov = override_text(o->hash, t); tx = ov ? ov : authored_text(o); }
            text_at(s, &dc, p, i, tx, pl.x, pl.y, pl.sx, pl.rgba);
            continue;
        }
        GLuint tex = 0; int add = 0;
        if (p == PK_CHUD)   /* custom gauges: the style's art (0x549000) */
            for (int q = 0; q < 6; q++) if (o->hash == k_chud_obj[q] && s->chud_tex[q]) tex = s->chud_tex[q];
        if (tex) ;
        else if (s->qr_rowobj[i] == 2) { if (arrow) tex = arrow->tex; }   /* 0x54ef30: U2_Menu_Arrow */
        else if (o->res > 0 && o->res < pk->nres) {
            tex = s->res_tex[p][o->res];
            const ShopTex *st = tex_find(s, feng_tex_key(pk->res[o->res].name));
            add = st && st->additive;
        }
        if (!tex) continue;
        float uv[4];
        for (int q = 0; q < 4; q++) memcpy(&uv[q], &a->data[i][17 + q], 4);
        if (uv[0] == uv[2] || uv[1] == uv[3]) { uv[0] = 0; uv[1] = 0; uv[2] = 1; uv[3] = 1; }
        blit_uv(&dc, tex, fabsf(pl.sx), fabsf(pl.sy), pl.x, pl.y, pl.angle, uv, pl.rgba, add);
    }
}

/* 0x55bc90: the box is up on every Customize screen but the parts / rims /
   spinner browsers, the paint and the decal screens */
static int rating_shown(const Shop *s) {
    switch (s->screen) {
    case SC_CAR_SHOP: case SC_BODY: case SC_PERFORMANCE: case SC_GRAPHICS: case SC_SPECIALTIES:
    case SC_RIM_BRAND: case SC_PERF_PKG: case SC_VLAYER: case SC_VCAT: case SC_NEON: case SC_CARBON:
    case SC_HUD: case SC_HUD_COL: case SC_ICE:
        return s->db != NULL;
    default: return 0;
    }
}
static int rating_in_graphics(const Shop *s) {
    return s->screen == SC_GRAPHICS || s->screen == SC_PAINT || s->screen == SC_VLAYER || s->screen == SC_VCAT ||
           s->screen == SC_VLIST || s->screen == SC_DLOC || s->screen == SC_DPICK;
}
static void rating_update(Shop *s) {
    const N2CarConfig *b = rating_in_graphics(s) ? &s->gfx_base : &s->fitted;
    if (s->rate_ok && !memcmp(b, &s->rate_b, sizeof *b) && !memcmp(&s->preview, &s->rate_a, sizeof s->preview) &&
        !strcmp(s->rate_rim[0], s->rim_folder) && !strcmp(s->rate_rim[1], s->prev_folder) &&
        s->rate_style[0] == s->rim_style && s->rate_style[1] == s->prev_style) return;
    s->rate_b = *b; s->rate_a = s->preview;
    snprintf(s->rate_rim[0], sizeof s->rate_rim[0], "%s", s->rim_folder);
    snprintf(s->rate_rim[1], sizeof s->rate_rim[1], "%s", s->prev_folder);
    s->rate_style[0] = s->rim_style; s->rate_style[1] = s->prev_style;
    s->rate_inst = shop_rating(s, b, s->rim_folder, s->rim_style);
    s->rate_prev = shop_rating(s, &s->preview, s->prev_folder[0] ? s->prev_folder : s->rim_folder,
                               s->prev_folder[0] ? s->prev_style : s->rim_style);
    printf("shop: visual rating %.2f (preview %.2f)\n", s->rate_inst, s->rate_prev);
    s->rate_ok = 1;
}
/* 0x54b000 / 0x53da10 / 0x540e90 (vertical slider): "%$.2f" of the
   preview's rating, the bar its fraction above the whole number; colours
   0x5104d0: the same (202,244,138), better (128,255,0), worse (207,128,48) */
static void draw_rating(Shop *s, const DC *dc) {
    const FengPackage *pk = s->pk[PK_REP];
    if (!pk) return;
    float v = s->rate_prev, inst = s->rate_inst;
    uint32_t col = v > inst ? 0xff80ff00u : v < inst ? 0xffcf8030u : 0xffcaf48au;
    int ib = feng_find(pk, O_VR_BASE), it = feng_find(pk, O_VR_SLIDER), ih = feng_find(pk, O_VR_HANDLE);
    if (ib < 0) return;
    const FengObj *ob = &pk->obj[ib];
    float len = fabsf(ob->size[0]) - 88.0f;            /* the base's length less 2 x 44 */
    float t = v - floorf(v); if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    float bot = len * 0.5f + ob->ay, fl = t * len * 0.95f;
    s->nov = 0;
    ObjOv *ov;
    if (it >= 0) {
        ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_VR_SLIDER; ov->geom = 1;
        ov->x = ob->ax + 1.0f; ov->y = bot - fl - (t > 0.0f ? 7.0f : 0.0f);
        ov->w = fabsf(pk->obj[it].size[0]); ov->h = fabsf(pk->obj[it].size[1]);
    }
    if (ih >= 0) {
        ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_VR_HANDLE; ov->col = col;
        if (fl <= 0.0f) ov->hide = 1;
        else { ov->geom = 1; ov->x = ob->ax + 4.0f; ov->y = bot - fl * 0.5f - 7.0f; ov->w = fl; ov->h = fabsf(pk->obj[ih].size[1]); }
    }
    ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_VR_BEG; ov->hide = 1;
    ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_VR_END; ov->hide = 1;
    ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_VR_TEXT; ov->col = col;
    ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_VR_STAR; ov->col = col;
    static char txt[16];
    snprintf(txt, sizeof txt, "%.2f", v);
    static const uint32_t xh[1] = { O_VR_TEXT };
    const char *xt[1] = { txt };
    Texts rt; memset(&rt, 0, sizeof rt); rt.xh = xh; rt.xt = xt; rt.nx = 1;
    draw_package(s, dc, PK_REP, &rt, 0, NULL, 0);
    s->nov = 0;
}

void shop_draw(Shop *s, const RProg *rp, GpuMesh *quad, int width, int height) {
    if (!s || width <= 0 || height <= 0) return;   /* rp NULL: before the engine's renderer exists */
    (void)quad;
    if (!s->prog) return;
    DC dc; dc.s = s; dc.rp = rp; dc.asp = (float)width / (float)height; dc.pin = 0.0f; dc.cover = 0; dc.fade = 0.0f;
    GLboolean depth_on = glIsEnabled(GL_DEPTH_TEST), blend_on = glIsEnabled(GL_BLEND);
    GLboolean cull_on = glIsEnabled(GL_CULL_FACE);
    GLboolean depth_mask = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s->prog);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(s->u_tex, 0);

    Texts t; memset(&t, 0, sizeof t);
    char price[32], total[32];
    switch (s->screen) {
    case SC_CAR_SHOP: case SC_BODY: case SC_GRAPHICS: case SC_SPECIALTIES: case SC_PERFORMANCE: {
        const Scroller *sc = s->screen == SC_CAR_SHOP ? &s->top : s->screen == SC_BODY ? &s->body : &s->cat;
        int p = s->screen == SC_PERFORMANCE && s->pk[PK_PERFORMANCE] ? PK_PERFORMANCE : PK_CATEGORY;
        uint32_t title = s->screen == SC_CAR_SHOP ? STR_CAR_SHOP : s->screen == SC_BODY ? STR_BODY_SHOP :
                         s->screen == SC_PERFORMANCE ? STR_PERF_SHOP :
                         s->screen == SC_GRAPHICS ? STR_GRAPHICS : STR_SPECIALTIES;
        t.title = str_get(s, title);
        t.option = sc->n ? str_get(s, sc->e[sc->sel].str) : "";
        draw_package(s, &dc, p, &t, 0, sc, 0);
        break;
    }
    case SC_NEON:
        t.title = str_get(s, STR_SPECIALTIES);
        t.option = s->neonm.n ? str_get(s, s->neonm.e[s->neonm.sel].str) : "";
        draw_package(s, &dc, PK_CATEGORY, &t, 0, &s->neonm, 0);
        break;
    case SC_NEON_LIST: {
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        int slot = neon_slot(s);
        t.title = str_get(s, STR_SPECIALTIES);
        t.option = s->neonm.n ? str_get(s, s->neonm.e[s->neonm.sel].str) : "";
        t.part = q ? q->name : "";
        snprintf(price, sizeof price, "$%d", q ? q->price : 0);
        snprintf(total, sizeof total, "$%d", q && (q->hash != s->fitted.neon[slot][0] ||
                 (unsigned)q->value != s->fitted.neon[slot][1]) ? q->price : 0);
        t.price = price; t.total = total;
        draw_package(s, &dc, PK_PARTS, &t, 0, &s->neonm, 1);
        break;
    }
    case SC_HUD: case SC_HUD_COL: {
        if (!s->pk[PK_CHUD] || !s->chud_anim) break;
        static const uint32_t prompt[3] = { 0x3a9ccf4eu, 0xded7ca79u, 0x6a0b254cu };
        uint32_t xh[1] = { O_CHUD_PROMPT };
        const char *xt[1] = { str_get(s, prompt[s->hud_layer < 0 || s->hud_layer > 2 ? 0 : s->hud_layer]) };
        Texts ct; memset(&ct, 0, sizeof ct);
        ct.xh = xh; ct.xt = xt; ct.nx = 1;
        if (s->screen == SC_HUD_COL) chud_cursor(s);   /* follows the palette's Show slide */
        draw_anim_package(s, &dc, PK_CHUD, s->chud_anim, &ct, NULL);
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        t.part = q ? q->name : "";
        snprintf(price, sizeof price, "$%d", q ? q->price : 0);
        t.price = price;
        if (s->screen == SC_HUD) {   /* the overlay goes while the colours are chosen (0x563d00) */
            s->nov = 0;
            ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0x0937e8f1u; ov->hide = 1;   /* the icon pool */
            draw_package(s, &dc, PK_CHUDOV, &t, 0, NULL, 1);
            s->nov = 0;
        }
        break;
    }
    case SC_CARBON:
        t.title = str_get(s, STR_BODY_SHOP);
        t.option = s->cfm.n ? str_get(s, s->cfm.e[s->cfm.sel].str) : "";
        draw_package(s, &dc, PK_CATEGORY, &t, 0, &s->cfm, 0);
        break;
    case SC_SPLASH:
        draw_package(s, &dc, PK_SPLASH, &t, 0, NULL, 0);
        break;
    case SC_QR_MODE:
        t.title = str_get(s, STR_QR_MODE_TITLE);
        t.option = s->qr_mode.n ? str_get(s, s->qr_mode.e[s->qr_mode.sel].str) : "";
        draw_package(s, &dc, PK_QR_MODE, &t, 0, &s->qr_mode, 0);
        break;
    case SC_QR_TRACK: {   /* 0x4ce170 */
        const QrSet *q = &s->set[s->qr_type];
        int ev = q->ev;
        const unsigned char *r = qr_rec(s, ev);
        static char dist[48], comment_lbl[40];
        /* metres x 0.001, English default imperial (0x83aa1a 0): x 0.62137 mi */
        float km = r ? (float)(int)n2_u32(r + 0x98) * 0.001f : 0.0f;
        snprintf(dist, sizeof dist, "%.2f %s", km * 0.62137f, str_get(s, STR_MI));
        snprintf(comment_lbl, sizeof comment_lbl, "QR_TRACK_%d_COMMENT", ev);
        uint32_t best = s->qr_type == QR_DRIFT ? 0x3b7b9567u
                      : s->qr_type == QR_SPRINT || s->qr_type == QR_DRAG ? 0xdb0c5efdu : 0x3b4a1ecbu;
        static const uint32_t xh[6] = { O_QR_TRACK_NAME, O_QR_RACE_TYPE, O_QR_DISTANCE,
                                        O_QR_BEST_LABEL, O_QR_BEST, O_QR_COMMENT };
        const char *xt[6] = { r ? qr_track_name(s, ev, q->dir && r[0x86] == 1) : "",
                              str_get(s, qr_type_str(s->qr_type)), dist, str_get(s, best),
                              "",                       /* no records without a profile */
                              str_get(s, feng_hash(comment_lbl)) };
        t.title = str_get(s, STR_QR_TRACK_TITLE);
        t.xh = xh; t.xt = xt; t.nx = 6;
        s->nov = 0;
        ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov);
        ov->hash = O_QR_MAP;            /* TRACKMAP<event>, faded in once loaded */
        if (s->map_tex && s->map_ev == ev && s->map_alpha > 0.0f) { ov->tex = s->map_tex; ov->alpha = s->map_alpha; }
        else ov->hide = 1;
        ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_QR_DEBUG; ov->hide = 1;
        if (s->qr_track.sel == 0) { ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_SCROLL_L; ov->hide = 1; }
        if (s->qr_track.sel >= s->qr_track.n - 1) { ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_SCROLL_R; ov->hide = 1; }
        draw_package(s, &dc, PK_QR_TRACK, &t, 0, &s->qr_track, 0);
        s->nov = 0;
        break;
    }
    case SC_QR_OPTIONS: {
        const FengPackage *pk = s->pk[PK_QR_OPTIONS];
        static const char *otxt[FENG_MAX_OBJ];
        static char val[QR_ROWS_SHOWN][32];
        memset(otxt, 0, sizeof otxt);
        for (int k = 0; pk && k < s->qr_nrow; k++) {
            int on = qr_pool(pk, QP_NAME, k + 1), od = qr_pool(pk, QP_DATA, k + 1);
            qr_row_text(s, s->qr_row[k], val[k], sizeof val[k]);
            if (on >= 0) otxt[on] = str_get(s, k_qo_name[s->qr_row[k]]);
            if (od >= 0) otxt[od] = val[k];
        }
        draw_anim_package(s, &dc, PK_QR_OPTIONS, s->qr_anim, &t, otxt);
        break;
    }
    case SC_QR_CAR: {   /* 0x4b2310 / 0x497cd0 */
        const FengPackage *pk = s->pk[PK_QR_CAR];
        const ShopCar *c = s->qr_ncar ? &s->cars[s->qr_car_list[s->qr_car_sel]] : NULL;
        static const uint32_t xh[4] = { O_TITLE, O_QR_FILTER, O_QR_RACE_MODE, O_QR_CUSTOMIZE };
        const char *xt[4] = { str_get(s, STR_QR_CAR_TITLE), str_get(s, k_qr_filters[s->qr_filter].str),
                              str_get(s, qr_type_str(s->qr_type)), "" };
        t.xh = xh; t.xt = xt; t.nx = 4;
        s->nov = 0;
        const ShopTex *ph = tex_find(s, K_GENERIC_PLACEHOLDER);
        char nm[64];
        ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_QR_LOGO;
        snprintf(nm, sizeof nm, "CARSELECT_MANUFACTURER_%s", c ? c->make : "");
        const ShopTex *lt = c ? tex_find(s, feng_tex_key(nm)) : NULL;
        if (!lt) lt = ph;
        if (lt) ov->tex = lt->tex; else ov->hide = 1;
        ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_QR_BADGE;
        snprintf(nm, sizeof nm, "SECONDARY_LOGO_%s", c ? c->folder : "");
        const ShopTex *bt = c ? tex_find(s, feng_tex_key(nm)) : NULL;
        if (!bt) bt = ph;
        if (bt) ov->tex = bt->tex; else ov->hide = 1;
        /* the stat sliders (0x540d20): 46 in from each end of the backing,
           the fill from there to the handle */
        float v3[3] = { c ? c->accel : 0.0f, c ? c->top_speed : 0.0f, c ? c->handling : 0.0f };
        for (int k = 0; k < 3 && pk; k++) {
            int ib = feng_find(pk, k_qr_slider[k][0]), ih = feng_find(pk, k_qr_slider[k][1]),
                ic = feng_find(pk, k_qr_slider[k][2]);
            if (ib < 0) continue;
            float bw = fabsf(pk->obj[ib].size[0]), x0 = pk->obj[ib].ax - 0.5f * bw + 46.0f;
            float len = bw - 92.0f, f = v3[k] < 0.0f ? 0.0f : v3[k] > 1.0f ? 1.0f : v3[k];
            if (ih >= 0) {
                ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = k_qr_slider[k][1]; ov->geom = 1;
                ov->x = x0 + f * len; ov->y = pk->obj[ih].ay;
                ov->w = fabsf(pk->obj[ih].size[0]); ov->h = fabsf(pk->obj[ih].size[1]);
            }
            if (ic >= 0) {
                ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = k_qr_slider[k][2]; ov->geom = 1;
                ov->w = f * len; ov->h = fabsf(pk->obj[ic].size[1]);
                ov->x = x0 + 0.5f * ov->w; ov->y = pk->obj[ic].ay;
                if (ov->w <= 0.0f) ov->hide = 1;
            }
        }
        if (s->qr_car_sel == 0) { ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_SCROLL_L; ov->hide = 1; }
        if (s->qr_car_sel >= s->qr_ncar - 1) { ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_SCROLL_R; ov->hide = 1; }
        draw_package(s, &dc, PK_QR_CAR, &t, 0, NULL, 0);
        s->nov = 0;
        break;
    }
    case SC_LOADING:
        draw_package(s, &dc, PK_LOADING, &t, 0, NULL, 0);
        break;
    case SC_OPT_MAIN:
        t.title = str_get(s, STR_OPTIONS);
        t.option = str_get(s, s->opt_main.e[s->opt_main.sel].str);
        draw_package(s, &dc, s->pk[PK_OPT_MAIN] ? PK_OPT_MAIN : PK_MAIN, &t, 0, &s->opt_main, 0);
        break;
    case SC_OPT: {
        const FengPackage *pk = s->pk[PK_OPTIONS];
        static const char *otxt[FENG_MAX_OBJ];
        memset(otxt, 0, sizeof otxt);
        for (int k = 0; pk && k < s->opt_nrow; k++) {
            int on = qr_pool(pk, QP_NAME, k + 1), od = qr_pool(pk, QP_DATA, k + 1);
            if (on >= 0) otxt[on] = str_get(s, k_or_name[s->opt_row[k]]);
            if (od >= 0 && !opt_is_slider(s->opt_row[k])) otxt[od] = str_get(s, opt_row_value(s->opt_row[k]));
        }
        t.title = str_get(s, k_opt_main[s->opt_main.sel].str);
        draw_anim_package(s, &dc, PK_OPTIONS, s->opt_anim, &t, otxt);
        break;
    }
    case SC_CTL: {
        const FengPackage *pk = s->pk[PK_CTL];
        static const char *otxt[FENG_MAX_OBJ];
        memset(otxt, 0, sizeof otxt);
        for (int k = 0; pk && k < OPT_ACT_COUNT; k++) {
            int on = ctl_pool(pk, "OPTION_NAME_%d", k + 1), oa = ctl_pool(pk, "OPTION_DATA_%d", k + 1),
                ob = ctl_pool(pk, "OPTION_DATA_2_%d", k + 1);
            if (on >= 0) otxt[on] = str_get(s, k_ctl_name[k]);
            if (oa >= 0) otxt[oa] = g_vb_name[g_opt.ctl[k][0] < VB_COUNT ? g_opt.ctl[k][0] : 0];
            if (ob >= 0) otxt[ob] = g_vb_name[g_opt.ctl[k][1] < VB_COUNT ? g_opt.ctl[k][1] : 0];
        }
        static const uint32_t xh[4] = { O_CTL_DEVICE, O_CTL_PRIMARY, O_CTL_SECONDARY, 0x4f79f553u };
        const char *xt[4] = { str_get(s, STR_CTL_JOYPAD), str_get(s, 0x1b15f53cu), str_get(s, 0x37e740e0u),
                              str_get(s, s->ctl_instr) };
        t.xh = xh; t.xt = xt; t.nx = 4;
        t.title = str_get(s, 0x04dc6db5u);   /* "Controls" */
        draw_anim_package(s, &dc, PK_CTL, s->ctl_anim, &t, otxt);
        break;
    }
    case SC_JUKE: {
        const FengPackage *pk = s->pk[PK_JUKE];
        static const char *otxt[FENG_MAX_OBJ];
        static char num[JUKE_SLOTS][8];
        memset(otxt, 0, sizeof otxt);
        for (int k = 0; pk && k < JUKE_SLOTS; k++) {
            int i = s->juke_top + k;
            if (i >= MUSIC_SONGS) break;
            int song = g_opt.pl_song[i];
            snprintf(num[k], sizeof num[k], i < 9 ? "0%d" : "%d", i + 1);
            int o;
            if ((o = juke_obj(s, JW_NUM, k)) >= 0) otxt[o] = num[k];
            if ((o = juke_obj(s, JW_ARTIST, k)) >= 0) otxt[o] = music_artist(song);
            if ((o = juke_obj(s, JW_TITLE, k)) >= 0) otxt[o] = music_title(song);
            if ((o = juke_obj(s, JW_ALBUM, k)) >= 0) otxt[o] = music_album(song);
            if ((o = juke_obj(s, JW_MODE, k)) >= 0) otxt[o] = str_get(s, k_juke_mode_str[g_opt.pl_mode[i] & 3]);
        }
        static const uint32_t xh[1] = { O_JUKE_SHUFFLE };
        const char *xt[1] = { str_get(s, g_opt.shuffle ? 0xa9c9c8f7u : 0x4ca36b89u) };   /* Random / Sequential */
        t.xh = xh; t.xt = xt; t.nx = 1;
        t.title = str_get(s, 0xdcfb6b36u);
        draw_anim_package(s, &dc, PK_JUKE, s->juke_anim, &t, otxt);
        break;
    }
    case SC_PAINT: {
        const FengPackage *pk = s->pk[PK_PAINT];
        if (!pk || !s->paint_anim) break;
        static const uint32_t xh[2] = { O_PAINT_TEXT, O_OPTION_A };
        char brand[64];
        snprintf(brand, sizeof brand, "%s", str_get(s, hash_more(s->paint_brand, "_PAINT")));
        if (s->vmode >= 0) brand[0] = 0;   /* VINYL_PAINT has one brand: no name */
        const char *xt[2] = { brand, str_get(s, s->paintcat.e[s->paintcat.sel].str) };
        t.xh = xh; t.xt = xt; t.nx = 2;
        t.title = NULL;   /* the package's own "Paint Shop" */
        draw_anim_package(s, &dc, PK_PAINT, s->paint_anim, &t, NULL);
        float cx, cy, w, h;
        slot_geometry(s, PK_PAINT, O_SLOT_A, &cx, &cy, &w, &h);
        int si = feng_find(pk, O_SLOT_A);
        DC sd = dc; sd.cover = 0; sd.pin = si >= 0 ? obj_pin(pk, si, 1) : 0.0f;
        draw_scroller(s, &sd, &s->paintcat, cx, cy, w, h);
        break;
    }
    case SC_PERF_PKG: {
        const FengPackage *pk = s->pk[PK_PERF_PKG];
        if (!pk || !s->perf_anim) break;
        static const char *otxt[FENG_MAX_OBJ];
        memset(otxt, 0, sizeof otxt);
        for (int n = 0; n < s->perf_nrow; n++) {
            int on = perf_obj(s, "PartName_%d", n + 1), op = perf_obj(s, "PartPrice_%d", n + 1);
            if (on >= 0) otxt[on] = str_get(s, s->perf_row_name[n]);
            if (op >= 0) otxt[op] = str_get(s, s->perf_row_price[n]);
        }
        static const uint32_t xh[2] = { O_PERF_HEADER, O_PERF_TITLE };
        const char *xt[2] = { str_get(s, perf_cat_label(s->perf_cat)), str_get(s, k_perf_pkg[s->perfpkg.sel].str) };
        t.xh = xh; t.xt = xt; t.nx = 2;
        t.title = NULL;
        memset(s->qr_rowobj, 0, sizeof s->qr_rowobj);
        draw_anim_package(s, &dc, PK_PERF_PKG, s->perf_anim, &t, otxt);
        float cx, cy, w, h;
        slot_geometry(s, PK_PERF_PKG, O_SLOT_A, &cx, &cy, &w, &h);
        int si = feng_find(pk, O_SLOT_A);
        DC sd = dc; sd.cover = 0; sd.pin = si >= 0 ? obj_pin(pk, si, 1) : 0.0f;
        draw_scroller(s, &sd, &s->perfpkg, cx, cy, w, h);
        const FengPackage *bp = s->pk[PK_PERF_BUY];
        if (bp && s->buy_anim && k_perf_pkg[s->perfpkg.sel].group == 0) {   /* the parts list under it */
            memset(otxt, 0, sizeof otxt);
            for (int k = 0; k < s->buy_n; k++) {
                int on = qr_pool(bp, QP_NAME, k + 1), od = qr_pool(bp, QP_DATA, k + 1);
                if (on >= 0) otxt[on] = str_get(s, s->buy_name[k]);
                if (od >= 0) otxt[od] = str_get(s, buy_installed(s, k) ? 0xed56be1fu : 0x4287206fu);
            }
            int o;
            if ((o = feng_find(bp, O_BUY_TORQUE)) >= 0) otxt[o] = s->buy_txt[0];
            if ((o = feng_find(bp, O_BUY_POWER)) >= 0) otxt[o] = s->buy_txt[1];
            Texts bt; memset(&bt, 0, sizeof bt);
            buy_marks(s);
            draw_anim_package(s, &dc, PK_PERF_BUY, s->buy_anim, &bt, otxt);
            memset(s->qr_rowobj, 0, sizeof s->qr_rowobj);
        }
        break;
    }
    case SC_CREDITS: {
        const FengPackage *pk = s->pk[PK_CREDITS];
        FengAnim *a = s->cred_anim;
        if (!pk || !a) break;
        static const char *otxt[FENG_MAX_OBJ];
        memset(otxt, 0, sizeof otxt);
        int tm = feng_find(pk, O_CREDITS_AREA);
        if (tm >= 0) otxt[tm] = "";   /* the template itself shows nothing */
        draw_anim_package(s, &dc, PK_CREDITS, a, &t, otxt);
        if (tm < 0) break;
        uint32_t roll = feng_hash_upper("RollCredit");
        DC dl = dc; dl.cover = 0; dl.pin = obj_pin(pk, tm, 1);
        for (int i = 0; i < s->cred_nroll; i++) {
            feng_anim_eval_at(a, tm, roll, s->cred_roll[i].t);
            FengPlace pl; feng_anim_place(a, tm, &pl);
            if (pl.rgba[3] <= 0.003f) continue;
            text_at(s, &dl, PK_CREDITS, tm, s->cred_line[s->cred_roll[i].line], pl.x, pl.y, pl.sx, pl.rgba);
        }
        break;
    }
    case SC_MAIN:
        t.title = str_get(s, STR_MAIN_MENU);
        t.option = str_get(s, s->main.e[s->main.sel].str);
        draw_package(s, &dc, s->pk[PK_MAIN] ? PK_MAIN : PK_CATEGORY, &t, 0, &s->main, 0);
        break;
    case SC_RIM_BRAND:
        t.title = str_get(s, STR_RIMS);
        t.option = s->rims.n ? str_get(s, s->rims.e[s->rims.sel].str) : "";
        draw_package(s, &dc, s->pk[PK_RIMBRAND] ? PK_RIMBRAND : PK_CATEGORY, &t, 0, &s->rims, 0);
        break;
    case SC_RIMS: {
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        t.title = str_get(s, STR_RIM_STYLES);
        t.option = s->rims.n ? str_get(s, s->rims.e[s->rims.sel].str) : "";
        t.part = q ? q->name : "";
        static char sized[64];   /* 0x5468a0: "%s %d", the name and the INNER_RADIUS of the size list's part */
        if (q && s->size_mode) {
            snprintf(sized, sizeof sized, "%s %d", q->name, s->rim_sizes[s->part_sel][s->size_sel]);
            t.part = sized;
        }
        int changed = strcmp(s->prev_folder, s->rim_folder) || s->prev_style != s->rim_style;
        snprintf(price, sizeof price, "$%d", q ? q->price : 0);
        snprintf(total, sizeof total, "$%d", q && changed ? q->price : 0);
        t.price = price; t.total = total;
        draw_package(s, &dc, s->pk[PK_RIMS] ? PK_RIMS : PK_PARTS, &t, 0, &s->rims, 1);
        break;
    }
    case SC_VLAYER: {
        t.title = str_get(s, STR_VLAYER_TITLE);
        t.option = str_get(s, s->vlay.e[s->vlay.sel].str);
        /* the arrows and the reorder hint with a vinyl to move (0x5535d0) */
        int movable = s->fitted.vinyl[s->vlay.sel] && vinyl_unique_at(&s->fitted) < 0;
        static const uint32_t hide[] = { O_VL_ARROW_L, O_VL_ARROW_R, O_VL_LTRIG, O_VL_RTRIG, O_VL_REORDER };
        s->nov = 0;
        {   /* Customize, not the career: no cost (0x545460) */
            ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0x9ff84a88u; ov->hide = 1;
        }
        for (int k = 0; k < NELEM(hide); k++) {
            int on = k == 0 || k == 2 ? s->vlay.sel > 0 : k == 1 || k == 3 ? s->vlay.sel < VINYL_LAYERS - 1 : 1;
            if (movable && on) continue;
            ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = hide[k]; ov->hide = 1;
        }
        draw_package(s, &dc, s->pk[PK_VLAYER] ? PK_VLAYER : PK_CATEGORY, &t, 0, &s->vlay, 0);
        s->nov = 0;
        break;
    }
    case SC_DLOC: case SC_DPICK: {
        /* UI_DecalMain: its own "DECAL SHOP" title; the slot's decals over it
           in UI_DecalsOverlay. Customize, not the career: no cost (0x545460),
           no price (0x568450) */
        t.title = NULL;
        t.option = s->dloc.n ? str_get(s, s->dloc.e[s->dloc.sel].str) : "";
        s->nov = 0;
        ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0x9ff84a88u; ov->hide = 1;
        draw_package(s, &dc, s->pk[PK_DECAL] ? PK_DECAL : PK_CATEGORY, &t, 0, &s->dloc, 0);
        if (s->screen == SC_DPICK && s->pk[PK_DECALOV]) {
            const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
            Texts ot; memset(&ot, 0, sizeof ot);
            ot.part = q ? q->name : "";
            ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_PART_PRICE; ov->hide = 1;
            draw_package(s, &dc, PK_DECALOV, &ot, 0, NULL, 1);
        }
        s->nov = 0;
        break;
    }
    case SC_VCAT:
        t.title = str_get(s, STR_GRAPHICS);
        t.option = str_get(s, s->vcat.e[s->vcat.sel].str);
        draw_package(s, &dc, PK_CATEGORY, &t, s->vcat.locked[s->vcat.sel], &s->vcat, 0);
        break;
    case SC_VLIST: {
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        t.title = str_get(s, STR_GRAPHICS);
        t.option = str_get(s, s->vcat.e[s->vcat.sel].str);
        t.part = q ? q->name : "";
        int changed = q && q->hash != s->fitted.vinyl[s->vlayer];
        snprintf(price, sizeof price, "$%d", q ? q->price : 0);
        snprintf(total, sizeof total, "$%d", changed ? q->price : 0);
        t.price = price; t.total = total;
        draw_package(s, &dc, PK_PARTS, &t, 0, &s->vcat, 1);
        break;
    }
    case SC_TINT: {
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        t.title = str_get(s, STR_SPECIALTIES);
        t.option = s->cat.n ? str_get(s, s->cat.e[s->cat.sel].str) : "";
        t.part = q ? q->name : "";
        snprintf(price, sizeof price, "$%d", q ? q->price : 0);
        snprintf(total, sizeof total, "$%d", q && tint_of(q) != s->fitted.tint ? q->price : 0);
        t.price = price; t.total = total;
        draw_package(s, &dc, PK_PARTS, &t, 0, &s->cat, 1);
        break;
    }
    case SC_ICE:
        t.title = str_get(s, STR_TRUNK_AUDIO);
        t.option = !s->icem.n ? "" : s->ice_mode == 4 ? ice_slot_label(s, s->icem.e[s->icem.sel].group) :
                   str_get(s, s->icem.e[s->icem.sel].str);
        draw_package(s, &dc, PK_CATEGORY, &t, 0, &s->icem, 0);
        break;
    case SC_SPEC: {
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        int ice = s->spec_slot >= 0x22 && s->spec_slot <= 0x2e;
        t.title = str_get(s, ice ? STR_TRUNK_AUDIO : STR_SPECIALTIES);
        t.option = ice_comp_slot(s->spec_slot) ? ice_slot_label(s, s->spec_slot) :
                   s->spec_slot == 0x22 ? str_get(s, 0xc8d23393u) :
                   s->cat.n ? str_get(s, s->cat.e[s->cat.sel].str) : "";
        t.part = q ? q->name : "";
        snprintf(price, sizeof price, "$%d", q ? q->price : 0);
        snprintf(total, sizeof total, "$%d", q && !spec_is_fitted(s, q) ? q->price : 0);
        t.price = price; t.total = total;
        draw_package(s, &dc, PK_PARTS, &t, 0, ice ? &s->icem : &s->cat, 1);
        break;
    }
    case SC_BULB: {
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        t.title = str_get(s, STR_SPECIALTIES);
        t.option = s->cat.n ? str_get(s, s->cat.e[s->cat.sel].str) : "";
        t.part = q ? q->name : "";
        snprintf(price, sizeof price, "$%d", q ? q->price : 0);
        snprintf(total, sizeof total, "$%d", q && q->hash != s->fitted.bulb ? q->price : 0);
        t.price = price; t.total = total;
        draw_package(s, &dc, PK_PARTS, &t, 0, &s->cat, 1);
        break;
    }
    case SC_PARTS: {
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        t.title = str_get(s, STR_BODY_SHOP);
        t.option = s->cf_mode ? str_get(s, s->cfm.e[s->cfm.sel].str) : str_get(s, s->body.e[s->body.sel].str);
        t.part = q ? q->name : "";
        int changed = memcmp(&s->preview, &s->fitted, sizeof s->preview) != 0;
        snprintf(price, sizeof price, "$%d", q ? q->price : 0);
        snprintf(total, sizeof total, "$%d", q && changed ? q->price : 0);
        t.price = price; t.total = total;
        draw_package(s, &dc, PK_PARTS, &t, q && !q->available, s->cf_mode ? &s->cfm : &s->body, 1);
        break;
    }
    }
    if (rating_shown(s)) draw_rating(s, &dc);
    if (dlg_open(s) && s->dlg_anim) {         /* the dialog over the screen */
        const FengPackage *pk = s->pk[s->dlg_pk];
        static const char *otxt[FENG_MAX_OBJ];
        static unsigned char keep[FENG_MAX_OBJ];
        memset(otxt, 0, sizeof otxt);
        int o;
        if ((o = feng_find(pk, O_DLG_BLURB)) >= 0) otxt[o] = s->dlg_text;
        if ((o = feng_find(pk, O_DLG_TEXT)) >= 0) otxt[o] = s->dlg_nbtn == 2 ? str_get(s, s->dlg_b1) : "";
        if ((o = feng_find(pk, O_DLG_TEXT + 1)) >= 0) otxt[o] = s->dlg_nbtn == 2 ? str_get(s, s->dlg_b2) : "";
        if ((o = feng_find(pk, O_DLG_TEXT + 2)) >= 0) otxt[o] = s->dlg_nbtn == 1 ? str_get(s, s->dlg_b1) : "";
        Texts dt; memset(&dt, 0, sizeof dt);
        memcpy(keep, s->qr_rowobj, sizeof keep);
        memset(s->qr_rowobj, 0, sizeof s->qr_rowobj);
        draw_anim_package(s, &dc, s->dlg_pk, s->dlg_anim, &dt, otxt);
        memcpy(s->qr_rowobj, keep, sizeof keep);
    }

    if (rp) glUseProgram(rp->prog);          /* the caller's program again */
    glDepthMask(depth_mask);
    if (depth_on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend_on) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cull_on) glEnable(GL_CULL_FACE);
}
