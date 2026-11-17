/* shop.c — the Car Shop screens. See shop.h for what comes from where. */
#include "shop.h"
#include "audio.h"
#include "music.h"
#include "options.h"
#include "feng.h"
#include "hud.h"
#include "perfparts.h"
#include "vinyl.h"
#include "n2sim.h"
#include "profile.h"
#include "resource.h"   /* LOAD_PUMP */
#include "savenote.h"
#include "pcsave.h"
#include "career.h"
#include "airace.h"
#include "beyond.h"
#include "movie.h"
#include "mplay.h"
#include "beyond.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __vita__
#define SHOP_CACHE "ux0:data/NFSU2/cache/shop_v32.bin"
#else
#define SHOP_CACHE "cache/shop_v33.bin"
#endif

/* ------------------------------------------------------------ game tables */

/* Packages (FRONTEND/FrontB.lzc). */
enum { PK_CATEGORY, PK_PERFORMANCE, PK_PARTS, PK_RIMBRAND, PK_RIMS, PK_SPLASH, PK_MAIN, PK_LOADING,
       PK_QR_MODE, PK_QR_TRACK, PK_QR_OPTIONS, PK_QR_CAR, PK_OPT_MAIN, PK_OPTIONS, PK_CTL, PK_JUKE, PK_CREDITS, PK_PAINT, PK_PERF_PKG, PK_PERF_BUY,
       PK_VLAYER, PK_DECAL, PK_DECALOV, PK_DLG_S, PK_DLG_M, PK_DLG_L, PK_CHUD, PK_CHUDOV, PK_REP, PK_HELP_S, PK_HELP_M,
       PK_PROFILE, PK_MC, PK_MC_LIST, PK_KEYBOARD, PK_START_CAREER, PK_CAREER_LOT, PK_CRIB, PK_CRIB_CARSEL, PK_CRIB_SPONSORS, PK_UNIQUE, PK_CRIB_WMAP, PK_MAG_REWARD, PK_MAG_BACK, PK_CRIB_REWARDS, PK_MAG_SELECT, PK_MAG_VIEW, PK_BRAND, PK_LOADING_CTL, PK_COUNT };
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
    "HelpDialog_SMALL.fng",                 /* a screen's Help: its controls (0x526c40) */
    "HelpDialog_MED.fng",                   /* ... more than 4 lines of them */
    "UI_ProfileManager.fng",                /* Profile Manager (0x4c58b0) */
    "MC_Main.fng",                          /* the save / load host: its messages and questions (0x4fd570) */
    "MC_List.fng",                          /* ... the profiles to load or delete (0x4d7070) */
    "UI_VirtualKeyboard.fng",               /* ... the name entry (FEKeyboard 0x4e3300) */
    "UI_StartCareer.fng",                   /* Career: Resume / New / Load (800d1f7c) */
    "UI_CareerCarLot.fng",                  /* Career: the Car Lot (UICareerCarLot 800d34bc) */
    "UI_CareerCrib.fng",                    /* Career: the crib, the career's main menu (uiCareerCrib 800d2268) */
    "UI_CareerCarSelect.fng",               /* Career: the crib's car select (UICareerCribCarSelect 800d4b80) */
    "UI_RewardsSponsor.fng",                /* Career: the crib's signed sponsors (uiCribReward_Sponsors 800d2ef0) */
    "UI_ChooseUniquePart.fng",              /* Career: a world challenge's unique parts (UniquesBrowser 8016cc14) */
    "UI_CareerWorldMap.fng",                /* Career: the crib's World Map, the event replay (CribWorldMapScreen 0x4ee690) */
    "UI_MagazineReward.fng",                /* Career: a front-end showcase's cover (UIMagazineReward 801868fc) */
    "UI_MagazineBack.fng",                  /* ... its background, under the car (UIMagazineBack 80187200) */
    "UI_CribRewardOptionsMain.fng",         /* Career: the PC crib's reward options (Setup 0x4afb70) */
    "UI_MagazineSelect.fng",                /* ... Show Magazines / Show DVDs (UIMagazineSelect, PC create 0x55b520) */
    "UI_MagazineView.fng",                  /* ... one saved cover photo (UIMagazineView, PC ctor 0x553be0) */
    "UI_PerformanceBrandSelect.fng",        /* Performance Shop: a bought part's brand (ChooseBrandsScreen 0x550330) */
    "Loading_Controller.fng",               /* the console loading screen with the controls picture (GLOBALB, F1.11) */
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
/* The crib's Change Parts (BeginCarCustomize entry 1): Setup__23ChooseCustomizeCategory
   (GC 8016104c) adds the paint IconOption (type 2, 0xc6089ca7 / 0xdc99aafe) only when
   the customize entry (803d84c8 +0x18 = DAT_803d84e0) != 1 (801611a0..801611b0), so the
   crib shows Body / Performance / Specialties. The car-lot option (type 5, 0x7edc9715 /
   0x8244f164) needs FEDatabase +0x125f0 == 0x100, the online mode UIQRCarSelect sets
   (800dea84) -- never offline. */
static const ShopEntry k_car_shop_crib[] = {
    { 0xa92b4ae8u, 0xb802f10eu, 0 }, { 0xeca6b0ccu, 0xc56d3032u, 0 }, { 0xc10d7979u, 0x3f40e75fu, 0 },
};
static const int k_car_shop_crib_cat[] = { CAT_BODY, CAT_PERFORMANCE, CAT_SPECIALTIES };

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
/* Setup__18ChoosePerfCategory 80171f20: career with a city shop's entry
   (gCarCustomizeManager+0x18 == 0) adds a Test Track IconOption (tex
   7a9b397a, str 8513f2c0, PerfMenuStartTuning) after the nine categories;
   its 0x34dc1bec -> DoStartTuning 801720f0 -> BeginCarTuning(0, car, entry 1) */
#define PERF_TEST_TRACK 0x7e
static const ShopEntry k_perf_shop_career[] = {
    { 0x026eea5eu, 0xc15c94e6u, 3 }, { 0xa7884765u, 0x502abeedu, 7 },
    { 0x73ad2f72u, 0x01a29ffau, 1 }, { 0x9c34c75fu, 0x178475e7u, 5 },
    { 0xa7886f38u, 0x9701bde4u, 2 }, { 0xabca6aefu, 0x06e8e477u, 8 },
    { 0xaa8959adu, 0xbaa23a28u, 6 }, { 0x758d5e3cu, 0xea57d2c4u, 0 },
    { 0xabd0ff14u, 0x06ef789cu, 4 },
    { 0x8513f2c0u, 0x7a9b397au, PERF_TEST_TRACK },
};
/* Setup__23ChooseCustomizeCategory 8016104c: for customize entry 2 / 3 / 4 (BeginCarCustomize's
   entry point, 803d84e0 -- 2 = the front end's Customize from UIQRCarSelect 800debfc; 1 the
   crib's CCChangeParts 80304120, 0 a city shop) the Car Shop ends with the Test Track IconOption
   (PerfMenuStartTuning: 0x34dc1bec -> 0x461a18ee -> BeginCarTuning(.., entry 2)) */
static const ShopEntry k_car_shop_fe[] = {
    { 0xa92b4ae8u, 0xb802f10eu, 0 }, { 0xeca6b0ccu, 0xc56d3032u, 0 },
    { 0xc6089ca7u, 0xdc99aafeu, 0 }, { 0xc10d7979u, 0x3f40e75fu, 0 },
    { 0x8513f2c0u, 0x7a9b397au, PERF_TEST_TRACK },
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
static const ShopEntry k_graphics_shop_career[] = {
    { 0x6a4c46a6u, 0x550681a4u, 0 }, { 0x78deea1cu, 0x55778e5au, 0 },   /* Paint, Vinyls */
    { 0x4e933e83u, 0xda1dae54u, 0 }, { 0x920dbb02u, 0x911a4edeu, 0 },   /* Decals, Install Setup (career: Purchase Setup 0x920dbb02) */
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
enum { MM_NONE, MM_QUICK_RACE, MM_CUSTOMIZE, MM_OPTIONS, MM_PROFILE, MM_CAREER };
static const ShopEntry k_main_menu[] = {
    { 0x5e7e1192u, 0x03704f3du, MM_CAREER },      /* Career          MAIN_ICON_CAREER */
    { 0x24909fd7u, 0x4e6fbb02u, MM_QUICK_RACE },  /* Quick Race      MAIN_ICON_QUICK_RACE */
    { 0x7b63ff85u, 0x771849a5u, MM_NONE },        /* Online          MAIN_ICON_GO_ONLINE */
    { 0x7294434cu, 0xd13a2e66u, MM_NONE },        /* LAN             MAIN_ICON_LAN */
    { 0xa53039a3u, 0xb0c46023u, MM_CUSTOMIZE },   /* Customize       MAIN_ICON_CUSTOMIZE_CAR */
    { 0xbcb18f38u, 0x6b303856u, MM_PROFILE },     /* Profile Manager MAIN_ICON_PROFILE_MANAG */
    { 0xed1f0b2cu, 0x3058fe37u, MM_OPTIONS },     /* Options         MAIN_ICON_OPTIONS */
};
#define STR_MAIN_MENU  0x1398f642u   /* UI_MAIN_TITLE */

/* EnterMemcard (0x4a3100) flags: the mode (1 an overlay), the operation,
   0x200 always confirm a save, 0x1000 a cancel while named is a success,
   0x2000 keep the setup, 0x4000 a forced save; 0x200000 the create
   question was asked (DoSaveFlow 0x4e8d30) */
enum { MCOP_LOAD = 0x10, MCOP_BOOT = 0x20, MCOP_DELETE = 0x30, MCOP_SAVE_CHECK = 0x40,
       MCOP_SAVE_CUR = 0x50, MCOP_CREATE = 0x60 };
#define MCF_CONFIRM   0x200
#define MCF_CANCEL_OK 0x1000
#define MCF_FORCE     0x4000
#define MCF_ASKED     0x200000
/* Profile Manager (Setup 0x4c58b0: 0x51f670(icon, label), added Create,
   Load, Save, Delete); group: its EnterMemcard flags */
static const ShopEntry k_profile_menu[] = {
    { 0x55423473u, 0x56026748u, 0x861 },    /* Create */
    { 0x0018ecffu, 0x2287e063u, 0xc11 },    /* Load */
    { 0x001c8aceu, 0x228b7e32u, 0x4a51 },   /* Save: locked while unnamed (0x4a9360) */
    { 0x56b00632u, 0xb9929709u, 0x831 },    /* Delete */
};
#define STR_PROFILE_MANAGER 0xbcb18f38u   /* SAVELOAD_PROFILE_MANAGER */
/* UI_StartCareer (GC Setup 800d1f7c: IconOption(icon, text), shown here as
   {text, icon}); group: the option's class */
enum { CS_RESUME, CS_NEW, CS_LOAD };
/* uiCareerCrib::Setup PC 0x4af820 (GC 800d26ac): IconOption(icon, text) in
   this order; the world map only past stage 1. The PC's sixth option is the
   reward options (vtable 0x790d24, React 0x4af6e0), where the GC has
   CCViewSponsors; the sponsors are inside it on the PC */
enum { CR_PLAY, CR_WORLD_MAP, CR_CAR_SELECT, CR_PARTS, CR_TEST_TRACK, CR_REWARDS, CR_SAVE, CR_MAIN };
static const ShopEntry k_crib[] = {
    { 0x194efa7au, 0xa230cd88u, CR_PLAY },        /* CCEnterGameplay */
    { 0x9c07ca52u, 0x22f2ceabu, CR_WORLD_MAP },   /* CCViewWorldMap -> UI_CareerWorldMap */
    { 0x451879e2u, 0xa964095bu, CR_CAR_SELECT },  /* CCarSel -> UI_CareerCarSelect */
    { 0xabb24bbcu, 0x511f9d75u, CR_PARTS },       /* CCChangeParts -> BeginCarCustomize(1) */
    { 0x8513f2c0u, 0x7a9b397au, CR_TEST_TRACK },  /* CCTestTrack -> BeginCarTuning */
    { 0xce62c0bfu, 0x49d2e3deu, CR_REWARDS },     /* -> UI_CribRewardOptionsMain (no lock) */
    { 0x03ec7395u, 0x8ada6fd4u, CR_SAVE },        /* CCSaveMemcard: EnterMemcard 0xc41 */
    { 0x046e7d84u, 0xfc39853fu, CR_MAIN },        /* CCBackToMain */
};
static const ShopEntry k_start_career[] = {
    { 0x97b1dcdau, 0x3cb92e26u, CS_RESUME },   /* named, not started: the New icon/text acting as CSResume */
    { 0xe072db21u, 0xa230cd88u, CS_RESUME },   /* CSResume */
    { 0x97b1dcdau, 0x3cb92e26u, CS_NEW },      /* CSNew "Start New Career" */
    { 0xaa3f09d8u, 0x72c87f3cu, CS_LOAD },     /* CSLoad "Load Game" */
};
#define STR_CAREER 0x5e7e1192u   /* the title FEPrintf(0x42adb44c, "Career") */
#define STR_NO_PROFILE      0xcf4f80c2u   /* STRING_NO_PROFILE "No Profile" */

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
#define O_QR_CUSTOMIZE   0xa936c3a2u   /* help bar "Customize" (Square: the Car Shop, entry 2) */
#define O_QR_CUST_BTN    0x89782734u   /* its button icon */
#define O_QR_MODE_BOX    0x2043aba0u   /* the race mode box's group */
#define STR_QR_CUST_TITLE 0x61032ce5u  /* QR_TITLE_CUSTOMIZE "Customize Car Select" */
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
    int  ent, lvl;          /* career: MapCarPartToUnlockable 8012e8bc, the record's level (+5 >> 5) */
    uint32_t okey;          /* career: the inventory key, the part's name hash (0 = never owned) */
} ShopPart;
#define SHOP_MAX_PARTS 200

enum { SC_CAR_SHOP, SC_BODY, SC_PERFORMANCE, SC_GRAPHICS, SC_SPECIALTIES, SC_PARTS,
       SC_RIM_BRAND, SC_RIMS, SC_SPLASH, SC_MAIN, SC_LOADING,
       SC_QR_MODE, SC_QR_TRACK, SC_QR_OPTIONS, SC_QR_CAR, SC_OPT_MAIN, SC_OPT, SC_CTL, SC_JUKE, SC_CREDITS, SC_PAINT, SC_PERF_PKG,
       SC_VLAYER, SC_VCAT, SC_VLIST, SC_DLOC, SC_DPICK, SC_TINT, SC_BULB, SC_CARBON,
       SC_NEON, SC_NEON_LIST, SC_HUD, SC_HUD_COL, SC_SPEC, SC_ICE, SC_PROFILE, SC_START_CAREER, SC_MOVIE, SC_CAREER_LOT, SC_CRIB, SC_CRIB_CARSEL, SC_CRIB_SPONSORS, SC_UNIQUES, SC_CRIB_WMAP, SC_MAG_REWARD, SC_CRIB_REWARDS, SC_MAG_SELECT, SC_MAG_VIEW };

/* one settings record (0x83aac4 + type * 20, ctor 0x52a220) */
typedef struct { int opp, diff, traffic, catchup, ev, laps, dir, ko; } QrSet;
/* An image the code changes: its texture, place, size or alpha, or hidden. */
typedef struct { uint32_t hash; GLuint tex; int hide, geom; float x, y, w, h, alpha; uint32_t col; /* 0xAARRGGBB, 0 authored */
                 int uvo; float uv[4];   /* the code's texture through this uv window */ } ObjOv;

#define QR_NTYPE 11
#define QR_MAX_TRACKS 64
#define QR_MAX_CARS 64   /* the stock cars + 20 tuned slots (F2.4) */

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
    /* the career's CarCustomizeManager: the unlock filter (GetPartUnlockFilter
       8017107c: 0 = outside the career, the port's unlock-all; 2 the crib's
       Change Parts, owned parts only; 6 a shop, owned or unlocked), the entry
       point (0x803d84e0: 1 crib, 0 a shop) and the shop (0x803d84e4); each
       entity's UnlockablePartData levels (GCareer 0x34A1C: +4 the rule,
       +8 its argument) and the player / cash swap (SetAlternatingPlayerCash) */
    int career, cust_entry, cust_shop, tt_req;
    unsigned char unl_type[96][3]; uint32_t unl_arg[96][3];
    float alt_t;
    /* SetAlternatingText 80151df0 / AlternateTheText 80151ed8: per MenuScreen
       (restarted when the screen changes) the current text (1 the name,
       2 the cash, 0 off), its countdown in 1/4000 s, the switch count and ms
       since the switch; alt_anim runs FADE_IN / FADE_OUT on the two objects
       of the package that shows them */
    int alt_cur, alt_timer, alt_screen, alt_gen, alt_bgen, alt_ms, alt_bound, alt_scripts;
    FengAnim *alt_anim;
    int buy_after;                /* career_gate: the install after the buy */
    const unsigned char *cshop;   /* the engaged shop (PlayerCareerState +0x7df8): CC_MEGALOW's prices */
    const unsigned char *fe_shop; /* the shop the front end is in (GetAmbientZone's 0x86344c), NULL none */
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
    uint32_t rim_skey[SHOP_MAX_PARTS][8];    /* each size's own CarPart: its name hash (the owned key) */
    unsigned char rim_slvl[SHOP_MAX_PARTS][8];   /* and its level (+5 >> 5) */
    int size_mode, size_sel; float size_scroll;
    int rim_from_parts;   /* the rim brands opened from the parts browser's Rims category */
    /* front end */
    Scroller main; int front_end, fe_action; float blink;
    char ls_line1[96], ls_line2[96], ls_line3[96];   /* PC_Loading text in place of "LOADING" (shop_loading_text) */
    int ls_ctl;                        /* SC_LOADING shows Loading_Controller (shop_loading_pick) */
    GLuint ls_pic; unsigned char ls_sig[64]; uint32_t ls_lab[14];   /* its Vita picture, what it was drawn for, the slots' labels */
    float text_dx, text_dy;            /* offset applied by text() (second loading line) */
    /* quick race */
    const unsigned char *trk; int ntrk;          /* GLOBALB 0x34201 records */
    QrSet set[QR_NTYPE];
    int qr_type;                                 /* the mode picked */
    Scroller qr_mode, qr_track;
    ShopEntry qr_track_e[QR_MAX_TRACKS]; short qr_track_rec[QR_MAX_TRACKS]; unsigned char qr_track_lock[QR_MAX_TRACKS];
    int qr_row[QR_ROWS_SHOWN], qr_nrow, qr_rsel; /* option rows (QO_*) and the cursor */
    FengAnim *qr_anim; int qr_anim_ms;           /* UI_QRModeOptions with its scripts */
    float map_wait; int map_ev, map_want; GLuint map_tex; float map_alpha;
    ShopCar cars[QR_MAX_CARS]; int ncars;
    short qr_car_list[QR_MAX_CARS]; int qr_ncar, qr_car_sel, qr_filter;
    char qr_car_cur[32];       /* the car select's car: "<folder>", "<folder>#<slot>" a tuned one (F2.4) */
    int qr_del;                /* F2.4: a tuned slot + 1 deleted, for shop_poll */
    int qr_car_pending; float qr_car_debounce;
    int qr_go;
    int qr_cust, cust_go;                        /* Customize Car Select (game mode 0x20), its accept */
    int cust_qr;                                 /* cust_go from the Quick Race car select's Square (entry 2) */
    ObjOv ov[24]; int nov;                       /* draw_package's image overrides */
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
    int spk_screen;                              /* the last screen told to the FE speech (+1) */
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
    /* SC_UNIQUES (UniquesBrowser): the world challenge's unique parts
       (GetUniquesPartsList 8012d688) -- a car slot's part, or a junkman path */
    Scroller uqs; ShopEntry uq_e[64];
    struct { int slot, value, path, ent; uint32_t hash, okey; char folder[16], name[64]; } uq[64];
    int uq_perf, uq_done;
    int spec_slot;             /* SC_SPEC: the Specialties slot being browsed */
    int spec_open;             /* SC_SPEC: Square opened the hood / doors (0x568d30 0xc519bfc0) */
    int spec_ret;              /* SC_SPEC: where Back / Install return (the Specialties or SC_ICE) */
    Scroller icem; ShopEntry ice_e[12]; int ice_mode;   /* SC_ICE: UI_ICEMAIN's strip, mode 3 / 4 */
    Scroller neonm;            /* Car Specialties -> Neon's strip */
    int perf_cat;                                /* DAT_00838994 */
    int perf_shown;                              /* the contents shown (PackageFadeIn) */
    uint32_t perf_row_name[4], perf_row_price[4]; int perf_nrow;
    char perf_row_cash[4][16], perf_cost_txt[3][48];   /* career: the rows' prices, the discount label / amount, the cost */
#define BUY_MAX 12
    FengAnim *buy_anim;                          /* UI_BuyPerformanceParts */
    int buy_idx[BUY_MAX]; uint32_t buy_name[BUY_MAX];
    int buy_n, buy_sel, buy_top, buy_focus;
    char buy_txt[2][32];                         /* the torque / power gains */
    /* ChooseBrandsScreen (UI_PerformanceBrandSelect) over the package
       screen: a bought package's parts (or the one part) and their brands */
    FengAnim *brand_anim;
    int brand_on, brand_pkg, brand_part, brand_n, brand_sel;
    struct { int part, nb, cur, arrows; uint32_t name, b[8]; } brand_w[4];
    /* credits */
    FengAnim *cred_anim;
    char **cred_line; int cred_n, cred_cur;      /* the text's lines (wrapped), the last one rolled */
    int cred_ms, cred_loaded;                    /* time on the screen */
    struct { int line, t; } cred_roll[48]; int cred_nroll;
    /* a GenericDialog over the screen (0x558020): its package (-1 none),
       one or two buttons, the cursor, what its first button does */
    FengAnim *dlg_anim;
    int dlg_pk, dlg_nbtn, dlg_sel, dlg_act, dlg_act2;   /* act2: the second button's, when it does something */
    int dlg_back;                                /* the cancel message's, when it does something */
    uint32_t dlg_b1, dlg_b2;
    const char *dlg_l1, *dlg_l2;                 /* plain button labels instead of dlg_b1 / dlg_b2 (the World Expansion choice) */
    char dlg_text[1024];
    /* options: the save settings when the category opened (0x4b6010, 0x49c690) */
    unsigned char opt_save0, opt_auto0;
    /* profiles: UI_ProfileManager's strip; the memcard flow over the screen
       (MC_Main 0x4fd570, DoSaveFlow 0x4e8d30): its operation and flags, what
       shows (MCV_*), the question up (MCD_*), its buttons and the cursor,
       the screen it returns to; the list (MC_List) and the name entry */
    Scroller prof;
    int mc_op, mc_flags, mc_view, mc_dlg, mc_nbtn, mc_sel, mc_ret, mc_changed, mc_backup, mc_then;
    uint32_t mc_b[2];
    char mc_text[512];
    FengAnim *mc_anim, *mcl_anim, *kb_anim;
    char mc_names[PROFILE_LIST_MAX][PROFILE_NAME_MAX + 1];
    int mc_n, mc_lsel, mc_ltop;
    char kb_text[PROFILE_NAME_MAX + 1];
    int kb_sel, kb_fresh, kb_ms;   /* focus (-1 none yet), "NAME" untouched, time open */
    /* career: UI_StartCareer's options, the full-screen movie (IG_PlayMovie)
       and StartCareerFreeRoam for main.c (1 + the start NIS) */
    Scroller scar;
    Scroller crib; ShopEntry crib_e[8];
    int platform;             /* PlatformNameMap index, 0 = the default (SHOWROOM) */
    int crib_from_main;       /* uiCareerCrib::bCameFromCareerMain: BACK asks for the main menu, not gameplay */
    int crib_play;            /* the memcard save is CheckMemory's: gameplay after it */
    /* uiCribReward_Sponsors: the stages' chosen sponsors, the current one,
       its contract terms broken into lines and the CTextScroller's top line */
    const unsigned char *csp[8]; int csp_n, csp_sel, csp_top, csp_nl;
    /* UIMagazineReward: the cover art (back, mast, chick, extra), the car
       setup's anim flags and its garage camera view */
    GLuint mag_tex[4]; int mag_flags, mag_cam_ok; float mag_cam[12];
    uint32_t mag_rig; char mag_env[16];
    const unsigned char *mag_sc; int mag_shot, mag_leave;               /* the showcase on the cover, frames to its photo */
    Scroller crw; ShopEntry crw_e[3]; int crw_seen;          /* uiCribRewardOptionsMain */
    Scroller msel; ShopEntry msel_e[32]; int msel_mode, msel_seen;   /* UIMagazineSelect: 1 magazines, 2 DVDs */
    GLuint mview_tex; int mview_idx;                         /* UIMagazineView: the photo, its showcase */
    Scroller cwm;                                /* CribWorldMapScreen: the ThumbnailScroller (+0x138) */
    const unsigned char *cwm_ev[72];             /* its nodes' events (NULL: the region item) */
    int cwm_stage, cwm_nst;                      /* +0x224 the region; the completed stages */
    float map_t;                                 /* TrackMapScroller: time since the map came in (the zoom) */
    const unsigned char *fe_event;               /* SHOP_ACT_CAREER_EVENT */
    char csp_ln[48][160];
    ShopEntry scar_e[3];
    struct MPlay *mv;
    int career_go;
    /* the career Car Lot (UICareerCarLot): its state (+0x50), the unlocked /
       empty slot counts taken at Setup (+0x7c / +0x78), the list (+0x54: car
       type and FECarRecord handle; filter 1 stock, 4 career), the current
       entry (+0x5c), the stock car remembered for the browse (+0x68), the
       scroll bar's index (+0x88, 1-based, not reset by a rebuild) and what
       RefreshHeader last put up (logos, sliders, scroll bar) */
    int lot_state, lot_unlocked, lot_empty;
    struct { int type; uint32_t handle; int kind; } lot[QR_MAX_CARS];   /* kind: the crib's 0 car, 1 open, 2 locked */
    int lot_n, lot_sel, lot_bar;
    uint32_t lot_new;
    struct { int type, n, idx, first, last, kind; } lot_hdr;
};

static void make_program(Shop *s);
static void credits_free(Shop *s);
static void credits_enter(Shop *s);
static void credits_tick(Shop *s, int ms);
static void movie_stop(Shop *s);

/* -------------------------------------------------------------- strings */

static void load_strings(Shop *s) {
    char path[600];
    n2_lang_path(path, sizeof path, s->dataroot);
    s->lang = n2_read_file(path, &s->langlen);
    printf("shop: language %d %s (%s)\n", n2_lang(), k_n2_lang_name[n2_lang()], s->lang ? "loaded" : "MISSING");
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
                            s->unl_type[idx][l] = c[r + 4 + 12 * l + 4];
                            s->unl_arg[idx][l] = n2_u32(c + r + 4 + 12 * l + 8);
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

/* 0x610020: the models that honour EXCLUDE_UG1 (MIATA GOLF RSX RWX 350Z
   240SX CIVIC FOCUS PEUGOT SUPRA TIBURON CELICA ECLIPSE SENTRA SKYLINE) */
static int rr_model_ug1(uint32_t m) {
    static const uint32_t t[] = { 0x3455d6bu, 0x163087u, 0xdb7cu, 0xdc00u, 0xac6d1u, 0x150fb80u, 0x290c04du, 0x2ca04dfu,
                                  0x72b2e513u, 0x3b8c48au, 0x4fd0522u, 0x545aec60u, 0x7502da84u, 0x79aebfecu, 0xbe48375eu };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) if (t[i] == m) return 1;
    return 0;
}
/* 0x6100a0: the models that honour EXCLUDE_UG2 (IS300 GTO A3 TT G35 RWY
   CORSA PEUGOT106 LANCEREVO8 3000GT MUSTANGGT IMPREZAWRX COROLLA) */
static int rr_model_ug2(uint32_t m) {
    static const uint32_t t[] = { 0x30236eeu, 0xacc9u, 0x453u, 0x6e7u, 0xa86eu, 0xdc01u, 0x293fad7u,
                                  0x4b10070au, 0x9d2eff6u, 0x2d7c9b5du, 0x35165819u, 0xcf4458f8u, 0xf77c13ebu };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) if (t[i] == m) return 1;
    return 0;
}
/* the value of attribute `key` (0 when absent), as GetCarPartDirect reads it */
static uint32_t db_attr_u(const Shop *s, unsigned aoff, uint32_t key) {
    if (aoff == 0xffffu || !s->db_off || !s->db_attr) return 0;
    const unsigned char *p = s->db + s->db_off + aoff * 2L;
    unsigned n = p[0] | p[1] << 8;
    for (unsigned k = 0; k < n && k < 64; k++) {
        unsigned ai = p[2 + 2 * k] | p[3 + 2 * k] << 8;
        if (n2_u32(s->db + s->db_attr + ai * 8L) == key) return n2_u32(s->db + s->db_attr + ai * 8L + 4);
    }
    return 0;
}
/* GetCarPartDirect 0x61b670's exclusion of a part record, by its model:
   EXCLUDE_UG1 for 0x610020's models, EXCLUDE_UG2 for 0x6100a0's,
   EXCLUDE_SUV for the SUVs' own models (NAVIGATOR, HUMMER, ESCALADE); the
   generic libraries are never excluded. Every Customize list goes through
   it (GetPartsList 8015e1d4 / the browsers: NewGetFirstCarPart -> NewGetCarPart). */
static int part_excluded(const Shop *s, const unsigned char *r) {
    if (r[7] >= s->nmodels) return 0;
    uint32_t m = n2_u32(s->db + s->db_models + 4L * r[7]);
    unsigned aoff = r[10] | r[11] << 8;
    if (rr_model_ug1(m) && db_attr_u(s, aoff, 0xF7934315u)) return 1;   /* EXCLUDE_UG1 */
    if (rr_model_ug2(m) && db_attr_u(s, aoff, 0xF7934316u)) return 1;   /* EXCLUDE_UG2 */
    if ((m == 0x0219c94au || m == 0x6126dd4du || m == 0xcf704771u) && db_attr_u(s, aoff, 0xF7933C86u)) return 1;   /* EXCLUDE_SUV */
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

/* part_value of a database part: a library's "none" entry (e.g. NO ROOF
   SCOOP; lib = the part's model is a library, not the car) is the stock look */
static int part_value_lib(const char *car, int group, uint32_t hash, int lib, int upg) {
    int v = part_value(car, group, hash);
    if (v < 0 && lib && (upg & 0xff) == 0) v = 0;
    return v;
}
/* a Body Shop part (value) into a configuration's slot, as the parts
   browser fits it: a carbon hood (from the Carbon Fiber lists, the parts
   with CARBONFIBRE) is the same hood, carbon */
static void body_put(N2CarConfig *c, int slot, int value, int carbon) {
    c->parts[slot] = value;
    if (slot == N2_PART_HOOD) c->carbon = carbon ? c->carbon | 1u : c->carbon & ~1u;
}
/* InstallPart 8016e4bc: a CARBONFIBRE hood (0x721aff7c) takes the "Hoods"
   vinyls off (UninstallHoodVinyls 8016ea7c: a layer 0x40..0x43 whose vinyl
   group is 0x14); a hood above upgrade level 0 takes the hood's decals off
   (UninstallHoodDecals 8016eaec: slots 0x61..0x68) */
static void hood_installed(N2CarConfig *c, int level) {
    if (c->carbon & 1u)
        for (int l = 0; l < 4; l++) {
            const VinylPart *v = c->vinyl[l] ? vinyl_find(c->vinyl[l]) : NULL;
            if (!v || v->group != 0x14) continue;
            printf("shop: carbon hood fitted -- hood vinyl on layer %d off\n", l + 1);
            c->vinyl[l] = 0; memset(c->vinyl_col[l], 0, sizeof c->vinyl_col[l]);
        }
    if (level > 0) {
        for (int n = 0; n < 8; n++)
            if (c->decal[0][n]) { printf("shop: hood level %d fitted -- hood decals off\n", level); break; }
        memset(c->decal[0], 0, sizeof c->decal[0]);
    }
}

static int menu_has(const N2PartMenu *m, int value) {
    for (int i = 0; i < m->count; i++) if (m->options[i].value == value) return 1;
    return 0;
}

/* ---- the career's UnlockSystem (CAREER.md "C6: shops") ---------------- */
/* CalculateIfUnlocked 8012dfb0 (filter 4): a level's UnlockablePartData rule
   -- 1 the shop (handle) is enterable, 2 / 3 / 4 the stage (arg >> 16) has
   that many world / URL / sponsor races won; level 0 is always open, 4 never */
static int calc_unlocked(const Shop *s, int ent, int lvl) {
    if (lvl == 4) return 0;
    if (lvl == 0 || lvl > 3 || ent <= 0 || ent >= 96) return 1;
    int t = s->unl_type[ent][lvl - 1];
    uint32_t a = s->unl_arg[ent][lvl - 1];
    int st = (int)((int32_t)a >> 16), need = (int)(a & 0xffffu);
    const CareerStage *cs = st >= 0 && st < 8 ? &g_career.st[st] : NULL;
    switch (t) {
    case 1: { const unsigned char *sh = career_shop_by_handle(a); return sh && career_shop_enterable(sh); }
    case 2: return cs && need <= cstage_world_done(cs);
    case 3: return cs && need <= cstage_url_done(cs);
    case 4: return cs && need <= cstage_sponsor_done(cs);
    default: return 1;
    }
}
/* UnlockSystem::IsCarPartUnlocked 8012fc3c -> CareerUnlocker 8012f530 with the
   shop's filter: 2 = level 0 or owned, 4 = CalculateIfUnlocked */
/* IsUnlockedByEnteringShop 8012df58: the level's rule is 1 (a shop) */
static int unl_by_shop(const Shop *s, int ent, int lvl) {
    return lvl >= 1 && lvl <= 3 && ent > 0 && ent < 0x57 && s->unl_type[ent][lvl - 1] == 1;
}
/* CareerUnlocker filter 4: entity 0x3d (manufacturer vinyls) asks only the
   IsManufacturerVinylUnlocked easter egg (never on), not CalculateIfUnlocked */
static int calc_part(const Shop *s, int ent, int lvl) { return ent != 0x3d && calc_unlocked(s, ent, lvl); }
static int part_open(const Shop *s, int ent, int lvl, uint32_t key) {
    if (!s->career)   /* outside the career: QuickRaceUnlocker::IsCarPartUnlocked 8012f088 (filter 1) */
        return lvl == 0 || calc_part(s, ent, lvl) ||
               (career_completed_career() && (lvl == 4 || unl_by_shop(s, ent, lvl)));
    int ok = 0;
    if (s->career & 2) ok = lvl == 0 || career_owns_part(ent, key);
    if (s->career & 4) ok = ok || calc_part(s, ent, lvl);
    return ok;
}
/* GetWordToSetBitFor 80135610 / OwnsCarPart 801358bc: every part is owned
   by its own bit (regions or bit idx of the entity's word), keyed here by
   the part's name hash; the CarSlots 0x3f, 0x44..0x47, 0x49 are never owned
   (0: charged, nothing recorded) */
static uint32_t own_key(int slot, uint32_t hash) {
    if (slot == 0x3f || (slot >= 0x44 && slot <= 0x47) || slot == 0x49) return 0;
    return hash ? hash : 1u;
}
/* InitUnlockablesMap 8012e544: MapCarSlotToUnlockable, a CarSlot's
   unlockable entity (MapCarPartToUnlockable 8012e8bc's default) */
static const unsigned char k_slot_ent[0xaa] = {
    0x00, 0x12, 0x11, 0x13, 0x13, 0x00, 0x16, 0x14, 0x00, 0x0b, 0x00, 0x10, 0x0c, 0x00, 0x0f, 0x0e,
    0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x0d, 0x21,
    0x13, 0x00, 0x1e, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x28, 0x50, 0x4f, 0x4e, 0x52, 0x4d, 0x51, 0x53, 0x54, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x00, 0x00,
    0x56, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c,
    0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c,
    0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c,
    0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x20, 0x19, 0x1a, 0x1b, 0x1c, 0x1b, 0x18,
    0x23, 0x24, 0x1d, 0x17, 0x17, 0x17, 0x17, 0x00, 0x00, 0x00,
};
static int slot_ent(int slot) { return slot >= 0 && slot < (int)sizeof k_slot_ent ? k_slot_ent[slot] : 0; }
/* a list part's entity, level, owned key and GetCarPartCost 801294f4 /
   0x514280 price: level 4 free, level 0 free except a decal (entity 0x4c),
   which costs the level-1 price */
static int ent_price(const Shop *s, int ent, int lvl) {
    if (lvl == 0 && ent == 0x4c) lvl = 1;
    int c = lvl >= 1 && lvl <= 3 && ent > 0 && ent < 96 ? s->price[ent][lvl - 1] : 0;
    return c < 0 ? 0 : c;
}
static void part_cost(const Shop *s, ShopPart *q, int ent, int lvl, uint32_t okey) {
    q->ent = ent; q->lvl = lvl; q->okey = okey;
    q->price = ent_price(s, ent, lvl);
}
/* the part costs money: in the career, not bought and not level 0
   (PartSelectionScreen 0x9f00f07 vs 0x51123592) */
static int part_to_buy(const Shop *s, const ShopPart *q) {
    return s->career && q && q->lvl != 0 && !career_owns_part(q->ent, q->okey);
}

/* The part list for one Body Shop group, in database order: the car's own
   parts, then the shared libraries. GetPartsList 8015e1d4 walks
   NewGetFirstCarPart / Next, so GetCarPartDirect's per-model exclusions
   apply (part_excluded: a UG1-list car drops its EXCLUDE_UG1 kits, a
   UG2-list car its EXCLUDE_UG2 kits and lights); parts we cannot fit stay
   listed but locked. */
static void build_parts(Shop *s, int group) {
    s->nparts = 0; s->part_sel = 0; s->part_group = group;
    int slot = group_slot(group), row = group_price_row(group);
    if (!s->db || !s->db_parts) return;
    static const char *const libs[] = { NULL, "SPOILER", "SPOILER_HATCH", "SPOILER_SUV", "EXHAUST", "ROOF",
                                        "MIRRORS_BODY", "MIRRORS_POST", "MIRRORS_SUV", "MIRRORS_HUMMER" };
    int models[6], nm = 0, hidden = 0;
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
            if (part_excluded(s, r)) continue;      /* EXCLUDE_UG1 / UG2 / SUV by model (0x61b670) */
            if (db_has_attr(s, aoff, 0x721AFF7Cu) != (s->cf_mode != 0)) continue;   /* CARBONFIBRE: the Carbon Fiber menu */
            if (group == G_HOOD && (upg & 0x1f) == 5) continue;   /* split hoods: Car Specialties (0x539960) */
            if (s->nparts >= SHOP_MAX_PARTS) break;
            ShopPart *q = &s->parts[s->nparts];
            const char *nmstr = s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?";
            snprintf(q->name, sizeof q->name, "%s", nmstr);
            q->value = part_value_lib(s->car, group, hash, mi > 0, upg);
            int lvl = (upg >> 5) & 7;
            q->level = lvl > 3 ? 3 : lvl;
            /* MapCarPartToUnlockable: the carbon hood / roof / spoiler / mirrors
               are their own entities; GetUnlockablePartCost 8012e228 */
            int ent = row;
            if (db_has_attr(s, aoff, 0x721AFF7Cu))
                ent = group == G_HOOD ? 0x4b : group == G_ROOF ? 0x46 : group == G_SPOILER ? 0x4a :
                      group == G_LEFT_MIRROR ? 0x45 : row;
            q->ent = ent; q->lvl = lvl; q->okey = own_key(slot, hash);
            q->price = q->level > 0 && ent > 0 ? s->price[ent][q->level - 1] : 0;
            if (q->price < 0) q->price = 0;
            if (!part_open(s, ent, lvl, q->okey)) continue;   /* GetPartsList 8015e1d4: IsPartAvailable */
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

/* A rim part's model: LoadedWheel 80247cec (0x633650) takes the wheel's
   models from the part (GetModelNameHash) and reads its attribute 0x10c98090
   (GetAppliedAttributeUParam 0x4951f0), which in the data is the hash of
   "<MODEL>_STYLEnn" -- the folder's own name for most brands, but the NFSU
   set ships as SPARCO_STYLE01, STARK_STYLE01, ... (NFSU_STYLE03 = SPARCO).
   Every WHEELS part (group 28) of the database, by name hash, once. */
typedef struct { uint32_t hash, base; } RimBase;
static int rim_base_cmp(const void *a, const void *b) {
    uint32_t x = ((const RimBase *)a)->hash, y = ((const RimBase *)b)->hash;
    return x < y ? -1 : x > y;
}
uint32_t shop_rim_base(const char *dataroot, const char *folder, int value) {
    static RimBase *tab; static int n, tried;
    if (!tried) {
        tried = 1;
        char path[600]; uint32_t sz = 0;
        snprintf(path, sizeof path, "%s/GLOBAL/GLOBALB.BUN", dataroot);
        FILE *f = fopen(path, "rb");
        long at = f ? find_chunk_in_file(f, 0x80034602u, &sz) : -1;
        unsigned char *db = at >= 0 && sz < (8u << 20) ? (unsigned char *)malloc(sz) : NULL;
        if (db && (fseek(f, at, SEEK_SET) || fread(db, 1, sz, f) != sz)) { free(db); db = NULL; }
        if (f) fclose(f);
        long po = -1, pl = 0, ao = -1, oo = -1;
        for (long o = 0; db && o + 8 <= (long)sz; ) {
            uint32_t id = n2_u32(db + o), cs = n2_u32(db + o + 4);
            if (o + 8 + (long)cs > (long)sz) break;
            if (id == 0x00034604u) { po = o + 8; pl = cs; }
            else if (id == 0x00034605u) ao = o + 8;
            else if (id == 0x0003460Cu) oo = o + 8;
            o += 8 + cs;
        }
        if (po >= 0 && ao >= 0 && oo >= 0) tab = (RimBase *)malloc((size_t)(pl / 14 + 1) * sizeof *tab);
        for (long p = po; tab && p + 14 <= po + pl; p += 14) {
            const unsigned char *rec = db + p;
            unsigned aoff = rec[10] | rec[11] << 8;
            if (rec[4] != G_WHEEL || aoff == 0xffffu) continue;
            const unsigned char *o = db + oo + aoff * 2L;
            unsigned na = o[0] | o[1] << 8;
            for (unsigned k = 0; k < na && k < 64; k++) {
                unsigned ai = o[2 + 2 * k] | o[3 + 2 * k] << 8;
                if (n2_u32(db + ao + ai * 8L) == 0x10c98090u) {
                    tab[n].hash = n2_u32(rec); tab[n++].base = n2_u32(db + ao + ai * 8L + 4);
                }
            }
        }
        free(db);
        if (tab) qsort(tab, n, sizeof *tab, rim_base_cmp);
        printf("shop: %d rim model names\n", n);
    }
    if (!tab || !folder || !folder[0]) return 0;
    char pre[48];
    snprintf(pre, sizeof pre, "%s_STYLE%02d_", folder, RIM_STYLE(value));
    uint32_t h0 = hash_more(0xffffffffu, pre);
    for (int a = RIM_IN_LO; a <= RIM_IN_HI; a++) {   /* every size of a style shares the model */
        if (RIM_INNER(value) && a != RIM_INNER(value)) continue;
        char t[16]; snprintf(t, sizeof t, "%d_", a);
        uint32_t h1 = hash_more(h0, t);
        for (int b = RIM_OUT_LO; b <= RIM_OUT_HI; b++) {
            char u[8]; snprintf(u, sizeof u, "%d", b);
            RimBase key = { hash_more(h1, u), 0 };
            const RimBase *r = (const RimBase *)bsearch(&key, tab, n, sizeof *tab, rim_base_cmp);
            if (r) return r->base;
        }
    }
    return 0;
}

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
        q->ent = 13; q->lvl = 0; q->okey = 1u;
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
        if (brand != r->key || part_excluded(s, rec)) continue;
        int style, inner, outer;
        if (!rim_part_parse(r->folder, hash, &style, &inner, &outer)) continue;
        if (rim_outer && outer != rim_outer) continue;
        int dup = -1;
        for (int k = 0; k < s->nparts; k++) if (s->parts[k].value == style) dup = k;
        if (dup >= 0) {
            int z = s->rim_nsizes[dup];
            if (z < 8) {
                s->rim_sizes[dup][z] = (unsigned char)inner; s->rim_skey[dup][z] = hash;
                s->rim_slvl[dup][z] = (unsigned char)((upg >> 5) & 7); s->rim_nsizes[dup]++;
            }
            continue;
        }
        if (s->nparts >= SHOP_MAX_PARTS) continue;
        s->rim_sizes[s->nparts][0] = (unsigned char)inner; s->rim_nsizes[s->nparts] = 1;
        s->rim_skey[s->nparts][0] = hash; s->rim_slvl[s->nparts][0] = (unsigned char)((upg >> 5) & 7);
        ShopPart *q = &s->parts[s->nparts++];
        snprintf(q->name, sizeof q->name, "%s", s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?");
        q->value = style;
        int lvl = (upg >> 5) & 7; q->level = lvl > 3 ? 3 : lvl;
        part_cost(s, q, 13, lvl, hash);
        q->available = 1;
    }
    /* IsRimAvailable 8015e9a0 is per part: brand attr 0xebb03e66 == the brand,
       outer size attr 0xce7d8db5 == the car type's +0xdc, and
       IsPartAvailable(slot 0x1d -> entity 13); or level (+5 >> 5) 0 of the
       STOCK brand 0x3b83203. GetPartsList 8015e428 puts every such part in
       the list, so a style has a row as soon as one of its sizes passes
       (CONFIRMED G5 11-01). The style row prices / owns the fitted size when
       it is this style, else the first size (rim_preview's) */
    int kept = 0;
    for (int k = 0; k < s->nparts; k++) {
        int open = 0;
        for (int z = 0; z < s->rim_nsizes[k] && !open; z++) open = part_open(s, 13, s->rim_slvl[k][z], s->rim_skey[k][z]);
        if (!open) continue;
        if (kept != k) {
            s->parts[kept] = s->parts[k]; s->rim_nsizes[kept] = s->rim_nsizes[k];
            memcpy(s->rim_sizes[kept], s->rim_sizes[k], sizeof s->rim_sizes[k]);
            memcpy(s->rim_skey[kept], s->rim_skey[k], sizeof s->rim_skey[k]);
            memcpy(s->rim_slvl[kept], s->rim_slvl[k], sizeof s->rim_slvl[k]);
        }
        int z0 = 0;
        if (!strcmp(s->rim_folder, r->folder) && RIM_STYLE(s->rim_style) == s->parts[kept].value)
            for (int z = 0; z < s->rim_nsizes[kept]; z++) if (s->rim_sizes[kept][z] == RIM_INNER(s->rim_style)) z0 = z;
        part_cost(s, &s->parts[kept], 13, s->rim_slvl[kept][z0], s->rim_skey[kept][z0]);
        kept++;
    }
    s->nparts = kept;
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
/* the CarPart the rim browser prices and buys (0x567a50 / 0x55fb00
   IsPartPurchased(0x1d, part)): in the size list the size under the cursor,
   on a style row the part build_rims chose (fitted size or first size) */
static const ShopPart *rim_cur(const Shop *s) {
    static ShopPart t;
    if (!s->nparts) return NULL;
    const ShopPart *q = &s->parts[s->part_sel];
    if (!s->size_mode || !s->rim_nsizes[s->part_sel]) return q;
    int z = s->size_sel;
    if (z < 0 || z >= s->rim_nsizes[s->part_sel]) return q;
    t = *q;
    part_cost(s, &t, 13, s->rim_slvl[s->part_sel][z], s->rim_skey[s->part_sel][z]);
    return &t;
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
    for (int i = 0; i < NELEM(k_profile_menu) && n < cap; i++) k[n++] = k_profile_menu[i].tex;
    for (int i = 1; i < NELEM(k_start_career) && n < cap; i++) k[n++] = k_start_career[i].tex;
    for (int i = 0; i < NELEM(k_crib) && n < cap; i++) k[n++] = k_crib[i].tex;
    if (n < cap) k[n++] = 0xda665928u;   /* the crib car select's open slot */
    static const uint32_t crw_icons[] = { 0xff293517u, 0x597df2a7u, 0x0878ffdfu };   /* reward options: magazines, DVDs, sponsors */
    for (int i = 0; i < NELEM(crw_icons) && n < cap; i++) k[n++] = crw_icons[i];
    static const uint32_t uq_icons[] = { 0x945e9bc5u, 0x74ce8c0bu,   /* UniquesBrowser: the slots' (8016ce1c) */
        0xea57d2c4u, 0x01a29ffau, 0x9701bde4u, 0xc15c94e6u, 0x06ef789cu, 0x178475e7u, 0xbaa23a28u, 0x502abeedu, 0x06e8e477u };
    for (int i = 0; i < NELEM(uq_icons) && n < cap; i++) k[n++] = uq_icons[i];
    static const char *const decal_icon[] = {   /* DECAL_ICON_<sponsor> (GLOBALB), the signed sponsors' icons */
        "ALPINE", "BILSTEIN", "BREMBO", "BRIDE", "EDELBROCK", "ENKEI", "EUROLITE", "GREDDY", "KENWOOD", "KONIG",
        "NEUSPEED", "NITROUS_EXPR", "NITROUS_EXPRESS", "NOS", "OZ_WHEELS", "PIAA", "PLASMAGLOW", "SPARCO", "STILLEN",
        "STREETGLOW", "TOYO", "YOKOHAMA" };
    for (int i = 0; i < NELEM(decal_icon) && n < cap; i++) {
        char di[40]; snprintf(di, sizeof di, "DECAL_ICON_%s", decal_icon[i]);
        k[n++] = feng_tex_key(di);
    }
    static const char *const brand_icon[] = {   /* DECAL_ICON_<brand>: the perf parts' brands (GLOBALB 0x34a14) */
        "EUROLITE", "TURBONETICS", "HPRACING", "STILLEN", "NITROUS_EXPRESS", "WILWOOD", "AROSPEED", "JACKSON", "DC",
        "ACT", "AEM", "HKS", "KVR", "NOS", "APEX", "KONI", "TOYO", "APEXI", "BORLA", "HANDR", "INJEN", "KUMHO",
        "NFSU2", "NIZMO", "VENOM", "NEUSPEED", "BILSTEIN", "YOKOHAMA", "5ZIGEN", "BREMBO", "EIBACH", "GREDDY",
        "CLUTCHMASTERS", "SKUNK2", "SPARCO" };
    for (int i = 0; i < NELEM(brand_icon) && n < cap; i++) {
        char di[40]; snprintf(di, sizeof di, "DECAL_ICON_%s", brand_icon[i]);
        k[n++] = feng_tex_key(di);
    }
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
        fwrite("OUSHOP02", 1, 8, f);
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

/* the PC converter writes the cache ahead of the first shop (converter/) */
const char *shop_cache_file(void) { return SHOP_CACHE; }
int shop_build_cache(const char *dataroot) { return build_cache(dataroot); }

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

/* The PC packages park the console help bar (the "Help" prompt
   $JOY_EVENT_FENG_HELP$ and the screen's own prompts, with the bottom box
   they sit on) below the 480-high screen, y 285..305, for its mouse bar;
   the console screens have it at y 205 (UI_Main, the Car Shop). Every top
   level group or string down there moves up to 205 with what hangs off
   it. */
static void help_bar_lift(FengPackage *pk) {
    int hi = -1;
    for (int i = 0; i < pk->nobj && hi < 0; i++)
        if (pk->obj[i].type == FENG_STRING && !strcmp(pk->obj[i].text, "$JOY_EVENT_FENG_HELP$")) hi = i;
    if (hi < 0 || pk->obj[hi].ay <= 240.0f) return;
    float dy = 205.0f - pk->obj[hi].ay;
    for (int r = 0; r < pk->nobj; r++) {
        FengObj *g = &pk->obj[r];
        if (g->parent >= 0 || g->ay <= 240.0f || g->ay >= 340.0f || g->type == FENG_IMAGE) continue;   /* not the parked icon pools */
        g->pos[1] += dy;
        float y; memcpy(&y, &g->data[8], 4); y += dy; memcpy(&g->data[8], &y, 4);
        for (int i = 0; i < pk->nobj; i++) if (pk->obj[i].root == r) pk->obj[i].ay += dy;
    }
}

/* Streamed: the packages part read whole, then one texture at a time
   through one reused buffer -- never the whole ~19 MB file in memory (the
   boot opens this beside the world load). */
static int load_cache(Shop *s) {
    FILE *f = fopen(SHOP_CACHE, "rb");
    if (!f) return 0;
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "OUSHOP02", 8)) { fclose(f); return 0; }
    uint32_t npk = n2_u32(h + 8);
    long bunlen = 0;
    for (uint32_t i = 0; i < npk; i++) {   /* the packages' extent */
        unsigned char c[8];
        if (fseek(f, 12 + bunlen, SEEK_SET) || fread(c, 1, 8, f) != 8) break;
        bunlen += 8 + (long)n2_u32(c + 4);
    }
    unsigned char *bun = (unsigned char *)malloc((size_t)(bunlen ? bunlen : 1));
    if (!bun || fseek(f, 12, SEEK_SET) || fread(bun, 1, (size_t)bunlen, f) != (size_t)bunlen) {
        free(bun); fclose(f); return 0;
    }
    for (int i = 0; i < PK_COUNT; i++) {
        s->pk[i] = (FengPackage *)malloc(sizeof *s->pk[i]);
        if (s->pk[i] && !feng_load_package(bun, bunlen, k_pk_name[i], s->pk[i])) {
            free(s->pk[i]); s->pk[i] = NULL;
        }
        if (s->pk[i]) help_bar_lift(s->pk[i]);
        LOAD_PUMP();
    }
    free(bun);
    unsigned char c[12];
    uint32_t nt = fread(c, 1, 4, f) == 4 ? n2_u32(c) : 0;
    s->tex = (ShopTex *)calloc(nt ? nt : 1, sizeof *s->tex);
    unsigned char *px = NULL; size_t cap = 0;
    for (uint32_t i = 0; s->tex && i < nt && fread(c, 1, 12, f) == 12; i++) {
        uint32_t key = n2_u32(c), wh = n2_u32(c + 4), bl = n2_u32(c + 8);
        int w = (int)(wh & 0xffff), h = (int)(wh >> 16);
        size_t n = (size_t)w * h * 4;
        if (n > cap) {
            unsigned char *np = (unsigned char *)realloc(px, n);
            if (!np) break;
            px = np; cap = n;
        }
        if (fread(px, 1, n, f) != n) break;
        ShopTex *t = &s->tex[s->ntex++];
        t->key = key; t->w = w; t->h = h; t->additive = bl == 6;
        t->tex = upload_rgba(w, h, px);
        LOAD_PUMP();   /* 348 of them through stdio: 11 s behind the boot screen on the Vita (play1116b) */
    }
    free(px);
    fclose(f);
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
       DA_HUD, DA_SPEC, DA_GFX_INSTALL, DA_GFX_LEAVE, DA_GFX_DISCARD, DA_GFX_CANCEL, DA_OPT_LEAVE,
       DA_LOT_BUY, DA_LOT_TRADE, DA_LOT_BROWSE, DA_LOT_EXIT,
       DA_CRIB_PLAY, DA_CRIB_MAIN, DA_CRIB_TEST, DA_CRIB_SWITCH,
       DA_PART_BUY, DA_RIM_BUY, DA_SHOP_LEAVE, DA_PERF_PKG_BUY, DA_PERF_PART_BUY, DA_GEN_BUY,
       DA_UQ_PICK, DA_UQ_LEAVE, DA_CWM_RACE, DA_MAG_SAVE, DA_MAG_NOSAVE, DA_TRANS_MANUAL, DA_TRANS_AUTO, DA_BRAND_BUY, DA_QR_DELETE, DA_QR_BAYVIEW, DA_QR_BEYOND };   /* the crib: 0x451e768e 0x30ed2368 0x34dc1bec */   /* the Car Lot: 0xd05fc3a3 0x526adefc 0x75d1e35e 0x51123592 */
static void dlg_act(Shop *s, int act);
static void dialog(Shop *s, int pk, int nbtn, uint32_t msg, int act, uint32_t b1, uint32_t b2);
/* PartSelectionScreen 801615f8 in the career: a part to buy -- too little
   cash 0xd1ac60be (SMALL), else "purchase this part?" 0x44b7167f whose yes
   (0x9f00f07) buys it and runs the install (after). 0: not to buy. */
static int setup_part_cost(const Shop *s, int slot, uint32_t hash);
static int gfx_setup_price(const Shop *s);
/* the paint setup's buys (ChoosePaintScreen 80177658, DoVinylsButtonPress
   80163710, ChooseDecalScreen 801685cc): in the career a part whose price
   with the setup's total is more than the wallet is "insufficient funds"
   (0xd1ac60be); nothing is charged until Purchase Setup */
static int setup_short(Shop *s, int price) {
    if (!s->career || g_career.cash >= price + gfx_setup_price(s)) return 0;
    dialog(s, PK_DLG_S, 1, 0xd1ac60beu, DA_NONE, 0, 0);
    return 1;
}
static int career_gate(Shop *s, const ShopPart *q, int pkg, int after) {
    if (!part_to_buy(s, q)) return 0;
    if (g_career.cash < q->price) dialog(s, PK_DLG_S, 1, 0xd1ac60beu, DA_NONE, 0, 0);
    else { s->buy_after = after; dialog(s, pkg, 2, 0x44b7167fu, DA_GEN_BUY, 0, 0); }
    return 1;
}
static void lot_dlg(Shop *s, int act);
static void mc_enter(Shop *s, int flags, int ret);
static void opt_leave(Shop *s);

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
static void dialog_text(Shop *s, int pk, int nbtn, const char *t, int act, uint32_t b1, uint32_t b2);
static void dialog(Shop *s, int pk, int nbtn, uint32_t msg, int act, uint32_t b1, uint32_t b2) {
    dialog_text(s, pk, nbtn, str_get(s, msg), act, b1, b2);
}
static void dialog_text(Shop *s, int pk, int nbtn, const char *t, int act, uint32_t b1, uint32_t b2) {
    if (t != s->dlg_text) snprintf(s->dlg_text, sizeof s->dlg_text, "%s", t);
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
    s->dlg_pk = pk; s->dlg_nbtn = nbtn; s->dlg_act = act; s->dlg_act2 = DA_NONE; s->dlg_back = DA_NONE;
    s->dlg_b1 = nbtn == 1 ? STR_OK : b1 ? b1 : STR_YES; s->dlg_b2 = b2 ? b2 : STR_NO;
    s->dlg_l1 = s->dlg_l2 = NULL;
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
        int act = s->dlg_act, act2 = s->dlg_act2, back = s->dlg_back;
        s->dlg_pk = -1;
        printf("shop: dialog answered %s\n", s->dlg_nbtn == 1 ? "OK" : yes ? "yes" : "no");
        if (yes) dlg_act(s, act);
        else if (key == SHOP_KEY_OK && s->dlg_nbtn == 2 && act2) dlg_act(s, act2);
        else if (key != SHOP_KEY_OK && back) dlg_act(s, back);
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
        /* hidden: the object's own bit 0 only (cFEng::RenderObject 0x51c670;
           RenderGroup 0x52c5d0 tests no group hide bit, and bit 31 never
           hides in the game: feng.c feng_anim_init). A group's alpha 0 is in c. */
        int hidden = (o->flags & 1u) != 0;
        /* PC_Loading's keyboard controls group (af4a85eb, "Steering (left)"
           .. "Esc" / "E" / "Q"): no code touches it, so the PC shows it.
           GUESS: kept hidden on the Vita -- PC_Loading is PC-only and these
           are keyboard keys the Vita does not have (GAPS "F") */
        if (p == PK_LOADING)
            for (int q = o->parent; q >= 0; q = pk->obj[q].parent) if (pk->obj[q].hash == 0xaf4a85ebu) hidden = 1;
        if (o->hash == O_PRESS_START) { c[3] = 1.0f; memcpy(s->rgba[p][i], c, sizeof c); }
        /* PC_Loading's circles are authored invisible: the script fades
           them in turn, which draw_package does in code */
        if ((p == PK_LOADING && o->hash >= 0x50d98304u && o->hash <= 0x50d98308u) ||
            (p == PK_LOADING_CTL && o->hash >= 0x50d98305u && o->hash <= 0x50d98309u)) {
            c[3] = 1.0f; memcpy(s->rgba[p][i], c, sizeof c);
        }
        if (hidden || c[3] <= 0.0f) continue;
        /* pools and templates the code clones sit far off screen; the cover
           screens' U2_magazine_texture masks are centred off it but reach in */
        if (p == PK_MAG_VIEW || p == PK_MAG_REWARD || p == PK_MAG_BACK) {
            if (fabsf(o->ax) - fabsf(o->size[0]) * 0.5f > 320.0f || fabsf(o->ay) - fabsf(o->size[1]) * 0.5f > 240.0f) continue;
        } else if (fabsf(o->ax) > 330.0f || fabsf(o->ay) > 250.0f) continue;
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
    LOAD_PUMP();
    load_strings(s);
    LOAD_PUMP();
    load_fonts(s);
    LOAD_PUMP();
    for (int p = 0; p < PK_COUNT; p++) feng_apply_labels(s->pk[p], s->str_hash, s->str_txt, s->nstr, 0);   /* FEString labels */
    for (int p = 0; p < PK_COUNT; p++) { prepare_package(s, p); LOAD_PUMP(); }
    load_db(s);
    LOAD_PUMP();
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
    scroller_set(&s->top, k_car_shop_fe, NELEM(k_car_shop_fe));   /* the front end's (entry 2); career resets it */
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
#define LS_CTL_NRES 12               /* Loading_Controller's resources (11) */
#define LS_CTL_PIC_KEY 0x42226734u   /* its PS2 pad picture: the Vita one is drawn */
static int load_cache_loading(Shop *s) {
    FILE *f = fopen(SHOP_CACHE, "rb");
    if (!f) return 0;
    unsigned char h[12];
    int ok = 0;
    unsigned char *bun = NULL;
    if (fread(h, 1, 12, f) == 12 && !memcmp(h, "OUSHOP02", 8)) {
        uint32_t npk = n2_u32(h + 8);
        long at = 12, bunlen = 0;
        for (uint32_t i = 0; i < npk; i++) {   /* the packages' extent */
            unsigned char c[8];
            if (fseek(f, at + bunlen, SEEK_SET) || fread(c, 1, 8, f) != 8) break;
            bunlen += 8 + (long)n2_u32(c + 4);
        }
        bun = (unsigned char *)malloc((size_t)(bunlen ? bunlen : 1));
        if (bun && !fseek(f, at, SEEK_SET) && fread(bun, 1, (size_t)bunlen, f) == (size_t)bunlen) {
            static const int lp[2] = { PK_LOADING, PK_LOADING_CTL };   /* the console one: caches from before F1.11 lack it */
            for (int k = 0; k < 2; k++) {
                s->pk[lp[k]] = (FengPackage *)malloc(sizeof *s->pk[lp[k]]);
                if (s->pk[lp[k]] && !feng_load_package(bun, bunlen, k_pk_name[lp[k]], s->pk[lp[k]])) {
                    free(s->pk[lp[k]]); s->pk[lp[k]] = NULL;
                }
            }
        }
        const FengPackage *pk = s->pk[PK_LOADING], *pk2 = s->pk[PK_LOADING_CTL];
        unsigned char c[12];
        if (pk && !fseek(f, at + bunlen, SEEK_SET) && fread(c, 1, 4, f) == 4) {
            uint32_t nt = n2_u32(c);
            s->tex = (ShopTex *)calloc((size_t)pk->nres + LS_CTL_NRES + 1, sizeof *s->tex);
            long o = at + bunlen + 4;
            for (uint32_t i = 0; s->tex && i < nt && !fseek(f, o, SEEK_SET) && fread(c, 1, 12, f) == 12; i++) {
                uint32_t key = n2_u32(c), wh = n2_u32(c + 4), bl = n2_u32(c + 8);
                int w = (int)(wh & 0xffff), ht = (int)(wh >> 16);
                long px = (long)w * ht * 4;
                o += 12 + px;
                int want = 0;
                for (int r = 0; r < pk->nres && !want; r++)
                    want = pk->res[r].type == 1 && feng_tex_key(pk->res[r].name) == key &&
                           !(hud_is_wide_loading_res(pk->res[r].name) && hud_wide_loading_tex());   /* drawn wide instead */
                for (int r = 0; pk2 && r < pk2->nres && !want; r++)
                    want = pk2->res[r].type == 1 && feng_tex_key(pk2->res[r].name) == key && key != LS_CTL_PIC_KEY;
                if (!want || s->ntex >= pk->nres + LS_CTL_NRES) continue;
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
/* ---------------------------------------------- F1.11 controls loading screen

   The console loading screen (GC ShowLoadingScreen 80104b30): game mode 1
   (career) picks Loading.fng or Loading_Controller.fng by bRandom(2), mode 8
   Loading.fng, every other mode Loading_Controller.fng. Its ctor 80102cbc:
   HideControllerConfig 80102fa4 (the picture 922a39c4, CButton_1..14 and the
   tip be813131 -- DisplayGameTip 801030bc is an empty stub, so the tip never
   shows), PrepToShowControllerConfig 801030c0 streams the platform's pad
   picture (PS2 Loading_controller_ps2, GC 0x302059b8, PC
   "LOADING_CONTROLLER_PC") and its callback ShowLoadingTip 80102f40 runs
   SetupControllerConfig 80102dd8: CButton_%d (1..14) gets the language hash
   of what its button does (FindButtonNameHashForFEString, table GC
   0x8035e2c8), hidden when 0; outside the career World Map / SMS / Career
   Statistics / Enter Shop read "Not Used" (0xf8b4303b).
   The Vita has no picture in the game: ours is drawn here in the pad
   pictures' style (DYNTEX: the device on a clear ground, light leader lines
   from each control out to the 7 left / 7 right label heights). The labels
   are the GC table's 14 actions, from g_opt's live bindings, each in the slot
   nearest its control. GUESS: the art (no Vita art exists) and the rear
   touch inset (the shift / hydraulics corners sit behind the device). */
#define LSC_N 14
#define LSC_W 256
static const uint32_t k_lsc_obj[LSC_N] = {   /* CButton_1..7 (left, top down), CButton_8..14 (right) */
    0xbaa4198eu, 0xbaa4198fu, 0xbaa41990u, 0xbaa41991u, 0xbaa41992u, 0xbaa41993u, 0xbaa41994u,
    0xbaa41995u, 0xbaa41996u, 0x0f274b7eu, 0x0f274b7fu, 0x0f274b80u, 0x0f274b81u, 0x0f274b82u };
static const struct { int act; uint32_t str; int career; } k_lsc_act[LSC_N] = {   /* the GC table's labels */
    { OPT_ACT_STEER_L, 0xecd20f7fu, 0 },      /* Steer */
    { OPT_ACT_THROTTLE, 0x619b23b0u, 0 },     /* Accelerate */
    { OPT_ACT_BACK, 0x6ab80afau, 0 },         /* Brake / Reverse */
    { OPT_ACT_HANDBRAKE, 0xb70ff4a4u, 0 },    /* Handbrake */
    { OPT_ACT_NITROUS, 0x5e5c5f2eu, 0 },      /* Nitrous */
    { OPT_ACT_SHIFT_UP, 0xdc1cd97du, 0 },     /* Shift Up */
    { OPT_ACT_SHIFT_DOWN, 0x56afe190u, 0 },   /* Shift Down */
    { OPT_ACT_CAMERA, 0xd38df194u, 0 },       /* Change Camera */
    { OPT_ACT_LOOK_BACK, 0x13121940u, 0 },    /* Look Back */
    { OPT_ACT_RESET, 0x1cedc132u, 0 },        /* Reset Car */
    { OPT_ACT_ENGAGE, 0xab1032fau, 1 },       /* Enter Shop/Challenge */
    { OPT_ACT_WORLD_MAP, 0x6026c91fu, 1 },    /* World Map */
    { OPT_ACT_READ_SMS, 0x6ef09d0du, 1 },     /* Display SMS */
    { OPT_ACT_CAREER_STATS, 0x70a30056u, 1 }, /* Career Statistics */
};
#define LSC_NOT_USED 0xf8b4303bu

/* the controls, in picture pixels (body centre 128, 131) */
static void lsc_anchor(int vb, float *x, float *y) {
    static const float a[VB_COUNT][2] = {
        { 0, 0 },
        { 216, 124 }, { 226, 114 }, { 206, 114 }, { 216, 104 },
        { 40, 80 }, { 216, 80 },
        { 40, 105 }, { 40, 123 }, { 31, 114 }, { 49, 114 },
        { 222, 166 }, { 207, 166 },
        { 54, 146 }, { 54, 146 },
        { 114, 211 }, { 142, 211 }, { 114, 223 }, { 142, 223 },
        { 54, 136 }, { 54, 156 },
        { 202, 136 }, { 202, 156 },
        { 192, 146 }, { 212, 146 },
        { 97, 113 }, { 159, 113 },
        { 97, 149 }, { 159, 149 }
    };

    *x = a[vb][0];
    *y = a[vb][1];
}

typedef struct { int k; float a, b, c, d, e, w; } LscShp;   /* 0 rrect (cx cy hw hh r; w > 0: its outline), 1 segment (x0 y0 x1 y1, half width w) */
static float lsc_sd(const LscShp *q, float x, float y) {
    if (q->k == 0) {
        float r = q->e, qx = fabsf(x - q->a) - (q->c - r), qy = fabsf(y - q->b) - (q->d - r);
        float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
        float d = sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
        return q->w > 0 ? fabsf(d) - q->w : d;
    }
    float px = x - q->a, py = y - q->b, vx = q->c - q->a, vy = q->d - q->b;
    float l2 = vx * vx + vy * vy, t = l2 > 0 ? (px * vx + py * vy) / l2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float dx = px - vx * t, dy = py - vy * t;
    return sqrtf(dx * dx + dy * dy) - q->w;
}
/* one shape over the premultiplied canvas: colour c0 at its top, c1 at its bottom */
static void lsc_paint(float *cv, LscShp q, uint32_t c0, uint32_t c1, float alpha) {
    float x0, y0, x1, y1, m = q.w + 1.5f;
    if (q.k == 0) { x0 = q.a - q.c; x1 = q.a + q.c; y0 = q.b - q.d; y1 = q.b + q.d; }
    else { x0 = fminf(q.a, q.c); x1 = fmaxf(q.a, q.c); y0 = fminf(q.b, q.d); y1 = fmaxf(q.b, q.d); }
    int ix0 = (int)floorf(x0 - m), ix1 = (int)ceilf(x1 + m), iy0 = (int)floorf(y0 - m), iy1 = (int)ceilf(y1 + m);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > LSC_W - 1) ix1 = LSC_W - 1;
    if (iy1 > LSC_W - 1) iy1 = LSC_W - 1;
    float h = y1 - y0 > 0 ? y1 - y0 : 1;
    for (int y = iy0; y <= iy1; y++) {
        float t = ((float)y + 0.5f - y0) / h;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float col[3];
        for (int k = 0; k < 3; k++) {
            float a = (float)(c0 >> (16 - 8 * k) & 255), b = (float)(c1 >> (16 - 8 * k) & 255);
            col[k] = (a + (b - a) * t) / 255.0f;
        }
        for (int x = ix0; x <= ix1; x++) {
            float cov = 0.5f - lsc_sd(&q, (float)x + 0.5f, (float)y + 0.5f);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            float a = cov * alpha, *p = cv + ((size_t)y * LSC_W + x) * 4;
            for (int k = 0; k < 3; k++) p[k] = col[k] * a + p[k] * (1 - a);
            p[3] = a + p[3] * (1 - a);
        }
    }
}
static LscShp lsc_rr(float cx, float cy, float hw, float hh, float r) { LscShp q = { 0, cx, cy, hw, hh, r, 0 }; return q; }
static LscShp lsc_rro(float cx, float cy, float hw, float hh, float r, float w) { LscShp q = { 0, cx, cy, hw, hh, r, w }; return q; }
static LscShp lsc_seg(float x0, float y0, float x1, float y1, float w) { LscShp q = { 1, x0, y0, x1, y1, 0, w }; return q; }
#define LSC_C(cx, cy, r) lsc_rr(cx, cy, r, r, r)

static void lsc_face(float *cv, float cx, float cy, int sym) {
    static const uint32_t col[4] = { 0x5ae0a8u, 0xff6a6au, 0x78a8ffu, 0xf08ad8u };   /* triangle circle cross square */
    lsc_paint(cv, LSC_C(cx, cy, 5.6f), 0x4a4a50u, 0x2a2a2eu, 1);
    lsc_paint(cv, lsc_rro(cx, cy, 5.6f, 5.6f, 5.6f, 0.5f), 0x9a9aa0u, 0x9a9aa0u, 1);
    uint32_t c = col[sym];
    float w = 0.6f;
    if (sym == 0) {
        lsc_paint(cv, lsc_seg(cx, cy - 2.8f, cx - 2.6f, cy + 1.8f, w), c, c, 1);
        lsc_paint(cv, lsc_seg(cx, cy - 2.8f, cx + 2.6f, cy + 1.8f, w), c, c, 1);
        lsc_paint(cv, lsc_seg(cx - 2.6f, cy + 1.8f, cx + 2.6f, cy + 1.8f, w), c, c, 1);
    } else if (sym == 1) lsc_paint(cv, lsc_rro(cx, cy, 2.7f, 2.7f, 2.7f, w), c, c, 1);
    else if (sym == 2) {
        lsc_paint(cv, lsc_seg(cx - 2.4f, cy - 2.4f, cx + 2.4f, cy + 2.4f, w), c, c, 1);
        lsc_paint(cv, lsc_seg(cx - 2.4f, cy + 2.4f, cx + 2.4f, cy - 2.4f, w), c, c, 1);
    } else lsc_paint(cv, lsc_rro(cx, cy, 2.4f, 2.4f, 0.3f, w), c, c, 1);
}
static void lsc_stick(float *cv, float cx, float cy) {
    lsc_paint(cv, LSC_C(cx, cy, 9.0f), 0x121214u, 0x121214u, 1);
    lsc_paint(cv, LSC_C(cx, cy, 6.8f), 0x5a5a60u, 0x2e2e32u, 1);
    lsc_paint(cv, lsc_rro(cx, cy, 6.8f, 6.8f, 6.8f, 0.5f), 0x8a8a90u, 0x8a8a90u, 1);
}
static void lsc_device(float *cv) {
    /* rear touch inset: the back of the device, under it, in quarters */
    lsc_paint(cv, lsc_rr(128, 217, 46, 20, 18), 0x26262au, 0x1c1c1eu, 0.95f);
    lsc_paint(cv, lsc_rro(128, 217, 46, 20, 18, 0.6f), 0x8a8a90u, 0x6a6a70u, 1);
    lsc_paint(cv, lsc_rr(128, 217, 30, 12, 2), 0x3c3c42u, 0x34343au, 1);
    lsc_paint(cv, lsc_rro(128, 217, 30, 12, 2, 0.5f), 0x7a7a80u, 0x7a7a80u, 1);
    lsc_paint(cv, lsc_seg(128, 205, 128, 229, 0.4f), 0x6a6a70u, 0x6a6a70u, 1);
    lsc_paint(cv, lsc_seg(98, 217, 158, 217, 0.4f), 0x6a6a70u, 0x6a6a70u, 1);
    /* shoulders, then the body over them */
    lsc_paint(cv, lsc_rr(40, 81, 22, 4.5f, 4.5f), 0x9a9aa0u, 0x6a6a70u, 1);
    lsc_paint(cv, lsc_rr(216, 81, 22, 4.5f, 4.5f), 0x9a9aa0u, 0x6a6a70u, 1);
    lsc_paint(cv, lsc_rr(128, 131, 118, 50, 44), 0x3a3a40u, 0x1c1c20u, 1);
    lsc_paint(cv, lsc_rro(128, 131, 118, 50, 44, 0.8f), 0xc8c8ceu, 0x8a8a90u, 1);
    /* screen */
    lsc_paint(cv, lsc_rr(128, 131, 61, 36, 3), 0x0c0c10u, 0x050507u, 1);
    lsc_paint(cv, lsc_rro(128, 131, 61, 36, 3, 0.5f), 0x56565cu, 0x56565cu, 1);
    lsc_paint(cv, lsc_rr(128, 115, 58, 18, 2), 0x2a2a34u, 0x0c0c10u, 0.35f);   /* glare */
    /* d-pad */
    lsc_paint(cv, lsc_rr(40, 114, 3.8f, 11, 1.2f), 0x8a8a90u, 0x6a6a70u, 1);
    lsc_paint(cv, lsc_rr(40, 114, 11, 3.8f, 1.2f), 0x8a8a90u, 0x6a6a70u, 1);
    lsc_paint(cv, LSC_C(40, 114, 2.0f), 0x5a5a60u, 0x5a5a60u, 1);
    /* face buttons */
    lsc_face(cv, 216, 104, 0); lsc_face(cv, 226, 114, 1); lsc_face(cv, 216, 124, 2); lsc_face(cv, 206, 114, 3);
    /* sticks, PS button, select / start */
    lsc_stick(cv, 54, 146); lsc_stick(cv, 202, 146);
    lsc_paint(cv, LSC_C(32, 164, 3.2f), 0x7a7a80u, 0x5a5a60u, 1);
    lsc_paint(cv, lsc_rr(207, 166, 4.5f, 2.0f, 2.0f), 0x8a8a90u, 0x6a6a70u, 1);
    lsc_paint(cv, lsc_rr(222, 166, 4.5f, 2.0f, 2.0f), 0x8a8a90u, 0x6a6a70u, 1);
}

/* slot_y: picture y of the 7 left + 7 right labels; vb[i]: the control
   (VB_*) of k_lsc_act[i], 0 none. Out: lab[slot] = k_lsc_act index + 1,
   0 an empty slot. */
/* The device art from a file when there is one: 256x256 straight-alpha RGBA
   (loading_controller.raw, made from img/loading_controller.png by
   tools/lsc_png.py at build time). ux0:data/NFSU2 first (art tests without
   a reinstall), then the app's own copy. 0 = none: drawn by lsc_device. */
static int lsc_device_file(float *cv) {
    static const char *const k_path[2] = { "ux0:data/NFSU2/loading_controller.raw", "app0:assets/loading_controller.raw" };
    const size_t n = (size_t)LSC_W * LSC_W * 4;
    for (int k = 0; k < 2; k++) {
        FILE *f = fopen(k_path[k], "rb");
        if (!f) continue;
        unsigned char *px = (unsigned char *)malloc(n);
        int ok = px && fread(px, 1, n, f) == n;
        fclose(f);
        if (ok) {
            for (size_t i = 0; i < (size_t)LSC_W * LSC_W; i++) {   /* to premultiplied */
                float a = px[i * 4 + 3] / 255.0f;
                for (int c = 0; c < 3; c++) cv[i * 4 + c] = px[i * 4 + c] / 255.0f * a;
                cv[i * 4 + 3] = a;
            }
            free(px);
            return 1;
        }
        free(px);
        printf("shop: %s is not 256x256 RGBA, ignored\n", k_path[k]);
    }
    return 0;
}
static void lsc_build(unsigned char *rgba, const float *slot_y, const int *vb, int *lab) {
    float *cv = (float *)calloc((size_t)LSC_W * LSC_W * 4, sizeof(float));
    for (int i = 0; i < LSC_N; i++) lab[i] = 0;
    if (!cv) { memset(rgba, 0, (size_t)LSC_W * LSC_W * 4); return; }
    if (!lsc_device_file(cv)) lsc_device(cv);
    if (getenv("OPENUG2_LSC_DUMP")) {   /* tools: the device layer + the label rows, for the art template */
        FILE *df = fopen(getenv("OPENUG2_LSC_DUMP"), "wb");
        if (df) {
            for (size_t i = 0; i < (size_t)LSC_W * LSC_W; i++) {
                float a = cv[i * 4 + 3];
                unsigned char o[4];
                for (int c = 0; c < 3; c++) { float v = a > 0 ? cv[i * 4 + c] / a : 0; o[c] = (unsigned char)(v >= 1 ? 255 : v * 255.0f + 0.5f); }
                o[3] = (unsigned char)(a >= 1 ? 255 : a * 255.0f + 0.5f);
                fwrite(o, 1, 4, df);
            }
            fclose(df);
        }
        for (int i = 0; i < LSC_N; i++) printf("lsc slot %d y %.1f\n", i, slot_y[i]);
        for (int v = 1; v < VB_COUNT; v++) { float x, y; lsc_anchor(v, &x, &y); printf("lsc anchor %d %.0f %.0f\n", v, x, y); }
    }
    /* each bound action on its control's side; a full side gives its most
       central ones to the other */
    int side[LSC_N], n[2] = { 0, 0 };
    float ax[LSC_N], ay[LSC_N];
    for (int i = 0; i < LSC_N; i++) {
        side[i] = -1; ax[i] = ay[i] = 0;
        if (vb[i] <= 0 || vb[i] >= VB_COUNT) continue;
        lsc_anchor(vb[i], &ax[i], &ay[i]);
        side[i] = ax[i] >= 128.0f; n[side[i]]++;
    }
    for (int sd = 0; sd < 2; sd++)
        while (n[sd] > 7) {
            int best = -1;
            for (int i = 0; i < LSC_N; i++)
                if (side[i] == sd && (best < 0 || fabsf(ax[i] - 128.0f) < fabsf(ax[best] - 128.0f))) best = i;
            side[best] = !sd; n[sd]--; n[!sd]++;
        }
    for (int sd = 0; sd < 2; sd++) {
        int it[7], k = 0;
        for (int i = 0; i < LSC_N; i++) if (side[i] == sd && k < 7) it[k++] = i;
        for (int a = 1; a < k; a++)   /* by control height, then across */
            for (int b = a; b > 0 && (ay[it[b]] < ay[it[b - 1]] || (ay[it[b]] == ay[it[b - 1]] && ax[it[b]] < ax[it[b - 1]])); b--) {
                int t = it[b]; it[b] = it[b - 1]; it[b - 1] = t;
            }
        /* in that order, the slots nearest their controls */
        float cost[8][8]; int take[8][8];
        for (int a = 0; a <= k; a++) for (int b = 0; b <= 7; b++) { cost[a][b] = 1e9f; take[a][b] = 0; }
        for (int b = 0; b <= 7; b++) cost[0][b] = 0;
        for (int a = 1; a <= k; a++)
            for (int b = a; b <= 7; b++) {
                float c1 = cost[a][b - 1], c2 = cost[a - 1][b - 1] + fabsf(slot_y[sd * 7 + b - 1] - ay[it[a - 1]]);
                if (c2 <= c1) { cost[a][b] = c2; take[a][b] = 1; } else cost[a][b] = c1;
            }
        int sl[7];
        for (int a = k, b = 7; a > 0 && b > 0; b--) if (take[a][b]) sl[--a] = b - 1;
        for (int a = 0; a < k; a++) {
            int i = it[a], s = sd * 7 + sl[a];
            lab[s] = i + 1;
            float sy = slot_y[s], e0 = sd ? LSC_W - 1.0f : 1.0f, dy = fabsf(ay[i] - sy);
            float ex = sd ? ax[i] + dy : ax[i] - dy;   /* level from the edge, then 45 degrees in */
            if (!sd && ex < 6.0f) ex = 6.0f;
            if (sd && ex > LSC_W - 6.0f) ex = LSC_W - 6.0f;
            LscShp h = lsc_seg(e0, sy, ex, sy, 1.6f), d = lsc_seg(ex, sy, ax[i], ay[i], 1.6f);
            lsc_paint(cv, h, 0x000000u, 0x000000u, 0.55f); lsc_paint(cv, d, 0x000000u, 0x000000u, 0.55f);
            h.w = d.w = 0.75f;
            lsc_paint(cv, h, 0xdcdce0u, 0xdcdce0u, 1); lsc_paint(cv, d, 0xdcdce0u, 0xdcdce0u, 1);
            lsc_paint(cv, LSC_C(ax[i], ay[i], 2.0f), 0xf0f0f4u, 0xf0f0f4u, 1);
        }
    }
    for (int i = 0; i < LSC_W * LSC_W; i++) {   /* to straight alpha */
        float a = cv[i * 4 + 3];
        for (int k = 0; k < 3; k++) {
            float v = a > 0.0f ? cv[i * 4 + k] / a : 0.0f;
            rgba[i * 4 + k] = (unsigned char)(v >= 1.0f ? 255 : v <= 0.0f ? 0 : v * 255.0f + 0.5f);
        }
        rgba[i * 4 + 3] = (unsigned char)(a >= 1.0f ? 255 : a * 255.0f + 0.5f);
    }
    free(cv);
}

/* the console screen for this load: GC ShowLoadingScreen's pick */
void shop_loading_pick(Shop *s, int career) {
    if (!s) return;
    s->ls_ctl = s->pk[PK_LOADING_CTL] && (!career || (rand() >> 4 & 1));   /* bRandom(2) */
    if (!s->ls_ctl) return;
    const FengPackage *pk = s->pk[PK_LOADING_CTL];
    int vb[LSC_N];
    unsigned char sig[64]; memset(sig, 0, sizeof sig);
    for (int i = 0; i < LSC_N; i++) {
        int a = k_lsc_act[i].act;
        vb[i] = g_opt.ctl[a][0] ? g_opt.ctl[a][0] : g_opt.ctl[a][1];
        if (a == OPT_ACT_STEER_L && !vb[i]) vb[i] = g_opt.ctl[OPT_ACT_STEER_R][0] ? g_opt.ctl[OPT_ACT_STEER_R][0] : g_opt.ctl[OPT_ACT_STEER_R][1];
        sig[i] = (unsigned char)vb[i];
    }
    sig[LSC_N] = (unsigned char)(career ? 2 : 1);
    if (s->ls_pic && !memcmp(sig, s->ls_sig, sizeof sig)) return;
    /* the label heights, in the picture (922a39c4, 256 pixels over its box) */
    float slot_y[LSC_N];
    int pic = feng_find(pk, 0x922a39c4u);
    float top = pic >= 0 ? pk->obj[pic].ay - 0.5f * fabsf(pk->obj[pic].size[1]) : -97.4f;
    float hgt = pic >= 0 ? fabsf(pk->obj[pic].size[1]) : 233.6f;
    for (int i = 0; i < LSC_N; i++) {
        int o = feng_find(pk, k_lsc_obj[i]);
        float y = o >= 0 ? pk->obj[o].ay : -92.6f + 36.0f * (float)(i % 7);
        if (o >= 0 && !(pk->obj[o].just & FENG_JUST_VCENTER)) y += 6.5f;   /* a top-aligned label's middle */
        slot_y[i] = (y - top) / hgt * (float)LSC_W;
    }
    unsigned char *rgba = (unsigned char *)malloc((size_t)LSC_W * LSC_W * 4);
    if (!rgba) { s->ls_ctl = 0; return; }
    int lab[LSC_N];
    lsc_build(rgba, slot_y, vb, lab);
    if (s->ls_pic) glDeleteTextures(1, &s->ls_pic);
    s->ls_pic = upload_rgba(LSC_W, LSC_W, rgba);
    free(rgba);
    for (int i = 0; i < LSC_N; i++) {
        int a = lab[i] - 1;
        s->ls_lab[i] = a < 0 ? 0 : k_lsc_act[a].career && !career ? LSC_NOT_USED : k_lsc_act[a].str;
    }
    memcpy(s->ls_sig, sig, sizeof sig);
    printf("loading: Loading_Controller and its Vita picture, slots");
    for (int i = 0; i < LSC_N; i++) printf(" %d", lab[i] ? vb[lab[i] - 1] : -1);
    printf(" (VB_*)\n");
}

/* caches built before F1.11 lack Loading_Controller: from GLOBALB, once */
static void ls_ctl_from_globalb(Shop *s) {
    char path[600];
    snprintf(path, sizeof path, "%s/GLOBAL/GLOBALB.BUN", s->dataroot);
    long glen = 0, pl = 0;
    unsigned char *gb = n2_read_file(path, &glen);
    unsigned char *pd = gb ? extract_package(gb, glen, k_pk_name[PK_LOADING_CTL], &pl) : NULL;
    unsigned char *one = pd ? (unsigned char *)malloc((size_t)pl + 8) : NULL;
    if (one) {
        uint32_t cid = 0x00030203u, csz = (uint32_t)pl;
        memcpy(one, &cid, 4); memcpy(one + 4, &csz, 4); memcpy(one + 8, pd, (size_t)pl);
        FengPackage *pk = (FengPackage *)malloc(sizeof *pk);
        if (pk && feng_load_package(one, pl + 8, k_pk_name[PK_LOADING_CTL], pk)) s->pk[PK_LOADING_CTL] = pk;
        else free(pk);
    }
    const FengPackage *pk = s->pk[PK_LOADING_CTL];
    if (pk && s->tex) {   /* its art the PC_Loading set lacks */
        uint32_t keys[LS_CTL_NRES]; FengImage img[LS_CTL_NRES]; int nk = 0;
        memset(img, 0, sizeof img);
        for (int r = 0; r < pk->nres && nk < LS_CTL_NRES; r++) {
            uint32_t key = pk->res[r].type == 1 ? feng_tex_key(pk->res[r].name) : 0;
            if (key && key != LS_CTL_PIC_KEY && !tex_find(s, key)) keys[nk++] = key;
        }
        if (nk) feng_tpk_load(gb, glen, keys, nk, img);
        for (int k = 0; k < nk; k++) {
            if (!img[k].rgba) continue;
            ShopTex *t = &s->tex[s->ntex++];
            t->key = keys[k]; t->w = img[k].w; t->h = img[k].h; t->additive = img[k].blend == 6;
            t->tex = upload_rgba(img[k].w, img[k].h, img[k].rgba);
            feng_image_free(&img[k]);
        }
    }
    printf("shop: %s from GLOBALB (%s)\n", k_pk_name[PK_LOADING_CTL], pk ? "ok" : "missing");
    free(one); free(pd); free(gb);
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
    if (!s->pk[PK_LOADING_CTL]) ls_ctl_from_globalb(s);
    load_strings(s);
    load_fonts(s);
    prepare_package(s, PK_LOADING);
    prepare_package(s, PK_LOADING_CTL);
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
                       free((*sp)->dlg_anim); (*sp)->dlg_anim = NULL;
                       free((*sp)->mc_anim); (*sp)->mc_anim = NULL;
                       free((*sp)->mcl_anim); (*sp)->mcl_anim = NULL;
                       free((*sp)->kb_anim); (*sp)->kb_anim = NULL;
                       movie_stop(*sp); }
    if (!sp || !*sp) return;
    Shop *s = *sp;
    for (int i = 0; i < s->ntex; i++) if (s->tex[i].tex) glDeleteTextures(1, &s->tex[i].tex);
    glDeleteTextures(4, s->mag_tex);
    if (s->ls_pic) glDeleteTextures(1, &s->ls_pic);
    if (s->mview_tex) glDeleteTextures(1, &s->mview_tex);
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
/* the Car Shop was opened from the Quick Race car select's Square (BeginCarCustomize
   entry 2): CalcPrevScreenFromChooseCustomizeCategory 80160ac0 sends entries 2 / 3
   back to UI_QRCarSelect.fng, entry 2 in game mode 2 (Quick Race) */
static int g_cust_qr;
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

static void qr_enter_cars(Shop *s);
static void qr_build_tracks(Shop *s);
static void qr_enter_options(Shop *s);
void shop_customize_cars(Shop *s) {
    if (!s) return;
    if (g_cust_qr) {   /* entry 2: the Quick Race car select again, its race as it was */
        g_cust_qr = 0;
        s->main.sel = 1; s->main.scroll = 1.0f;     /* Quick Race, for Back */
        s->qr_cust = 0;
        s->qr_type = k_qr_modes[s->qr_mode.sel].group;
        if (s->qr_type != QR_FREERUN) { qr_build_tracks(s); qr_enter_options(s); }   /* for Back */
        qr_enter_cars(s);
        printf("quick race: back from the Car Shop (customize entry 2), mode %d\n", s->qr_type);
        return;
    }
    s->main.sel = 4; s->main.scroll = 4.0f;     /* Customize, for Back */
    s->qr_cust = 1;
    qr_enter_cars(s);
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
    if (slot >= 0 && q->available) body_put(&next, slot, q->value, s->cf_mode);
    printf("shop: cursor on %s (value %d%s)\n", q->name, q->value, q->available ? "" : ", not fitted");
    if (memcmp(&next, &s->preview, sizeof next)) { s->preview = next; request_preview(s); }
}

/* IsCarbonPartConverterAvailable 801711f4: slot 0x5d -> entity 0x55, 0x60 ->
   0x56, level 3, with GetPartUnlockFilter (2 in the crib: never) */
static int cf_converter_ok(const Shop *s, int slot) {
    return (!s->career || (s->career & 4)) && calc_unlocked(s, slot == G_CF_KIT ? 0x55 : 0x56, 3);
}
/* GetPartsList's count of one Body Shop group (build_parts, carbon or not) */
static int body_count(Shop *s, int group, int carbon) {
    int cf = s->cf_mode;
    s->cf_mode = carbon;
    build_parts(s, group);
    s->cf_mode = cf;
    int n = s->nparts;
    s->nparts = 0; s->part_sel = 0; s->part_group = -1;
    return n;
}
/* the four carbon categories (AddCategory with its carbon flag: locked when
   GetPartsList finds none) and the converters (IsCarbonPartConverterAvailable) */
static void cf_locks(Shop *s, unsigned char lk[6]) {
    static const int g[4] = { G_HOOD, G_SPOILER, G_ROOF, G_LEFT_MIRROR };
    for (int i = 0; i < 4; i++) lk[i] = body_count(s, g[i], 1) <= 0;
    lk[4] = !cf_converter_ok(s, G_CF_TRUNK);
    lk[5] = !cf_converter_ok(s, G_CF_KIT);
}
/* PartsCategoryRimsIsLocked 8015ea90 / 0x539c20: outside the crib open with
   two available slot 0x1d parts -- the car's stock rim and one WHEELS rim.
   In the crib (filter 2) it returns AreAnyRimsOwnedInCareerMode 8012fc0c
   (AnyOwned(0x1d) = the rims region, rims and spinners; PC: car +0x5a0 != 0)
   as is: the category is LOCKED once the car owns a rim (GC and PC agree) */
static int rims_locked(Shop *s) {
    int model = db_model(s, "WHEELS");
    if (s->career == 2) return career_any_owned(slot_ent(0x1d)) || career_any_owned(0x21);
    if (!s->db || model < 0) return 0;
    int rim_outer = g_shop_rec_ok ? (signed char)g_shop_rec[0xdc] : 0;
    for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
        const unsigned char *rec = s->db + p;
        uint32_t hash = n2_u32(rec);
        unsigned soff = rec[8] | rec[9] << 8, aoff = rec[10] | rec[11] << 8;
        if (!hash && !soff) break;
        if (rec[7] != model || rec[4] != G_WHEEL || part_excluded(s, rec)) continue;
        if (rim_outer) {
            int st, in, out;
            const char *fold = NULL;
            for (int k = 0; k < s->nrb && !fold; k++)
                if (s->rb[k].folder && rim_part_parse(s->rb[k].folder, hash, &st, &in, &out)) fold = s->rb[k].folder;
            if (!fold || out != rim_outer) continue;
        }
        if (part_open(s, 13, (rec[5] >> 5) & 7, hash)) return 0;
    }
    return 1;
}
/* SetupBodyShop: AddCategory locks a category when GetPartsList has at most
   the stock part; Rims / Carbon Fiber by their own tests */
static void body_locks(Shop *s) {
    for (int i = 0; i < N_BODY; i++) {
        int g = k_body_shop[i].group;
        s->body.locked[i] = group_slot(g) < 0 && g != G_WHEEL && g != G_CARBON;   /* set_locks */
        if (s->body.locked[i]) continue;
        if (g == G_WHEEL) s->body.locked[i] = rims_locked(s);
        else if (g == G_CARBON) {   /* PartsCategoryCarbonIsLocked 8015ec10 */
            unsigned char lk[6]; cf_locks(s, lk);
            s->body.locked[i] = lk[0] && lk[1] && lk[2] && lk[3] && lk[4] && lk[5];
        } else s->body.locked[i] = body_count(s, g, 0) <= 1;
    }
}
static void open_parts(Shop *s) {
    int g = k_body_shop[s->body.sel].group;
    s->cf_mode = 0;
    build_parts(s, g);
    s->screen = SC_PARTS;
}
static int spec_locked(Shop *s, int slot);
static int neon_all_locked(Shop *s);
/* the Car Shop's categories (UI_ChooseCustomizeCategory's next screens) */
static void car_shop_open(Shop *s, int cat) {
    static const int to[CAT_COUNT] = { SC_BODY, SC_PERFORMANCE, SC_GRAPHICS, SC_SPECIALTIES };
    s->screen = to[cat];
    s->cust_shop = cat;   /* React__22CustomizeCategoryThing 8030e25c: gCarCustomizeManager+0x1c = the category
                             (0 parts, 1 perf, 2 paint, 3 ICE: CalcNextScreenFromChooseCustomizeCategory 8016092c) */
    if (s->screen == SC_BODY) body_locks(s);
    if (s->screen == SC_PERFORMANCE) {
        if (s->career && s->cust_entry == 0) scroller_set(&s->cat, k_perf_shop_career, NELEM(k_perf_shop_career));
        else scroller_set(&s->cat, k_perf_shop, NELEM(k_perf_shop));
    }
    if (s->screen == SC_GRAPHICS) {
        /* Setup__23ChoosePaintShopCategory 801765e0: career "Purchase Setup" */
        scroller_set(&s->cat, s->career ? k_graphics_shop_career : k_graphics_shop, NELEM(k_graphics_shop));
        s->gfx_base = s->fitted;
    }
    if (s->screen == SC_SPECIALTIES) {
        scroller_set(&s->cat, k_special_shop, NELEM(k_special_shop));
        for (int i = 0; i < s->cat.n; i++)   /* Setup__18IceSelectionScreen: IceCategoryIsLocked each, Neon IsNeonCompletelyLocked */
            s->cat.locked[i] = s->cat.e[i].group == 0x9e ? neon_all_locked(s) : spec_locked(s, s->cat.e[i].group);
        char lk[64] = ""; int o = 0;
        for (int i = 0; i < s->cat.n && o < 56; i++) o += snprintf(lk + o, sizeof lk - o, " %x%s", s->cat.e[i].group, s->cat.locked[i] ? "L" : "");
        printf("shop: specialties locks%s\n", lk);
    }
}
/* BACK on a shop's category screen: the Car Shop, or in a city shop (entry 0:
   CalcPrevScreen gives "") "Are you sure you wish to exit back to Explore
   Mode?" (0xd76e0b5a, Yes 0xd05fc3a3 / No 0x1fab5998) */
static void cust_up(Shop *s) {
    if (s->career && !s->cust_entry) dialog(s, DLG_AUTO, 2, 0xd76e0b5au, DA_SHOP_LEAVE, STR_YES, STR_NO);
    else s->screen = SC_CAR_SHOP;
}
/* the rim browser's accept (0x567a50 / 0x55fb00): fitted -> "already
   installed", in the career not enough cash / buy, else install */
static void rim_accept(Shop *s) {
    const ShopPart *q = rim_cur(s);
    if (!strcmp(s->prev_folder, s->rim_folder) && s->prev_style == s->rim_style)
        dialog(s, PK_DLG_S, 1, 0xda15d7e5u, DA_NONE, 0, 0);
    else if (part_to_buy(s, q) && g_career.cash < q->price) dialog(s, PK_DLG_S, 1, 0xd1ac60beu, DA_NONE, 0, 0);
    else if (part_to_buy(s, q)) dialog(s, PK_DLG_S, 2, 0x44b7167fu, DA_RIM_BUY, 0, 0);
    else dialog(s, PK_DLG_S, 2, 0xf5ac2b2au, DA_RIM, 0, 0);
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
    cf_locks(s, s->cfm.locked);
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
    else if (key == SHOP_KEY_OK && !s->cfm.locked[s->cfm.sel]) {
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
    const char *bn = beyond_event_name(ev);   /* the World Expansion's events */
    if (bn) return bn;
    char l[40];
    snprintf(l, sizeof l, reverse ? "REVERSE_TRACK_NAME_%d" : "TRACK_NAME_%d", ev);
    return str_get(s, feng_hash(l));
}

static void qr_map_request(Shop *s) {
    int ev = s->qr_track.n ? s->qr_track_e[s->qr_track.sel].group : 0;
    if (ev == s->map_want) return;
    s->map_want = ev; s->map_wait = 0.3f;      /* shown once the cursor rests (0x4cdd40) */
}

/* 0x4cded0: the mode's tracks in database order, the locked ones after
   (AddTrack 0x4b4c10: UnlockSystem::IsTrackUnlocked(1, race) 0x5156f0 ->
   IsTrackUnlockedForQuickRace; the sort 0x4987a0) with the lock icon
   (CalcIconHash 0x4984b0: f05704ac); a saved pick that is locked falls back
   to the first track. Accept on a locked one does nothing (800e4b5c). */
static void qr_build_tracks(Shop *s) {
    int t = s->qr_type, bit = qr_bit(t), n = 0, sel = -1;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < s->ntrk && n < QR_MAX_TRACKS; i++) {
            const unsigned char *r = s->trk + (long)i * 0x128;
            if (r[0x88] || !(n2_u32(r + 0x94) & (1u << bit))) continue;
            int locked = !career_qr_track_unlocked(rec_ev(r));
            if (locked != pass) continue;
            s->qr_track_rec[n] = (short)i;
            s->qr_track_lock[n] = (unsigned char)locked;
            s->qr_track_e[n].str = 0;
            s->qr_track_e[n].tex = locked ? 0xf05704acu : qr_track_icon(t, r);
            s->qr_track_e[n].group = rec_ev(r);
            if (rec_ev(r) == s->set[t].ev && !locked) sel = n;
            n++;
        }
    if (sel < 0) sel = 0;
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
    case OR_SAVE:       /* 0x49b170: Autosave only for a named profile (0x502fe0) */
        if (step > 0) {
            if (g_opt.autosave) { g_opt.save_on = 1; g_opt.autosave = 0; }
            else if (g_opt.save_on) g_opt.save_on = 0;
            else { g_opt.save_on = 1; g_opt.autosave = (unsigned char)profile_named(); }
        } else {
            if (g_opt.autosave) { g_opt.save_on = 0; g_opt.autosave = 0; }
            else if (g_opt.save_on) { g_opt.autosave = (unsigned char)profile_named(); g_opt.save_on = g_opt.autosave; }
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
/* a category opening: the profile as it is (AllocBackupDB 0x52a460) and
   the save settings, for the questions on the way out */
static void opt_mark(Shop *s) {
    profile_snapshot();
    s->opt_save0 = g_opt.save_on; s->opt_auto0 = g_opt.autosave;
}
static void opt_enter(Shop *s, int cat) {
    const FengPackage *pk = s->pk[PK_OPTIONS];
    s->opt_cat = cat;
    opt_mark(s);
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
    opt_mark(s);
    s->ctl_sel = 0; s->ctl_col = 0; s->ctl_top = 0; s->ctl_cap = 0;
    s->ctl_instr = STR_CTL_HINT;   /* port: was STR_CTL_CONFIG "Configure your settings" */
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
    s->ctl_instr = STR_CTL_HINT;   /* port: was STR_CTL_ASSIGN */
    audio_fe_play(FE_SND_SELECT);
    printf("controls: %s %s = %s\n", s->ctl_col ? "secondary" : "primary",
           str_get(s, k_ctl_name[s->ctl_sel]), g_vb_name[vb]);
    ctl_scripts(s, 1);
}

/* 0x4f9340: Up / Down move the cursor (Up from the first row to the device
   title), Left / Right between the Primary and Secondary cells, accept
   starts the capture (0x406415e3: Pulse, "Press a button ..."), back leaves
   for UI_OptionsMain. 0xc519bfbf asks "Reset to default settings?" in a
   GenericDialog_SMALL (here Triangle): OK (0xefedcba0) resets, Cancel.
   Port addition (GitHub #24): Square clears the highlighted cell's binding. */
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
    } else if (key == SHOP_KEY_SQ && s->ctl_sel >= 0) {
        if (!g_opt.ctl[s->ctl_sel][s->ctl_col]) return;
        g_opt.ctl[s->ctl_sel][s->ctl_col] = VB_NONE;
        audio_fe_play(FE_SND_SELECT);
        printf("controls: %s %s cleared\n", s->ctl_col ? "secondary" : "primary", str_get(s, k_ctl_name[s->ctl_sel]));
    } else if (key == SHOP_KEY_TRI) {
        dialog(s, PK_DLG_S, 2, 0x2bbcca6bu, DA_CTL_RESET, STR_OK, STR_CANCEL);
        return;
    } else if (key == SHOP_KEY_BACK) {
        opt_leave(s);
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
    opt_mark(s);
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
        opt_leave(s);
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
    PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit); memcpy(pp.junk, s->fitted.junk, sizeof pp.junk);
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

/* IsPaintAvailable 80179654: the swatch's BRAND picks the unlockable
   entity (gloss 0x25 by default); VINYL_PAINT is always there. The filter is
   4 in a career shop (CalculateIfUnlocked), 2 in the crib (nothing), 1 outside
   the career (QuickRaceUnlocker -> filter 4 again) */
static int paint_avail(const Shop *s, uint32_t brand, int lvl) {
    int ent;
    switch (brand) {
    case 0x3e871f1u: return 1;
    case 0x3437a52u: ent = 0x26; break;
    case 0x16bdceu:  ent = 0x28; break;
    case 0xda27u:    ent = 0x4d; break;
    case 0x3797533u: ent = 0x27; break;
    case 0x66350f0u: ent = 0x4e; break;
    case 0xd6640dffu: ent = 0x4f; break;
    default:         ent = 0x25; break;
    }
    return (!s->career || (s->career & 4)) && calc_unlocked(s, ent, lvl);
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
        if (!paint_avail(s, br, r[5] >> 5)) continue;   /* BuildPaintList: IsPaintAvailable 80179654 */
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
    /* Customize, not the career: no cost (0x545460); the career shows the
       setup's Total Cost and, browsing, the colour's price (0x55d0d0) */
    int car = s->career != 0;
    if ((o = feng_find(pk, 0x9ff84a88u)) >= 0) { feng_anim_hide(a, o, !car); feng_anim_set(a, o, feng_hash_upper(car ? "SHOW" : "HIDE"), 1); }
    if ((o = feng_find(pk, 0x0013c45eu)) >= 0) feng_anim_hide(a, o, !car || s->vmode >= 0);
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
    /* PC 0x55cc50 fades the page arrows in here for slots 0x3f/0x45/0x4b/0x4c.
       They are flag-0x8 PC objects (mouse page arrows): the console build hides
       them at load (GC FEngHidePCObjects 8014ddc4, feng.c platform pass) and no
       GC code shows them, so they stay hidden. */
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
        if (s->vmode < 0 && setup_short(s, setup_part_cost(s, paint_slot(s), s->paint_hash[s->paint_sel]))) return;
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
        int n = 0, row = vinyl_price_row(k_vinyl_cat[i].group);   /* AddVinylCategory 80163ec4: locked if */
        for (int k = 0; k < vinyl_count() && !n; k++) {           /* GetPartsList finds no available vinyl */
            const VinylPart *v = vinyl_at(k);
            n = v->group == k_vinyl_cat[i].group &&
                part_open(s, row < 96 ? row : 0, v->level, own_key(0x40, v->hash));
        }
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
        part_cost(s, q, row < 96 ? row : 0, v->level, own_key(0x40, v->hash));
        if (!part_open(s, q->ent, v->level, q->okey)) continue;
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
    if (!setup_short(s, s->parts[s->part_sel].price)) dialog(s, PK_DLG_M, 2, 0x31fbc167u, DA_VINYL_ADD, 0, 0);
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
            if (setup_short(s, s->parts[s->part_sel].price)) break;
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
/* a WINDOW_TINT part's light material as N2CarConfig.tint keeps it */
static uint32_t tint_value(uint32_t mat) { return mat != 0x471a1dcau /* WINDSHIELD */ ? mat : 0; }
static uint32_t tint_of(const ShopPart *q) { return q ? tint_value(q->hash) : 0; }
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
        if (r[4] != G_WINDOW_TINT || part_excluded(s, r)) continue;   /* EXCLUDE_* by model */
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
        part_cost(s, q, slot_ent(0x99), lvl, own_key(0x99, hash));
        if (!part_open(s, q->ent, lvl, q->okey)) continue;   /* GetPartsList: IsPartAvailable */
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
        else if (!career_gate(s, &s->parts[s->part_sel], DLG_AUTO, DA_TINT)) dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_TINT, 0, 0);
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
/* a HEADLIGHT_BULB part's N2CarConfig.bulb: RED | GREEN << 8 | BLUE << 16, STOCK 0 */
static uint32_t bulb_value(const Shop *s, unsigned aoff) {
    uint32_t red = 0, green = 0, blue = 0, lang = 0;
    db_attr(s, aoff, 0x0000d99au, &red);
    db_attr(s, aoff, 0x02ddc8f0u, &green);
    db_attr(s, aoff, 0x00136707u, &blue);
    db_attr(s, aoff, 0x4732da07u, &lang);
    return lang == 0x03b83203u ? 0 : (red & 0xff) | (green & 0xff) << 8 | (blue & 0xff) << 16;   /* STOCK: 0 */
}
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
        if (r[4] != G_HEADLIGHT_BULB || part_excluded(s, r)) continue;   /* EXCLUDE_* by model */
        uint32_t lang = 0;
        db_attr(s, aoff, 0x4732da07u, &lang);
        ShopPart *q = &s->parts[s->nparts];
        memset(q, 0, sizeof *q);
        const char *nm = lang ? str_get(s, lang) : "";
        snprintf(q->name, sizeof q->name, "%s", nm[0] ? nm : s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?");
        q->hash = bulb_value(s, aoff);
        q->available = 1;
        int lvl = ((r[5] | r[6] << 8) >> 5) & 7;
        q->level = lvl > 3 ? 3 : lvl;
        part_cost(s, q, slot_ent(0x9f), lvl, own_key(0x9f, hash));
        if (!part_open(s, q->ent, lvl, q->okey)) continue;
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
        else if (!career_gate(s, &s->parts[s->part_sel], DLG_AUTO, DA_BULB)) dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_BULB, 0, 0);
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
/* a part's ShopPart value in the lists below (not the hood / spinner
   ones): the door style its ANIMSTYLE, the rest their level */
static int spec_value(const Shop *s, int slot, const unsigned char *r) {
    int upg = r[5] | r[6] << 8;
    uint32_t v = 0;
    if (slot == 0xa0) { db_attr(s, r[10] | r[11] << 8, 0x38A30E75u, &v); return (int)v; }   /* ANIMSTYLE */
    return (upg >> 5) & 7;
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
        if (r[4] != group || part_excluded(s, r)) continue;   /* EXCLUDE_* by model */
        if (model >= 0 && r[7] != model) continue;
        uint32_t lang = 0;
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
        default: q->value = spec_value(s, slot, r); break;
        }
        const char *nm = lang ? str_get(s, lang) : "";
        snprintf(q->name, sizeof q->name, "%s", nm[0] ? nm : s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?");
        int lvl = (upg >> 5) & 7;
        q->level = lvl > 3 ? 3 : lvl;
        /* MapCarPartToUnlockable: the split hood 0x22, spinners 0x21 (own each), else the slot's */
        /* the owned key: the part's own (spinners: the size part shown, the fitted one or the style's first) */
        uint32_t key = hash;
        if (slot == 0x1d && RIM_INNER(q->value)) {
            int st = 0, in = 0, out = 0;
            rim_part_parse(SPINNER_FOLDER, hash, &st, &in, &out);
            char nmk[48];
            snprintf(nmk, sizeof nmk, "%s_STYLE%02d_%d_%d", SPINNER_FOLDER, RIM_STYLE(q->value), RIM_INNER(q->value), out);
            uint32_t kh = feng_hash(nmk);
            const unsigned char *kr = db_part(s, kh);
            if (kr) { key = kh; upg = kr[5] | kr[6] << 8; }
        }
        lvl = (upg >> 5) & 7; q->level = lvl > 3 ? 3 : lvl;
        part_cost(s, q, slot == 9 ? 0x22 : slot == 0x1d ? 0x21 : slot_ent(slot), lvl, own_key(slot, key));
        if (!part_open(s, q->ent, lvl, q->okey)) continue;
        q->available = 1;
        s->nparts++;
    }
    return s->nparts;
}
static int perf_unlocked(const Shop *s, int cat, int lvl);
/* GetIcePartsList 8015e708's count for the slots with their own browsers
   (Window Tint 0x99, Neon 0x9a..0x9d, Lights 0x9f, Custom Gauges 0xa3):
   the slot's parts IsPartAvailable passes, as those browsers list them */
static int ice_count(const Shop *s, int slot) {
    int group = slot == 0x99 ? G_WINDOW_TINT : slot == 0x9f ? 56 /* HEADLIGHT_BULB */ :
                slot == 0xa3 ? 64 /* CUSTOM_HUD */ : 54 /* NEON */;
    int n = 0;
    if (!s->db || !s->db_parts) return 0;
    for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
        const unsigned char *r = s->db + p;
        uint32_t hash = n2_u32(r), mat = 0;
        unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
        if (!hash && !soff) break;
        if (r[4] != group || part_excluded(s, r)) continue;   /* EXCLUDE_* by model */
        if (slot == 0x99 && !db_attr(s, aoff, 0x6ba02c05u, &mat)) continue;
        if (part_open(s, slot_ent(slot), ((r[5] | r[6] << 8) >> 5) & 7, own_key(slot, hash))) n++;
    }
    return n;
}
/* IceCategoryIsLocked 8015ecb0 / 0x539d00 (every IceSelectionThing, ctor
   801693b8): nitrous purge needs a nitrous package of level 1 or 2 open,
   engine neon the engine package level 3 (IsPerfPackageAvailable 0x52a110 =
   the shop's unlock filter, not what is fitted), trunk neon the first level-3
   neon part available as trunk audio (slot 0x22); then the slot's parts
   list: locked at <= 1 part for 0x1d, 0x22, 0x99, 0x9a, 0x9b, 0x9c, 0x9d,
   0x9f..0xa1, 0xa3, at <= 0 for the rest (split hoods 9, purge, the trunk
   audio slots 0x23..) */
static int spec_locked(Shop *s, int slot) {
    int n;
    if (slot == 0xa2 && !perf_unlocked(s, 2, 1) && !perf_unlocked(s, 2, 2)) return 1;
    if (slot == 0x9b && !perf_unlocked(s, 3, 3)) return 1;
    if (slot == 0x9d) {   /* GetCarPart(0x9d, 3) && IsPartAvailable(0x22, it) */
        int ok = 0;
        for (long p = s->db_parts; s->db && p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
            const unsigned char *r = s->db + p;
            uint32_t hash = n2_u32(r);
            if (!hash && !(r[8] | r[9] << 8)) break;
            if (r[4] != 54 || ((r[5] | r[6] << 8) >> 5 & 7) != 3) continue;
            ok = part_open(s, slot_ent(0x22), 3, own_key(0x22, hash));
            break;
        }
        if (!ok) return 1;
    }
    if (slot == 0x99 || (slot >= 0x9a && slot <= 0x9d) || slot == 0x9f || slot == 0xa3) n = ice_count(s, slot);
    else if (spec_group(slot) < 0) return 1;
    else { n = spec_build(s, slot); s->nparts = 0; }
    int one = slot == 0x1d || slot == 0x22 || (slot >= 0x99 && slot <= 0x9d) || (slot >= 0x9f && slot <= 0xa1) || slot == 0xa3;
    return n <= one;
}
/* IsNeonCompletelyLocked 8015f050 */
static int neon_all_locked(Shop *s) { return spec_locked(s, 0x9a) && spec_locked(s, 0x9d) && spec_locked(s, 0x9b); }
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
        else if (s->spec_slot == 0x22) {   /* 0x56a300 (IcePartsBrowser 0x406415e3): install / buy by
                                              IsStockPartInstalled(0x22) and the target's level */
            const ShopPart *q = &s->parts[s->part_sel];
            int stock = ice_layout_level(&s->fitted) == 0;
            if (part_to_buy(s, q) && g_career.cash < q->price) dialog(s, PK_DLG_S, 1, 0xd1ac60beu, DA_NONE, 0, 0);
            else if (part_to_buy(s, q)) {
                s->buy_after = DA_SPEC;
                dialog(s, DLG_AUTO, 2, stock ? 0xf58c0cd9u : q->lvl == 0 ? 0x266131ceu : 0x4fafb114u, DA_GEN_BUY, 0, 0);
            } else dialog(s, DLG_AUTO, 2, stock ? 0x113eab80u : q->lvl == 0 ? 0x266131ceu : 0x8e4312b0u, DA_SPEC, 0, 0);
        }
        else if (!career_gate(s, &s->parts[s->part_sel], DLG_AUTO, s->spec_slot == 0x1d ? DA_RIM : DA_SPEC))
            dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, s->spec_slot == 0x1d ? DA_RIM : DA_SPEC, 0, 0);
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
    if (s->spec_slot == 9) hood_installed(&s->fitted, s->parts[s->part_sel].level);
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
    for (int i = 0; i < s->icem.n; i++)   /* IceSelectionThing ctor: IceCategoryIsLocked (Audio Components: 0x23) */
        s->icem.locked[i] = spec_locked(s, s->icem.e[i].group == ICE_GROUP_COMP ? 0x23 : s->icem.e[i].group);
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
    else if (key == SHOP_KEY_OK && s->icem.n && !s->icem.locked[s->icem.sel]) {
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
/* a NEON part's two colours as N2CarConfig.neon[] keeps them (0x61fc20) */
static void neon_value(const Shop *s, unsigned aoff, uint32_t col[2]) {
    static const uint32_t ak[8] = { 0x0000d99au, 0x02ddc8f0u, 0x00136707u, 0x026e1ac5u,     /* RED GREEN BLUE ALPHA */
                                    0x001c0d0cu, 0x5e96e722u, 0x02804819u, 0x50317397u };   /* RED2 .. ALPHA2 */
    col[0] = col[1] = 0;
    for (int i = 0; i < 8; i++) { uint32_t c = 0; db_attr(s, aoff, ak[i], &c); col[i / 4] |= (c & 0xff) << (8 * (i % 4)); }
}
static void neon_menu(Shop *s, int keep) {
    int sel = keep ? s->neonm.sel : 0;
    scroller_set(&s->neonm, k_neon_shop, NELEM(k_neon_shop));
    for (int i = 0; i < s->neonm.n; i++)   /* 0x53bc90 per entry: IceCategoryIsLocked(0x9a / 0x9b / 0x9d) */
        s->neonm.locked[i] = spec_locked(s, 0x9a + s->neonm.e[i].group);
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
        if (r[4] != G_NEON || part_excluded(s, r)) continue;   /* EXCLUDE_* by model */
        uint32_t lang = 0, col[2];
        neon_value(s, aoff, col);
        db_attr(s, aoff, 0x4732da07u, &lang);
        ShopPart *q = &s->parts[s->nparts];
        memset(q, 0, sizeof *q);
        const char *nm = lang ? str_get(s, lang) : "";
        snprintf(q->name, sizeof q->name, "%s", nm[0] ? nm : s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?");
        q->hash = col[0]; q->value = (int)col[1];
        q->available = 1;
        int lvl = ((r[5] | r[6] << 8) >> 5) & 7;
        q->level = lvl > 3 ? 3 : lvl;
        part_cost(s, q, slot_ent(0x9a + slot), lvl, own_key(0x9a + slot, hash));
        if (!part_open(s, q->ent, lvl, q->okey)) continue;
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
        else if (key == SHOP_KEY_OK && !s->neonm.locked[s->neonm.sel]) {
            int slot = neon_slot(s);
            PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit); memcpy(pp.junk, s->fitted.junk, sizeof pp.junk);
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
        else if (!career_gate(s, &s->parts[s->part_sel], DLG_AUTO, DA_NEON)) dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_NEON, 0, 0);
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
/* a CUSTOM_HUD_PAINT part's colour as N2CarConfig.hud_col[] keeps it:
   0x01RRGGBB of its RED GREEN BLUE */
static uint32_t hud_paint_value(const Shop *s, unsigned aoff) {
    uint32_t c[3] = { 0, 0, 0 };
    db_attr(s, aoff, 0x0000d99au, &c[0]); db_attr(s, aoff, 0x02ddc8f0u, &c[1]); db_attr(s, aoff, 0x00136707u, &c[2]);
    return 0x01000000u | (c[0] & 255) << 16 | (c[1] & 255) << 8 | (c[2] & 255);
}

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
        if ((r[4] != G_CUSTOM_HUD && r[4] != G_CUSTOM_HUD_PAINT) || part_excluded(s, r)) continue;
        if (r[4] == G_CUSTOM_HUD_PAINT) {   /* HUD_Customizer::IsColorAvailable 8017c418: entity 0x17 (filter 6 / 1) */
            if (s->hud_npal < 12 && calc_unlocked(s, 0x17, r[5] >> 5)) s->hud_pal[s->hud_npal++] = hud_paint_value(s, aoff);
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
        part_cost(s, q, slot_ent(0xa3), lvl, own_key(0xa3, hash));
        if (!part_open(s, q->ent, lvl, q->okey)) continue;
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
            else if (!career_gate(s, &s->parts[s->part_sel], DLG_AUTO, DA_HUD)) dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_HUD, 0, 0);
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
        else if (!career_gate(s, &s->parts[s->part_sel], DLG_AUTO, DA_HUD)) dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_HUD, 0, 0);
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
/* the car's layout part of a location (DB group 37 + loc = slot 0x34 + loc,
   upgrade group 1 SMALL, 0 MEDIUM), NULL when the car has none */
static const unsigned char *decal_layout_rec(const Shop *s, int loc, int small) {
    int model = s->db ? db_model(s, s->car) : -1;
    const unsigned char *any = NULL;
    for (long p = s->db_parts; model >= 0 && p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
        const unsigned char *r = s->db + p;
        if (!n2_u32(r) && !(r[8] | r[9] << 8)) break;
        if (r[4] != 37 + loc || r[7] != model) continue;
        if ((r[5] & 0x1f) == (small ? 1 : 0)) return r;
        if (!any) any = r;
    }
    return any;
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
    int nbrand = 0;
    for (int i = 0; i < decal_count() && s->nparts < SHOP_MAX_PARTS; i++) {
        const DecalPart *d = decal_at(i);
        /* CarCustomizeManager::IsDecalAvailable (GC 80170d5c) -> CareerUnlocker::IsDecalBrandUnlocked
           8012f94c (Quick Race asks the same): a brand a perf package part comes in is open only once a
           career car owns it (bought with that brand) */
        if (d->name != cur && !career_decal_brand_unlocked(d->brand)) { nbrand++; continue; }
        q = &s->parts[s->nparts];
        memset(q, 0, sizeof *q);
        const char *nm = d->lang ? str_get(s, d->lang) : "";
        snprintf(q->name, sizeof q->name, "%s", nm[0] ? nm : d->dbg);
        q->hash = d->name; q->value = i; q->available = 1;
        const unsigned char *r = db_part(s, d->hash);   /* the slot's entity (0x4c), the record's level */
        int lvl = r ? ((r[5] | r[6] << 8) >> 5) & 7 : 0;
        part_cost(s, q, slot_ent(slot), lvl, own_key(slot, d->hash));
        if (!part_open(s, q->ent, lvl, q->okey)) continue;
        if (cur && d->name == cur) s->part_sel = s->nparts;
        s->nparts++;
    }
    s->part_scroll = (float)s->part_sel;
    s->screen = SC_DPICK;
    printf("shop: decal slot %02x: %d decals (%d of perf brands not owned)\n", slot, s->nparts - 1, nbrand);
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
            else if (!setup_short(s, s->parts[s->part_sel].price))
                dialog(s, PK_DLG_S, 2, s->career ? 0x44b7167fu : 0xf5ac2b2au, DA_DECAL_ADD, 0, 0);
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
/* the database alone (no screens), loaded on first use and kept: the
   lookups below work without an open shop */
static Shop *g_db_only;
static const Shop *db_only(const char *dataroot) {
    if (!g_db_only) {
        g_db_only = (Shop *)calloc(1, sizeof *g_db_only);
        if (!g_db_only) return NULL;
        snprintf(g_db_only->dataroot, sizeof g_db_only->dataroot, "%s", dataroot);
        load_db(g_db_only);
    }
    return g_db_only;
}
unsigned shop_part_attrs(const char *dataroot, uint32_t part, const uint32_t *keys, int n, uint32_t *vals) {
    const Shop *db = db_only(dataroot);
    if (!db) return 0;
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
static int rec_model(const Shop *db, const unsigned char *r, int model, const char *car, char *out, int cap);
int shop_part_model(const char *dataroot, uint32_t part, int model, char *out, int cap) {
    const Shop *db = db_only(dataroot);
    if (!db) return 0;
    return rec_model(db, db_part(db, part), model, NULL, out, cap);
}
/* car: the car type whose own models (e.g. its engine styles) may be named, NULL generic models only */
static int rec_model(const Shop *db, const unsigned char *r, int model, const char *car, char *out, int cap) {
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
    static const char *const names[] = { "AUDIO", "WHEELS", "SPOILER", "SPOILER_HATCH", "SPOILER_SUV", "EXHAUST", "ROOF", "BRAKES" };
    uint32_t mh = n2_u32(db->db + db->db_models + 4L * r[7]);
    for (int i = 0; i < NELEM(names); i++)
        if (feng_hash(names[i]) == mh) snprintf(lib, sizeof lib, "%s", names[i]);
    if (!lib[0] && car && feng_hash(car) == mh) snprintf(lib, sizeof lib, "%s", car);
    if (!lib[0]) return 0;
    snprintf(out, (size_t)cap, "%s%s%s", lib, (const char *)db->db + db->db_str + base * 4L,
             (const char *)db->db + db->db_str + nm * 4L);
    return 1;
}
/* ---------------------------------------------------------------- preset rides */

/* The preset rides (FEPresetCar, GLOBALB chunk 0x30220, 0x338 bytes each):
   +8 the model (the car type's name), +0x28 the preset's name (found by its
   FEHashUpper, FindFEPresetCar 0x61c460), +0x48 a performance level (-1:
   the per-part flags +0x2f4), +0x4c a part hash per car slot 0..0xa9
   (0 none, 1 leave the part there). */
#define PRESET_SIZE 0x338
#define PRESET_SLOTS 0xaa
/* The database group a car slot takes (the CarSlotId names: 0x1b / 0x1c
   FRONT / REAR_BRAKE, 0x1d / 0x1e FRONT / REAR_WHEEL, 0x20 WING_MIRROR,
   0x21 LICENSE_PLATE, 0x22 TRUNK_AUDIO, 0x23.. TRUNK_AUDIO_COMP_n, 0x34..
   0x3e the decal locations, 0x3f BASE_PAINT, 0x40.. VINYL_LAYERn, 0x44..
   0x4c PAINT_*, 0x4d.. VINYL_COLOURl_c, 0x5d.. KIT / HOOD / DOOR / TRUNK_
   CARBON, 0x61.. DECAL_*_TEXn, 0x99 WINDOW_TINT, 0x9a.. NEON*, 0x9e
   CABIN_NEON_FRAME, 0x9f HEADLIGHT_BULB, 0xa0 DOOR_STYLE, 0xa1 HYDRAULICS,
   0xa2 NOS_PURGE, 0xa3 CUSTOM_HUD, 0xa4.. HUD_BACKING / NEEDLE / CHARACTER_
   COLOUR): parts that share a hash (e.g. every "STOCK") are told apart by
   it, as NewGetCarPart(type, slot, hash) only looks at the slot's parts.
   -1: any. */
static int preset_slot_group(int slot) {
    if (slot <= 0x1a) return slot;
    if (slot <= 0x1c) return 27;                       /* BRAKE */
    if (slot <= 0x1e) return G_WHEEL;
    if (slot == 0x20) return G_LEFT_MIRROR;
    if (slot == 0x21) return 31;
    if (slot == 0x22) return G_TRUNK_AUDIO;
    if (ice_comp_slot(slot)) return 61 + ice_size(slot);
    if (slot >= 0x34 && slot <= 0x3e) return slot - 0x34 + 37;
    if (slot == 0x3f || (slot >= 0x44 && slot <= 0x4c)) return 48;   /* PAINT */
    if (slot >= 0x40 && slot <= 0x43) return 50;       /* VINYL */
    if (slot >= 0x4d && slot <= 0x5c) return 49;       /* VINYL_PAINT */
    if (slot >= 0x5d && slot <= 0x60) return 51;       /* CARBON_FIBRE */
    if (slot >= 0x61 && slot <= 0x98) return 52;       /* DECAL_TEXTURE */
    if (slot == 0x99) return G_WINDOW_TINT;
    if (slot >= 0x9a && slot <= 0x9d) return G_NEON;
    if (slot >= 0x9e && slot <= 0xa2) return 55 + slot - 0x9e;   /* CABIN_NEON_FRAME .. NOS_PURGE */
    if (slot == 0xa3) return G_CUSTOM_HUD;
    if (slot >= 0xa4 && slot <= 0xa6) return G_CUSTOM_HUD_PAINT;
    if (slot == 0xa9) return 68;
    return -1;
}
/* the database part a preset puts in a slot (NewGetCarPart 0x61b980): NULL
   when the hash names no part of the slot's group -- the game then fits
   nothing there, as for hash 0 */
static const unsigned char *preset_part(const Shop *db, int slot, uint32_t hash) {
    int grp = preset_slot_group(slot);
    for (long q = db->db_parts; db->db && q + 14 <= db->db_parts + db->db_parts_len; q += 14) {
        const unsigned char *r = db->db + q;
        if (!n2_u32(r) && !(r[8] | r[9] << 8)) break;
        if (n2_u32(r) == hash && (grp < 0 || r[4] == grp)) return r;
    }
    return NULL;
}
static const char *preset_part_name(const Shop *db, const unsigned char *r) {
    return db->db_str ? (const char *)db->db + db->db_str + (r[8] | r[9] << 8) * 4L : "?";
}
/* a part our N2CarConfig has no place for: said when it is not the stock
   one (an upgrade level above 0) */
static void preset_unported(const Shop *db, const char *preset, int slot, const unsigned char *r) {
    if (r && (r[5] >> 5 & 7) > 0)
        printf("shop: preset %s: slot 0x%02x %s not ported\n", preset, slot, preset_part_name(db, r));
}
/* the rim folder of a BRAND_NAME (the Rims brands, then SPINNER), NULL none */
static const char *preset_rim_folder(uint32_t brand) {
    if (brand == 0xc8d1a75eu) return SPINNER_FOLDER;
    for (int i = 0; i < NELEM(k_rims_tuner); i++) if (k_rims_tuner[i].folder && k_rims_tuner[i].key == brand) return k_rims_tuner[i].folder;
    for (int i = 0; i < NELEM(k_rims_suv); i++) if (k_rims_suv[i].folder && k_rims_suv[i].key == brand) return k_rims_suv[i].folder;
    return NULL;
}

/* one slot of a RideInfo (its part record r, NULL none) into our
   representation, as shop_preset_ride documents it; shared by the preset
   and the random rides */
typedef struct {
    Shop *db; const char *car, *name; int car_model, wheels;
    const unsigned char *loc_part[11]; uint32_t front_wheel;
    char rim_folder[16]; int rim_style;
} RideFill;
static void ride_fill_init(RideFill *f, Shop *db, const char *car, const char *name, N2CarConfig *cfg) {
    memset(f, 0, sizeof *f);
    f->db = db; f->car = car; f->name = name;
    f->car_model = db_model(db, car); f->wheels = db_model(db, "WHEELS");
    snprintf(f->rim_folder, sizeof f->rim_folder, "%s", STOCK_RIM_FOLDER); f->rim_style = STOCK_RIM_STYLE;
    vinyl_db_load(db->db, db->dblen);   /* the decal table */
    memset(cfg, 0, sizeof *cfg);
}
static void ride_fill_slot(RideFill *f, N2CarConfig *cfg, int slot, const unsigned char *r) {
    uint32_t ph = r ? n2_u32(r) : 0;
    unsigned aoff = r ? (unsigned)(r[10] | r[11] << 8) : 0xffffu;
    int upg = r ? r[5] | r[6] << 8 : 0;
    int pi = -1;
    for (int i = 0; i < N2_PAINT_COUNT; i++) if (k_paint_cat[i].group == slot) pi = i;
    if (pi >= 0) { cfg->paint[pi] = ph; return; }
    if (slot >= 0x40 && slot <= 0x43) { cfg->vinyl[slot - 0x40] = ph; return; }
    if (slot >= 0x4d && slot <= 0x5c) { cfg->vinyl_col[(slot - 0x4d) / 4][(slot - 0x4d) % 4] = ph; return; }
    if (slot >= 0x34 && slot <= 0x3e) { f->loc_part[slot - 0x34] = r; return; }
    if (slot >= 0x61 && slot <= 0x98) {
        uint32_t nm = 0;
        for (int i = 0; r && i < decal_count(); i++) if (decal_at(i)->hash == ph) nm = decal_at(i)->name;
        if (r && !nm) printf("shop: preset %s: decal %s has no texture\n", f->name, preset_part_name(f->db, r));
        cfg->decal[decal_loc_of(slot)][decal_n_of(slot)] = nm;
        return;
    }
    if (slot >= 0x9a && slot <= 0x9d) {
        uint32_t col[2] = { 0, 0 };
        if (r) neon_value(f->db, aoff, col);
        cfg->neon[slot - 0x9a][0] = col[0]; cfg->neon[slot - 0x9a][1] = col[1];
        return;
    }
    if (slot == 0x22 || ice_comp_slot(slot) || (slot >= 0xa0 && slot <= 0xa2)) {
        if (r) {
            ShopPart q; memset(&q, 0, sizeof q);
            q.hash = ph; q.value = spec_value(f->db, slot, r);
            spec_put(f->db, cfg, slot, &q);
        } else if (slot == 0x22) cfg->parts[N2_PART_AUDIO] = 0;
        else if (slot == 0xa0) cfg->doors = 0;
        else if (slot == 0xa1) cfg->hydraulics = 0;
        else if (slot == 0xa2) cfg->purge = 0;
        else cfg->audio_comp[slot - 0x23] = 0;
        return;
    }
    switch (slot) {
    case 1: case 2: case 6: case 7: case 9: case 0xb: case 0xc: case 0xe: case 0xf: case 0x10: case 0x20: {
        int group = preset_slot_group(slot), v = 0;
        if (r) {
            v = part_value_lib(f->car, group, ph, r[7] != f->car_model, upg);
            if (v < 0) { printf("shop: preset %s: slot 0x%02x %s has no part value\n", f->name, slot, preset_part_name(f->db, r)); v = 0; }
        }
        body_put(cfg, group_slot(group), v, slot == 9 && r && db_has_attr(f->db, aoff, 0x721AFF7Cu));   /* CARBONFIBRE */
        break;
    }
    case 0x1d: {
        f->front_wheel = ph;
        uint32_t brand = 0;
        const char *folder = r && r[7] == f->wheels && db_attr(f->db, aoff, 0xEBB03E66u, &brand) ? preset_rim_folder(brand) : NULL;
        int st, in, out;
        if (folder && rim_part_parse(folder, ph, &st, &in, &out)) {
            snprintf(f->rim_folder, 16, "%s", folder); f->rim_style = RIM_VALUE(st, in);
        } else {
            if (r && r[7] == f->wheels) printf("shop: preset %s: rim %s not found\n", f->name, preset_part_name(f->db, r));
            snprintf(f->rim_folder, 16, "%s", STOCK_RIM_FOLDER); f->rim_style = STOCK_RIM_STYLE;
        }
        break;
    }
    case 0x1e:
        if (ph != f->front_wheel) preset_unported(f->db, f->name, slot, r);
        break;
    case 0x5d: case 0x60: {
        unsigned bit = slot == 0x5d ? 2u : 4u;
        if (r && ph != 0xc8d313acu) cfg->carbon |= bit; else cfg->carbon &= ~bit;   /* CARBON FIBRE NONE */
        break;
    }
    case 0x99: {
        uint32_t mat = 0;
        cfg->tint = r && db_attr(f->db, aoff, 0x6ba02c05u, &mat) ? tint_value(mat) : 0;
        break;
    }
    case 0x9f: cfg->bulb = r ? bulb_value(f->db, aoff) : 0; break;
    case 0xa3: {
        uint32_t idx = 0;
        if (r) db_attr(f->db, aoff, 0x311151f8u, &idx);   /* HUDINDEX */
        cfg->hud = idx;
        break;
    }
    case 0xa4: case 0xa5: case 0xa6:   /* backing, needle, character (the dial) */
        cfg->hud_col[slot == 0xa4 ? 0 : slot == 0xa6 ? 1 : 2] = r ? hud_paint_value(f->db, aoff) : 0;
        break;
    case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0xa9:
        break;   /* the wide body's */
    default:
        preset_unported(f->db, f->name, slot, r);
        break;
    }
}
static void ride_fill_done(RideFill *f, N2CarConfig *cfg) {
    /* the decal layouts, from the locations in use (a wide body's doors and
       quarters have their own) */
    int wide = cfg->parts[N2_PART_WIDE] > 1;
    for (int loc = 0; loc < 7; loc++) {
        const unsigned char *r = f->loc_part[loc >= 3 && wide ? loc + 4 : loc];
        int small = r && (r[5] & 0x1f) == 1;
        cfg->decal_layout[loc] = (unsigned char)(small ? 2 : r && loc >= 5 ? 1 : 0);
    }
}

/* RideInfo::FillWithPreset (0x637270, GC 80245bf8) into our representation:
   from a never-customized car of the preset's model (all zero, the stock
   rims -- Init + SetStockParts when the type changes) every slot takes its
   part (hash 0 none, 1 the stock one, else the part of that hash), the way
   the Car Shop's own install of that part writes it:
     body parts (1, 2, 6, 7, 0xb, 0xc, 0xe..0x10, 0x20): part_value, a
       library's "none" the stock look (build_parts); the hood (9) carbon
       with a CARBONFIBRE part (parts_cursor / spec_put);
     rims (0x1d FRONT_WHEEL): the brand's folder and RIM_VALUE(STYLEnn,
       inner) of <FOLDER>_STYLEnn_<inner>_<outer> (build_rims / the size
       list; SPINNER as the Spinners list); the car's own wheel = Stock;
     paint (0x3f, 0x44..0x4c): paint[] = the part (k_paint_cat order);
     vinyls (0x40..0x43) and their colours (0x4d..0x5c): the parts;
     carbon: KIT_CARBON (0x5d) bit 2, TRUNK_CARBON (0x60) bit 4 unless
       CARBON FIBRE NONE (cf_toggle);
     decals: a location (0x34..0x3e; on a wide body its own 0x3b..0x3e) its
       layout -- SMALL (upgrade 1) 2, a quarter panel's MEDIUM 1
       (decal_layout_take); a slot 0x61 + loc*8 + n the DECAL_TEXTURE
       part's NAME (dpick); 0x61..0x68 (the hood's) are never set by the PC
       build (0x6372fe), nor 0x19 (HOOD_UNDER);
     tint (0x99), neon (0x9a..0x9d), bulb (0x9f), custom HUD + colours
       (0xa3..0xa6): tint_value / neon_value / bulb_value / HUDINDEX /
       hud_paint_value;
     doors / hydraulics / purge / trunk layout / components (0xa0..0xa2,
       0x22..0x2e): spec_put (a layout first puts NO COMPONENTS everywhere);
     performance: +0x48 -1 -> InstallPerfPart(i, flag != 0) (0x599680) of
       each flag != -1 of +0x2f4 (0x44 of them), else
       InstallAllPerfParts(level, -1) (0x5a64a0): every category to it.
   Not ported (no place in N2CarConfig; printed when not the stock part):
   0 BASE, 3 / 4 LEFT / RIGHT_SIDE_MIRROR, 5 BODY, 8 TOP (always the type's
   default, 0x637346), 0xa TRUNK, 0xd ENGINE, 0x17 FENDER, 0x18 QUARTER,
   0x1a TRUNK_UNDER, 0x1b / 0x1c the brakes, 0x1e REAR_WHEEL (ours has one
   rim: said when it is not the front's), 0x1f, 0x21 LICENSE_PLATE,
   0x2f..0x33 DAMAGE_*, 0x48 PAINT_AUDIO, 0x5e HOOD_CARBON, 0x5f
   DOOR_CARBON, 0x9e CABIN_NEON_FRAME, 0xa7 / 0xa8. The wide body's doors,
   panels and sills (0x11..0x16) and its collision volume (0xa9) follow the
   wide body. A NULL body part (the game fits none) shows the stock one
   here; a part without geometry on the car is not checked (the parts
   browser hides those via n2_mod_catalog). */
int shop_preset_ride(const char *dataroot, uint32_t preset_hash, char car[32], N2CarConfig *cfg,
                     char rim_folder[16], int *rim_style) {
    static unsigned char *pre; static uint32_t npre;   /* chunk 0x30220, read once */
    Shop *db = (Shop *)db_only(dataroot);
    if (!db || !db->db) return 0;
    if (!pre) {
        char path[600];
        snprintf(path, sizeof path, "%s/GLOBAL/GLOBALB.BUN", dataroot);
        FILE *f = fopen(path, "rb");
        if (!f) return 0;
        uint32_t sz = 0;
        long at = find_chunk_in_file(f, 0x00030220u, &sz);
        if (at >= 0 && sz >= PRESET_SIZE && sz < (1u << 20) && (pre = (unsigned char *)malloc(sz)) != NULL) {
            if (fseek(f, at, SEEK_SET) == 0 && fread(pre, 1, sz, f) == sz) npre = sz / PRESET_SIZE;
            else { free(pre); pre = NULL; }
        }
        fclose(f);
        if (!pre) { printf("shop: no preset rides (GLOBALB 0x30220)\n"); return 0; }
    }
    const unsigned char *p = NULL;
    for (uint32_t i = 0; i < npre && !p; i++)   /* FindFEPresetCar 0x61c460 */
        if (feng_hash_upper((const char *)pre + i * PRESET_SIZE + 0x28) == preset_hash) p = pre + i * PRESET_SIZE;
    if (!p) return 0;
    char name[33];
    memcpy(name, p + 0x28, 32); name[32] = 0;
    memcpy(car, p + 8, 32); car[31] = 0;
    RideFill fill;
    ride_fill_init(&fill, db, car, name, cfg);
    for (int slot = 0; slot < PRESET_SLOTS; slot++) {
        uint32_t hash = n2_u32(p + 0x4c + 4 * slot);
        if (hash == 1) continue;                                        /* the stock part stays */
        if ((slot >= 0x61 && slot <= 0x68) || slot == 0x19) continue;   /* 0x6372fe: never set */
        if (slot == 8) continue;   /* 0x637346: TOP is always the slot's first part (the stock one) */
        ride_fill_slot(&fill, cfg, slot, hash ? preset_part(db, slot, hash) : NULL);
    }
    ride_fill_done(&fill, cfg);
    snprintf(rim_folder, 16, "%s", fill.rim_folder); *rim_style = fill.rim_style;
    /* performance */
    if (!perf_ready()) {
        char path[600]; long n = 0;
        snprintf(path, sizeof path, "%s/GLOBAL/GLOBALB.BUN", dataroot);
        unsigned char *g = n2_read_file(path, &n);
        if (g) { perf_init(g, n); free(g); }
    }
    int level = (int)n2_u32(p + 0x48);
    PerfParts pp; perf_clear(&pp);
    if (level == -1) {
        for (int i = 0; i < 0x44; i++) {
            signed char fl = (signed char)p[0x2f4 + i];
            if (fl != -1) perf_set_part(&pp, i, fl != 0);
        }
    } else perf_fit_level(&pp, -1, level);
    memcpy(cfg->perf, pp.fit, sizeof cfg->perf);
    printf("shop: preset %s = %s, rims %s %d, performance %d\n", name, car, rim_folder, *rim_style, level);
    return 1;
}

/* ---------------------------------------------------------------- random rides */

/* RideInfo::BuildRandomRide (0x6374b0, GC 802470a8) + BuildRandomSkin
   (0x632ec0, GC 8024748c), reached from SyncVisualPartsWithPhysics(1, 1)
   (0x6399d0) for the career's "RANDOM" opponents and the quick race's
   (re-tools/out/career/PRESET_OPPONENTS.md sections 5, 6). */

/* the part group of each CAR_SLOT_ID (0x802960, CarPartDatabase) */
static const unsigned char k_slot_group[PRESET_SLOTS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,   /* 0x00..0x1a */
    27, 27, 28, 28, 29, 30, 31,                                                                         /* 0x1b..0x21 */
    60, 61, 61, 62, 62, 63, 63, 61, 61, 61, 61, 62, 62,                                                 /* 0x22..0x2e */
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,                                     /* 0x2f..0x3e */
    48, 50, 50, 50, 50,                                                                                 /* 0x3f..0x43 */
    48, 48, 48, 48, 48, 48, 48, 48, 48,                                                                 /* 0x44..0x4c */
    49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49,                                     /* 0x4d..0x5c */
    51, 51, 51, 51,                                                                                     /* 0x5d..0x60 */
    52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
    52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52,   /* 0x61..0x98 */
    53, 54, 54, 54, 54, 55, 56, 57, 58, 59, 64, 65, 65, 65, 66, 67, 68 };                              /* 0x99..0xa9 */

/* RandomPartUpgrades (0x802c50, GC 80371694; NumRandomPartUpgrades 0x802dd8
   = 14): the slot, whether no drawn level means the slot's first level-0
   part (else none), and per row the chance in % of upgrade level 0..3. The
   row is L (the mean level), path 9 for the kit / roof scoop / spoiler and
   path 3 (engine) for the exhaust */
typedef struct { unsigned char slot, first; signed char odds[4][4]; } RandomPartUpgrade;
#define RPU_STD { { 60, 30, 10, 0 }, { 20, 60, 20, 0 }, { 0, 20, 60, 20 }, { 0, 10, 20, 70 } }
static const RandomPartUpgrade k_random_part_upgrades[14] = {
    { 0x09, 1, RPU_STD },   /* HOOD */
    { 0x07, 1, RPU_STD },   /* ROOF (scoop) */
    { 0x0c, 1, RPU_STD },   /* SPOILER */
    { 0x10, 1, RPU_STD },   /* EXHAUST */
    { 0x20, 1, RPU_STD },   /* WING_MIRROR */
    { 0x0f, 1, RPU_STD },   /* BRAKELIGHT */
    { 0x0e, 1, RPU_STD },   /* HEADLIGHT */
    { 0x9f, 1, RPU_STD },   /* HEADLIGHT_BULB */
    { 0x99, 0, RPU_STD },   /* WINDOW_TINT */
    { 0x3f, 1, { { 0, 70, 30, 0 }, { 0, 70, 20, 10 }, { 0, 20, 60, 20 }, { 0, 10, 20, 70 } } },     /* BASE_PAINT */
    { 0x49, 0, { { 70, 20, 10, 0 }, { 50, 30, 20, 0 }, { 20, 10, 60, 10 }, { 0, 10, 20, 70 } } },   /* rim paint */
    { 0x4a, 0, { { 0, 70, 30, 0 }, { 0, 70, 20, 10 }, { 0, 20, 60, 20 }, { 0, 10, 20, 70 } } },     /* spinner paint */
    { 0x9a, 0, { { 90, 10, 0, 0 }, { 40, 50, 10, 0 }, { 30, 20, 50, 0 }, { 30, 10, 30, 30 } } },    /* NEON */
    { 0x01, 0, RPU_STD },   /* FRONT_BUMPER (the kit) */
};
#undef RPU_STD
/* BuildRandomRide's rims, row by the tires level (path 8) */
static const signed char k_random_wheel_odds[4][4] = { { 70, 20, 10, 0 }, { 50, 40, 10, 0 }, { 10, 20, 50, 20 }, { 0, 10, 20, 70 } };
/* BuildRandomSkin: one full-body vinyl (groups 0x18, 0x1a, 0x1b, any level)
   in % by L, else layers 0..2 with these level odds (row L) and groups */
static const signed char k_random_vinyl_single[4] = { 0, 0, 5, 10 };
static const signed char k_random_vinyl_odds[3][4][4] = {
    { { 95, 0, 5, 0 }, { 90, 0, 10, 0 }, { 80, 0, 20, 0 }, { 65, 0, 35, 0 } },      /* layer 0: group 0x17 */
    { { 80, 10, 10, 0 }, { 70, 15, 10, 5 }, { 55, 25, 15, 5 }, { 40, 20, 20, 20 } }, /* layer 1: 19 groups */
    { { 95, 0, 0, 5 }, { 90, 0, 0, 10 }, { 80, 0, 0, 20 }, { 65, 0, 0, 35 } } };     /* layer 2: group 0x19 */
static const unsigned char k_random_vinyl_single_groups[3] = { 0x18, 0x1a, 0x1b };
static const unsigned char k_random_vinyl_groups1[19] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0x10, 0x11, 0x12, 0x13, 0x14 };
/* RandomVinylPaintJobs (0x8a43c8, GC 8040b21c; built at startup by
   0x77f520; NumRandomVinylJobs 0x802de0 = 5) */
static const char *const k_random_vinyl_jobs[5][4] = {
    { "VINYL_L1_COLOR01", "VINYL_L1_COLOR02", "VINYL_L2_COLOR11", "VINYL_L1_COLOR09" },
    { "VINYL_L1_COLOR01", "VINYL_L1_COLOR02", "VINYL_L1_COLOR03", "VINYL_L1_COLOR04" },
    { "VINYL_L1_COLOR02", "VINYL_L1_COLOR03", "VINYL_L1_COLOR04", "VINYL_L1_COLOR05" },
    { "VINYL_L1_COLOR02", "VINYL_L3_COLOR12", "VINYL_L3_COLOR13", "VINYL_L3_COLOR14" },
    { "VINYL_L1_COLOR02", "VINYL_L3_COLOR18", "VINYL_L3_COLOR19", "VINYL_L3_COLOR20" } };
/* SetStockParts' vinyl colours, every layer (0x637040) */
static const char *const k_stock_vinyl_colours[4] = { "VINYL_L1_COLOR09", "VINYL_L1_COLOR02", "VINYL_L2_COLOR11", "VINYL_L1_COLOR01" };
/* RandomCarPaintJobs (0x8a4418, GC 8040b154; built at startup by 0x77f2d0;
   NumRandomPaintJobs 0x802ddc = 5): a base paint, then the 5 colours its
   spoiler (0x45) and muffler tip (0x47) paints are drawn from */
static const char *const k_random_paint_jobs[5][6] = {
    { "GLOSS_L1_COLOR02", "GLOSS_L1_COLOR02", "GLOSS_L1_COLOR07", "GLOSS_L2_COLOR02", "GLOSS_L2_COLOR07", "GLOSS_L2_COLOR13" },
    { "GLOSS_L1_COLOR03", "GLOSS_L1_COLOR03", "GLOSS_L1_COLOR02", "GLOSS_L2_COLOR01", "GLOSS_L2_COLOR06", "GLOSS_L3_TEST07" },
    { "GLOSS_L2_COLOR01", "GLOSS_L2_COLOR01", "GLOSS_L2_COLOR02", "GLOSS_L2_COLOR07", "GLOSS_L2_COLOR11", "GLOSS_L2_COLOR03" },
    { "GLOSS_L2_COLOR17", "GLOSS_L2_COLOR17", "GLOSS_L1_COLOR02", "GLOSS_L1_COLOR04", "GLOSS_L2_COLOR01", "GLOSS_L2_COLOR07" },
    { "METAL_L2_COLOR01", "METAL_L2_COLOR01", "GLOSS_L1_COLOR02", "GLOSS_L1_COLOR07", "METAL_L2_COLOR05", "METAL_L3_TEST04" } };

/* bRandom 0x43c1c0: the old state mod n (0 when n <= 0), then the state
   steps either way (as airace.c's) */
static int rr_random(uint32_t *s, int n) {
    unsigned v = n > 0 ? *s % (unsigned)n : 0;
    uint32_t t = *s ^ 0x1d872b41u, u = t >> 5 ^ t;
    *s = u << 27 ^ u ^ t;
    return (int)v;
}
/* the first level j whose cumulative odds pass bRandom(100); 4 = none */
static int rr_roll(uint32_t *rng, const signed char odds[4]) {
    int r = rr_random(rng, 100), sum = 0, j;
    for (j = 0; j < 4; j++) { sum += odds[j]; if (r < sum) break; }
    return j;
}

/* GLOBALB 0x34607 (read by 0x6101e0): the two models of each slot (-1 the
   car itself, 0 none), then per-car overrides {car, slot, model0, model1};
   GLOBALB 0x34600: the car types (0x890 each after 0x11 alignment: +0 the
   name, +0xdc OUTER_RADIUS, +0xdd / +0xde the INNER_RADIUS range) */
static unsigned char *g_rr_models, *g_rr_types;
static uint32_t g_rr_models_len, g_rr_types_len;
static int rr_load(const char *dataroot) {
    if (g_rr_models && g_rr_types) return 1;
    char path[600];
    snprintf(path, sizeof path, "%s/GLOBAL/GLOBALB.BUN", dataroot);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    for (int k = 0; k < 2; k++) {
        uint32_t sz = 0, want = k ? 0x00034600u : 0x00034607u;
        long at = find_chunk_in_file(f, want, &sz);
        unsigned char *b = at >= 0 && sz > 0 && sz < (1u << 21) ? (unsigned char *)malloc(sz) : NULL;
        if (b && (fseek(f, at, SEEK_SET) != 0 || fread(b, 1, sz, f) != sz)) { free(b); b = NULL; }
        if (!b) continue;
        if (k) {
            uint32_t o = 0;
            while (o + 4 <= sz && n2_u32(b + o) == 0x11111111u) o += 4;
            memmove(b, b + o, sz - o); sz -= o;
            free(g_rr_types); g_rr_types = b; g_rr_types_len = sz;
        } else { free(g_rr_models); g_rr_models = b; g_rr_models_len = sz; }
    }
    fclose(f);
    if (!g_rr_models || !g_rr_types || g_rr_models_len < 8 * PRESET_SLOTS) {
        printf("shop: random rides: GLOBALB 0x34607 / 0x34600 missing\n");
        return 0;
    }
    return 1;
}
static const unsigned char *rr_type(const char *car) {
    for (uint32_t o = 0; o + 0x890 <= g_rr_types_len; o += 0x890)
        if (!strncmp((const char *)g_rr_types + o, car, 0x20)) return g_rr_types + o;
    return NULL;
}

typedef struct { Shop *db; uint32_t car_hash; const unsigned char *type; } RandRide;
#define RR_MAX 2048
/* 0x6101e0: the slot's two model hashes (0 none) */
static void rr_slot_models(const RandRide *c, int slot, uint32_t m[2]) {
    m[0] = n2_u32(g_rr_models + 8 * slot); m[1] = n2_u32(g_rr_models + 8 * slot + 4);
    for (uint32_t o = 8 * PRESET_SLOTS; o + 16 <= g_rr_models_len; o += 16)
        if (n2_u32(g_rr_models + o) == c->car_hash && (int)n2_u32(g_rr_models + o + 4) == slot) {
            m[0] = n2_u32(g_rr_models + o + 8); m[1] = n2_u32(g_rr_models + o + 12);
            break;
        }
    for (int i = 0; i < 2; i++) if (m[i] == 0xffffffffu) m[i] = c->car_hash;
}
/* GetCarPartDirect 0x61b670's test of one record: model, group, hash (0
   any), the model's exclusion switches (part_excluded), upgrade level
   (-1 any) */
static int rr_part_ok(const RandRide *c, const unsigned char *r, uint32_t model, int group, uint32_t hash, int level) {
    const Shop *s = c->db;
    if (r[7] >= s->nmodels || n2_u32(s->db + s->db_models + 4L * r[7]) != model) return 0;
    if (r[4] != group || (hash && n2_u32(r) != hash)) return 0;
    if (level != -1 && (r[5] >> 5) != level) return 0;
    if (part_excluded(s, r)) return 0;
    return 1;
}
/* NewGetCarPart 0x61b980 / NewGetNumCarParts 0x631a20: the slot's parts
   that pass (model 0's, then model 1's), up to max */
static int rr_parts(const RandRide *c, int slot, uint32_t hash, int level, const unsigned char **out, int max) {
    const Shop *s = c->db;
    uint32_t m[2];
    int n = 0;
    rr_slot_models(c, slot, m);
    for (int mi = 0; mi < 2; mi++) {
        if (!m[mi] || (mi && m[1] == m[0])) continue;
        for (long p = s->db_parts; p + 14 <= s->db_parts + s->db_parts_len && n < max; p += 14) {
            const unsigned char *r = s->db + p;
            if (!n2_u32(r) && !(r[8] | r[9] << 8)) break;
            if (rr_part_ok(c, r, m[mi], k_slot_group[slot], hash, level)) out[n++] = r;
        }
    }
    return n;
}
static const unsigned char *rr_first(const RandRide *c, int slot, uint32_t hash, int level) {
    const unsigned char *r = NULL;
    return rr_parts(c, slot, hash, level, &r, 1) ? r : NULL;
}
/* RideInfo::GetRandomPart 0x631cb0: one of the slot's parts of that level
   (no draw when there is none) */
static const unsigned char *rr_random_part(const RandRide *c, int slot, int level, uint32_t *rng) {
    static const unsigned char *t[RR_MAX];
    int n = rr_parts(c, slot, 0, level, t, RR_MAX);
    return n ? t[rr_random(rng, n)] : NULL;
}
/* GetRandomVinyl 0x61c4b0: a slot-0x40 vinyl of that level (-1 any) whose
   group (upgrade byte & 0x1f) is listed */
static const unsigned char *rr_random_vinyl(const RandRide *c, int level, const unsigned char *groups, int ng, uint32_t *rng) {
    static const unsigned char *t[RR_MAX], *u[RR_MAX];
    int n = rr_parts(c, 0x40, 0, level, t, RR_MAX), k = 0;
    for (int i = 0; i < n; i++)
        for (int g = 0; g < ng; g++) if ((t[i][5] & 0x1f) == groups[g]) { u[k++] = t[i]; break; }
    return k ? u[rr_random(rng, k)] : NULL;
}
/* GetRandomWheel 0x632de0: up to 128 FRONT_WHEEL parts of that level whose
   OUTER_RADIUS is the car type's (+0xdc) and INNER_RADIUS within
   +0xdd..+0xde; one of them */
static const unsigned char *rr_random_wheel(const RandRide *c, int level, uint32_t *rng) {
    static const unsigned char *t[RR_MAX], *u[128];
    int n = rr_parts(c, 0x1d, 0, level, t, RR_MAX), k = 0;
    for (int i = 0; i < n && k < 128; i++) {
        unsigned aoff = t[i][10] | t[i][11] << 8;
        uint32_t in = 0, out = 0;
        if (!db_attr(c->db, aoff, 0xeb0101e2u, &in) || !db_attr(c->db, aoff, 0xce7d8db5u, &out)) continue;   /* INNER / OUTER_RADIUS */
        if ((int)out == c->type[0xdc] && c->type[0xdd] <= (int)in && (int)in <= c->type[0xde]) u[k++] = t[i];
    }
    return k ? u[rr_random(rng, k)] : NULL;
}

Shop *shop_part_db(const char *dataroot) { return (Shop *)db_only(dataroot); }

/* SetUpgradePart 0x61bc80: the slot's first part (NewGetCarPart order) of
   that upgrade level, and its model name as shop_part_model's (generic
   libraries or the car's own models). 0 when there is none. */
int shop_upgrade_model(const char *dataroot, const char *car, int slot, int level, char *out, int cap) {
    const Shop *db = db_only(dataroot);
    if (!db || !db->db || !db->db_parts || !car || !rr_load(dataroot)) return 0;
    RandRide c = { (Shop *)db, feng_hash(car), rr_type(car) };
    const unsigned char *r = rr_first(&c, slot, 0, level);
    if (!r) return 0;
    if (rec_model(db, r, 0, car, out, cap)) return 1;
    if (r[6] == 0) {   /* no model record: the part hash is the model name's (BRAKE_STYLE01..03, <CAR>_KIT00_FRONT_BRAKE) */
        char nm[64];
        for (int k = 0; k < 10; k++) {
            snprintf(nm, sizeof nm, "BRAKE_STYLE%02d", k);
            if (feng_hash(nm) == n2_u32(r)) { snprintf(out, (size_t)cap, "%s", nm); return 1; }
        }
        snprintf(nm, sizeof nm, "%s_KIT00_FRONT_BRAKE", car);
        if (feng_hash(nm) == n2_u32(r)) { snprintf(out, (size_t)cap, "%s", nm); return 1; }
    }
    printf("shop: upgrade %s slot %#x level %d: part %08x model %u kind %u unnamed\n", car, slot, level, n2_u32(r), r[7], r[6]);
    return 0;
}

int shop_random_ride(Shop *s, const char *car, const int perf_lvl[10], uint32_t *rng, N2CarConfig *out,
                     char *rim, int rimsz, int *rim_style) {
    if (!s) s = g_db_only;
    if (!s || !s->db || !s->db_parts || !car || !rng || !out) return 0;
    if (!rr_load(s->dataroot)) return 0;
    RandRide c = { s, feng_hash(car), rr_type(car) };
    if (!c.type) { printf("shop: random ride: no car type %s\n", car); return 0; }
    /* the RideInfo after Init + SetStockParts (0x637040): a slot not
       touched below stays stock (our zero) */
    const unsigned char *part[PRESET_SLOTS];
    unsigned char touched[PRESET_SLOTS];
    memset(part, 0, sizeof part); memset(touched, 0, sizeof touched);
    int lv[10];
    for (int i = 0; i < 10; i++) lv[i] = perf_lvl ? (perf_lvl[i] < 0 ? 0 : perf_lvl[i] > 3 ? 3 : perf_lvl[i]) : 0;
    /* SyncVisualPartsWithPhysics re-matches path 9 (aero / the body kit)
       with MatchUpgradeLevel (0x5a6880) to the mean normalized level of
       paths 8 tires, 5 suspension and 0 weight before BuildRandomRide.
       guess: path 9 = floor of that mean */
    lv[9] = (lv[8] + lv[5] + lv[0]) / 3;
    int sum = 0;
    for (int i = 0; i < 10; i++) sum += lv[i];
    int L = (int)(sum * 0.1f);   /* (int)(0.1 * the sum of the 10 levels) */
    if (L > 3) L = 3;
    /* RandomPartUpgrades */
    for (int e = 0; e < 14; e++) {
        const RandomPartUpgrade *u = &k_random_part_upgrades[e];
        int row = u->slot == 1 || u->slot == 7 || u->slot == 0xc ? lv[9] : u->slot == 0x10 ? lv[3] : L;
        int j = rr_roll(rng, u->odds[row]);
        part[u->slot] = j < 4 ? rr_random_part(&c, u->slot, j, rng) : u->first ? rr_first(&c, u->slot, 0, 0) : NULL;
        touched[u->slot] = 1;
    }
    /* the kit follows the front bumper: its KITnn rear bumper and skirt,
       when the car has both */
    if (!part[1]) {
        part[1] = rr_first(&c, 1, 0, 0); part[2] = rr_first(&c, 2, 0, 0); part[0xb] = rr_first(&c, 0xb, 0, 0);
        touched[2] = touched[0xb] = 1;
    } else {
        char nm[96];
        int kit = part[1][5] & 0x1f;
        snprintf(nm, sizeof nm, "%s_KIT%.2d_REAR_BUMPER", car, kit);
        const unsigned char *rear = rr_first(&c, 2, feng_hash(nm), -1);
        snprintf(nm, sizeof nm, "%s_KIT%.2d_SKIRT", car, kit);
        const unsigned char *skirt = rr_first(&c, 0xb, feng_hash(nm), -1);
        if (rear && skirt) { part[2] = rear; part[0xb] = skirt; touched[2] = touched[0xb] = 1; }
    }
    /* the rims, by the tires level, sized for the car */
    {
        int j = rr_roll(rng, k_random_wheel_odds[lv[8]]);
        const unsigned char *w = j < 4 ? rr_random_wheel(&c, j, rng) : NULL;
        if (w) { part[0x1d] = part[0x1e] = w; touched[0x1d] = touched[0x1e] = 1; }
    }
    /* BuildRandomSkin: SetStockParts' vinyl colours first */
    for (int l = 0; l < 4; l++)
        for (int j = 0; j < 4; j++) {
            int slot = 0x4d + 4 * l + j;
            part[slot] = rr_first(&c, slot, feng_hash(k_stock_vinyl_colours[j]), -1);
            touched[slot] = 1;
        }
    touched[0x40] = touched[0x41] = touched[0x42] = touched[0x43] = 1;
    if (rr_random(rng, 100) < k_random_vinyl_single[L]) {
        const unsigned char *v = rr_random_vinyl(&c, -1, k_random_vinyl_single_groups, 3, rng);
        if (v) part[0x40] = v;
    } else {
        static const unsigned char g0 = 0x17, g2 = 0x19;
        /* guess: a layer whose drawn level has no vinyl of its groups (level 0: none exist) stays empty */
        int j = rr_roll(rng, k_random_vinyl_odds[0][L]);
        part[0x40] = j < 4 ? rr_random_vinyl(&c, j, &g0, 1, rng) : NULL;
        j = rr_roll(rng, k_random_vinyl_odds[1][L]);
        part[0x41] = j < 4 ? rr_random_vinyl(&c, j, k_random_vinyl_groups1, 19, rng) : NULL;
        j = rr_roll(rng, k_random_vinyl_odds[2][L]);
        part[0x42] = j < 4 ? rr_random_vinyl(&c, j, &g2, 1, rng) : NULL;
        for (int l = 0; l < 3; l++) {
            int perm[4] = { 0, 1, 2, 3 };
            for (int i = 0; i < 4; i++) { int k = rr_random(rng, 4), t = perm[k]; perm[k] = perm[i]; perm[i] = t; }
            int set = rr_random(rng, 5);
            for (int k = 0; k < 4; k++) {
                int slot = 0x4d + 4 * l + k;
                part[slot] = rr_first(&c, slot, feng_hash(k_random_vinyl_jobs[set][perm[k]]), -1);
            }
        }
    }
    /* the base paint drawn again (RandomPartUpgrades[9]'s odds) */
    {
        const RandomPartUpgrade *u = &k_random_part_upgrades[9];
        int j = rr_roll(rng, u->odds[L]);
        part[0x3f] = j < 4 ? rr_random_part(&c, 0x3f, j, rng) : rr_first(&c, 0x3f, 0, 0);
    }
    /* RandomCarPaintJobs: a listed base paint gets its spoiler and muffler tip paints */
    for (int e = 0; part[0x3f] && e < 5; e++) {
        if (n2_u32(part[0x3f]) != feng_hash(k_random_paint_jobs[e][0])) continue;
        int a = rr_random(rng, 5), b = rr_random(rng, 5);
        part[0x45] = rr_first(&c, 0x45, feng_hash(k_random_paint_jobs[e][1 + a]), -1);
        part[0x47] = rr_first(&c, 0x47, feng_hash(k_random_paint_jobs[e][1 + b]), -1);
        touched[0x45] = touched[0x47] = 1;
        break;
    }
    /* into our representation, as shop_preset_ride's slots */
    RideFill fill;
    ride_fill_init(&fill, s, car, "(random)", out);
    for (int slot = 0; slot < PRESET_SLOTS; slot++)
        if (touched[slot]) ride_fill_slot(&fill, out, slot, part[slot]);
    ride_fill_done(&fill, out);
    if (rim && rimsz > 0) snprintf(rim, (size_t)rimsz, "%s", fill.rim_folder);
    if (rim_style) *rim_style = fill.rim_style;
    return 1;
}

/* ---------------------------------------------------------------- a saved ride (PC save import) */

/* the car type (GLOBALB 0x34600) whose name's bStringHash is h */
static int rr_type_by_hash(uint32_t h, char car[32]) {
    for (uint32_t o = 0; o + 0x890 <= g_rr_types_len; o += 0x890) {
        char nm[33];
        memcpy(nm, g_rr_types + o, 32); nm[32] = 0;
        if (nm[0] && feng_hash(nm) == h) { if (car) snprintf(car, 32, "%s", nm); return 1; }
    }
    return 0;
}
int shop_car_type_name(const char *dataroot, uint32_t hash, char car[32]) {
    return rr_load(dataroot) && rr_type_by_hash(hash, car);
}
int shop_car_type_at(const char *dataroot, int i, char car[32]) {
    if (i < 0 || !rr_load(dataroot) || (uint32_t)(i + 1) * 0x890 > g_rr_types_len) return 0;
    memcpy(car, g_rr_types + i * 0x890, 31); car[31] = 0;
    return car[0] != 0;
}

/* PresetCarSlot (0x418, GC ctor 80134bfc; FillRideWith 80134e24 /
   FillWithRide 80134f38): +0 1 = filled, +8 the car type's name hash, +0xc
   the part hash of each CarSlot 0..0xa9 (0 none), +0x2b4 the
   PhysicsUpgradeSpecification (0x94: +0 float levels[10], +0x28 int junkman
   [10], +0x50 the 0x44 perf parts' installed bytes), +0x348 the tunings.
   FillRideWith: Init(type), SetStockParts, then SetPart of every slot with
   GetInstalledPart = NewGetCarPart(type, slot, hash) (0: NULL) -- each slot
   into our representation as ride_fill_slot does for the presets. */
int shop_slot_ride(const char *dataroot, const unsigned char *ps, char car[32], N2CarConfig *cfg,
                   char rim_folder[16], int *rim_style) {
    Shop *db = (Shop *)db_only(dataroot);
    if (!db || !db->db || !db->db_parts || !ps || !n2_u32(ps) || !rr_load(dataroot)) return 0;
    if (!rr_type_by_hash(n2_u32(ps + 8), car)) { printf("shop: saved ride: no car type %08x\n", (unsigned)n2_u32(ps + 8)); return 0; }
    RandRide c = { db, feng_hash(car), rr_type(car) };
    RideFill fill;
    ride_fill_init(&fill, db, car, car, cfg);
    for (int slot = 0; slot < PRESET_SLOTS; slot++) {
        uint32_t h = n2_u32(ps + 0xc + 4 * slot);
        const unsigned char *r = h ? rr_first(&c, slot, h, -1) : NULL;
        if (h && !r) printf("shop: saved %s: slot 0x%02x part %08x not in the database\n", car, slot, (unsigned)h);
        ride_fill_slot(&fill, cfg, slot, r);
    }
    ride_fill_done(&fill, cfg);
    snprintf(rim_folder, 16, "%s", fill.rim_folder); *rim_style = fill.rim_style;
    if (!perf_ready()) {
        char path[600]; long n = 0;
        snprintf(path, sizeof path, "%s/GLOBAL/GLOBALB.BUN", dataroot);
        unsigned char *g = n2_read_file(path, &n);
        if (g) { perf_init(g, n); free(g); }
    }
    PerfParts pp; perf_clear(&pp);
    for (int i = 0; i < 0x44; i++) if (ps[0x2b4 + 0x50 + i]) perf_set_part(&pp, i, 1);
    memcpy(cfg->perf, pp.fit, sizeof cfg->perf);
    for (int i = 0; i < 10; i++) cfg->junk[i] = n2_u32(ps + 0x2b4 + 0x28 + 4 * i) != 0;
    return 1;
}

/* MapCarPartToUnlockable 8012e8bc: a part's unlockable entity. The vinyl
   slots by the vinyl's group (upgrade byte & 0x1f); carbon fibre (attribute
   CARBONFIBRE) hood / roof / spoiler / mirrors their own entities (other
   slots 0); the spinners (BRAND_NAME SPINNER) 0x21, the split hoods
   (group 5) 0x22; else MapCarSlotToUnlockable. Slot 0x3f (by the paint's
   kind) is never owned, so not mapped here. */
static int part_unlockable(const Shop *s, int slot, const unsigned char *r) {
    static const unsigned char vinyl_ent[0x1c] = {
        0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
        0x38, 0x39, 0x3a, 0x3b, 0x41, 0x42, 0x43, 0x3c, 0x3d, 0x40, 0x3e, 0x37, 0x3f, 0x44 };
    unsigned aoff = r[10] | r[11] << 8;
    int g = r[5] & 0x1f;
    uint32_t v = 0;
    if (slot >= 0x40 && slot <= 0x43) return g < 0x1c ? vinyl_ent[g] : 0;
    if (db_attr(s, aoff, 0x721AFF7Cu, &v) && v)   /* CARBONFIBRE */
        return slot == 9 ? 0x4b : slot == 7 ? 0x46 : slot == 0xc ? 0x4a : slot == 0x20 ? 0x45 : 0;
    v = 0;
    if (slot == 0x1d && db_attr(s, aoff, 0xEBB03E66u, &v) && v == 0xc8d1a75eu) return 0x21;
    if (slot == 9 && g == 5) return 0x22;
    return slot_ent(slot);
}
/* OwnedPartInventory (0x3c0 as saved, 0x503f00): the entity words
   (0x57), the perf bits +0x15c, the regions -- rims 0x1d / 0x1e +0x16c
   (0x100 bytes), roof 7 +0x26c (0x10), spoiler 0xc +0x27c (0x10), mirrors
   0x20 +0x28c (8) -- the spent cash +0x294, the hood 9 region +0x298 (8),
   the brands +0x2a0 (0x44), slot 0xa2's region +0x3b0 (0x10). */
#define INV_SAVED_SIZE 0x3c0
static int inv_region(int slot, int *size) {
    switch (slot) {
    case 0x1d: case 0x1e: *size = 0x100; return 0x16c;
    case 7: *size = 0x10; return 0x26c;
    case 0xc: *size = 0x10; return 0x27c;
    case 0x20: *size = 8; return 0x28c;
    case 9: *size = 8; return 0x298;
    case 0xa2: *size = 0x10; return 0x3b0;
    default: *size = 0; return -1;
    }
}
/* the position of r in the NewGetFirstCarPart / Next list of (type, slot, any
   hash, any level), NewGetPartIndex 80243eb8; -1 not in it */
static int rr_index(const RandRide *c, int slot, const unsigned char *r) {
    static const unsigned char *t[RR_MAX];
    int n = rr_parts(c, slot, 0, -1, t, RR_MAX);
    for (int i = 0; i < n; i++) if (t[i] == r) return i;
    return -1;
}
/* OwnsCarPart 801358bc of every part the car can take: GetWordToSetBitFor
   (PC 0x503b10, GC 80135610) -- the part's index in its car type's list
   (GetCarTypeNameHash of the part: the car for its own parts, else no type),
   a region's word idx >> 5 bit idx & 31, else the entity's word bit idx
   (the PC's shl: idx & 31). The owned parts' keys (own_key: the name hash)
   go to own[]; fitted (a PresetCarSlot, may be NULL) is checked against
   them: *fit_n of its owned-able parts above level 0, *fit_owned owned. */
int shop_inv_owned(const char *dataroot, const char *car, const unsigned char *inv, const unsigned char *fitted,
                   uint32_t *own, int max, int *fit_n, int *fit_owned) {
    Shop *db = (Shop *)db_only(dataroot);
    int nown = 0;
    if (fit_n) *fit_n = 0;
    if (fit_owned) *fit_owned = 0;
    if (!db || !db->db || !db->db_parts || !inv || !car || !rr_load(dataroot)) return 0;
    RandRide cc = { db, feng_hash(car), rr_type(car) };
    /* a part of no car type (the shared libraries: GetCarTypeInfoFromHash
       NULL) is indexed with type 0x2e, one past the 46 car types: 0x6101e0
       then finds no per-car override and the slot's default models, whose
       "the car itself" (-1) becomes a hash no part model has */
    RandRide cn = { db, 0, NULL };
    static const unsigned char *t[RR_MAX];
    for (int slot = 0; slot < PRESET_SLOTS; slot++) {
        if (!own_key(slot, 1)) continue;   /* never owned */
        int n = rr_parts(&cc, slot, 0, -1, t, RR_MAX);
        uint32_t fh = fitted ? n2_u32(fitted + 0xc + 4 * slot) : 0;
        for (int i = 0; i < n; i++) {
            const unsigned char *r = t[i];
            uint32_t model = r[7] < db->nmodels ? n2_u32(db->db + db->db_models + 4L * r[7]) : 0;
            int idx = model == cc.car_hash ? i : rr_type_by_hash(model, NULL) ? -2 : rr_index(&cn, slot, r);
            if (idx == -2) {   /* another car type's part */
                char other[32] = "";
                rr_type_by_hash(model, other);
                RandRide co = { db, model, rr_type(other) };
                idx = rr_index(&co, slot, r);
            }
            if (idx < 0) continue;
            int size = 0, reg = inv_region(slot, &size);
            uint32_t w;
            int ent = reg >= 0 ? -1 : part_unlockable(db, slot, r);
            if (reg >= 0) {
                if ((idx >> 5) * 4 + 4 > size) continue;
                w = n2_u32(inv + reg + (idx >> 5) * 4);
            } else {
                if (ent < 0 || ent >= 0x57) continue;
                w = n2_u32(inv + 4 * ent);
            }
            int owned = (w >> (idx & 31)) & 1;
            uint32_t key = own_key(slot, n2_u32(r));
            if (fh && n2_u32(r) == fh && (r[5] >> 5) > 0 && ent != 0) {   /* an upgrade with a price entity */
                if (fit_n) (*fit_n)++;
                if (owned && fit_owned) (*fit_owned)++;
            }
            if (!owned || !key) continue;
            int have = 0;
            for (int k = 0; k < nown && !have; k++) have = own[k] == key;
            if (!have && nown < max) own[nown++] = key;
        }
    }
    return nown;
}

unsigned shop_purge_preview(Shop *s) {
    return s && s->screen == SC_SPEC && s->spec_open && s->spec_slot == 0xa2 ? s->preview.purge : 0;
}
/* 0x61f820: the hood part's upgrade group 5 = split, VERTSPLIT (attribute
   0x7a8780) nonzero = front/back (4) else left/right (2); flag: the part is
   <CAR>_STYLE22_HOOD or _CF (0x7a8770 / 0x7a875c seeded with the car hash) */
static void hood_anim_style(const Shop *s, const char *car, int hv, int *style, int *flag) {
    static int cache_v = -1, cache_style, cache_flag; static char cache_car[32];
    *style = 0; *flag = 0;
    if (hv <= 0 || !s || !s->db) return;
    if (hv != cache_v || strcmp(cache_car, car)) {
        int model = db_model(s, car);
        cache_v = hv; snprintf(cache_car, sizeof cache_car, "%s", car);
        cache_style = 0; cache_flag = 0;
        for (long p = s->db_parts; model >= 0 && p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
            const unsigned char *r = s->db + p;
            uint32_t hash = n2_u32(r);
            unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
            if (!hash && !soff) break;
            if (r[4] != G_HOOD || r[7] != model || ((r[5] | r[6] << 8) & 0x1f) != 5) continue;
            if (part_value(car, G_HOOD, hash) != hv) continue;
            uint32_t vs = 0;
            cache_style = db_attr(s, aoff, 0x3da5adacu, &vs) && vs ? 4 : 2;   /* VERTSPLIT */
            char nm[64]; snprintf(nm, sizeof nm, "%s_STYLE22_HOOD", car);
            cache_flag = hash == feng_hash(nm) || hash == feng_hash(strcat(nm, "_CF"));
            break;
        }
    }
    *style = cache_style; *flag = cache_flag;
}
/* 0x61f820's styles for a fitted car (no shop: the database alone), as
   shop_parts_open gives them */
void shop_car_part_anim_styles(const char *dataroot, const char *car, const N2CarConfig *cfg,
                               int *hood_style, int *hood_flag, int *door_style) {
    *hood_style = 0; *hood_flag = 0; *door_style = 0;
    if (!cfg) return;
    hood_anim_style(db_only(dataroot), car, cfg->parts[N2_PART_HOOD], hood_style, hood_flag);
    *door_style = (int)cfg->doors;
    if (*door_style == 1 && cfg->parts[N2_PART_WIDE] > 1) *door_style = cfg->parts[N2_PART_WIDE] + 1;
}
int shop_parts_open(Shop *s, int *hood_style, int *hood_flag, int *door_style) {
    *hood_style = 0; *hood_flag = 0; *door_style = 0;
    if (!s) return 0;
    int m = 0;
    if (s->screen == SC_SPEC && s->spec_open && s->spec_slot == 9) m |= 1;
    if (s->screen == SC_SPEC && s->spec_open && s->spec_slot == 0xa0) m |= 4;
    if (s->screen == SC_SPEC && s->spec_slot >= 0x22 && s->spec_slot <= 0x2e) m |= 2;
    if (s->screen == SC_ICE) m |= 2;   /* UI_ICEMAIN modes 2..5: 0x4c1860(1, 1, 1.0) */
    if (s->screen == SC_MAG_REWARD)    /* FEDoCarPartAnimNow: bit0 hood (0), bit3 trunk (1), bits 1/2 the doors (2 / 3) */
        m |= (s->mag_flags & 1 ? 1 : 0) | (s->mag_flags & 8 ? 2 : 0) |   /* CarAnimLocation 2 = LEFT_DOOR, */
             (s->mag_flags & 2 ? 8 : 0) | (s->mag_flags & 4 ? 16 : 0);   /* 3 = RIGHT_DOOR (0x433ad0): 8 / 16 */
    if (s->screen == SC_NEON_LIST && neon_slot(s) == 1) m |= 1;
    if (s->screen == SC_NEON_LIST && neon_slot(s) == 3) m |= 2;
    if ((shop_view(s) & 0xffff) == 0x44) m |= 1;
    hood_anim_style(s, s->car, s->preview.parts[N2_PART_HOOD], hood_style, hood_flag);
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

/* ---- the career's performance parts (CAREER.md "C6: shops") ------------
   MapPerfPackageToUnlockable 8012ed7c: the category's unlockable entity;
   CareerUnlocker::IsPerfPackageUnlocked 8012f724 / IsPerfPartUnlocked
   8012f650: filter 2 = a part of it owned, 4 = CalculateIfUnlocked; the
   package price GetPerfPackageCost 80129648: the parts neither installed
   nor bought (GetPackagePriceOfNotOwned), 90% of it for two or more (80%
   in CC_MEGALOW, 0x7fc8d0d0, the engaged shop) */
static const int k_perf_ent[PERF_NCAT] = { 6, 5, 4, 1, 9, 3, 8, 2, 7, 10 };
static int perf_megalow(const Shop *s) {
    return s->cshop && (s->cshop[0x38] | s->cshop[0x39] << 8 | s->cshop[0x3a] << 16 | (uint32_t)s->cshop[0x3b] << 24) == 0x7fc8d0d0u;
}
static int perf_bought(const Shop *s, int i) { return s->career && career_owns_perf(i); }   /* IsPerfPartPurchased */
static int perf_unlocked(const Shop *s, int cat, int lvl) {
    if (cat < 0 || cat >= PERF_NCAT) return 1;
    if (!s->career) return calc_unlocked(s, k_perf_ent[cat], lvl);   /* QuickRaceUnlocker: CareerUnlocker filter 4 */
    int ok = 0;
    if (s->career & 2) {
        int idx[4]; unsigned int nm[4];
        int n = perf_pkg_parts(cat, lvl, idx, nm, NULL);
        for (int j = 0; j < n && !ok; j++) ok = career_owns_perf(idx[j]);
    }
    if (s->career & 4) ok = ok || calc_unlocked(s, k_perf_ent[cat], lvl);
    return ok;
}
static void perf_pkg_cost(const Shop *s, int cat, int lvl, int *cost, int *disc) {
    int idx[4]; unsigned int nm[4]; int pr[4], sum = 0, cnt = 0;
    int n = perf_pkg_parts(cat, lvl, idx, nm, pr);
    for (int j = 0; j < n; j++)
        if (!(idx[j] >= 0 && idx[j] < N2_PERF_PARTS && s->fitted.perf[idx[j]]) && !perf_bought(s, idx[j])) { sum += pr[j]; cnt++; }
    *cost = sum; *disc = 0;
    if (cnt < 2) return;
    *cost = sum * (perf_megalow(s) ? 80 : 90) / 100;
    *disc = sum - *cost;
}
static int perf_pkg_bought(const Shop *s, int cat, int lvl) {   /* IsPerfPackagePurchased 801713f8 */
    int idx[4]; unsigned int nm[4];
    int n = perf_pkg_parts(cat, lvl, idx, nm, NULL);
    if (!n) return 0;
    for (int j = 0; j < n; j++) if (!perf_bought(s, idx[j])) return 0;
    return 1;
}
static int perf_part_price(int i) {   /* GetPerfPartCost 801295d8: the part's +8 */
    for (int c = 0; c < PERF_NCAT; c++)
        for (int l = 1; l <= 3; l++) {
            int idx[4]; unsigned int nm[4]; int pr[4];
            int n = perf_pkg_parts(c, l, idx, nm, pr);
            for (int j = 0; j < n; j++) if (idx[j] == i) return pr[j];
        }
    return 0;
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
            int bought = !inst && perf_bought(s, idx[n]);
            s->perf_row_name[n] = name[n];
            /* PartInfoWidget::Draw 801721bc: Installed / career: Purchased or "%$d" / Not Installed */
            s->perf_row_price[n] = inst ? 0xed56be1fu : bought ? 0x0371711cu : s->career ? 0 : 0x4287206fu;
            hud_num(s->perf_row_cash[n], (int)sizeof s->perf_row_cash[n], price[n]);
            if (on >= 0) feng_anim_hide(a, on, 0);
            if (op >= 0) {
                feng_anim_hide(a, op, 0);
                feng_anim_set(a, op, feng_hash_upper(inst || bought ? "Purchased" : "Init"), 1);
            }
            s->perf_nrow = n + 1;
        }
    }
    /* RefreshHeader 80172854: the discount's label and amount, the cost --
       outside the career and in the crib's Change Parts none of them */
    static const uint32_t cost[4] = { 0x06e0c9a8u, 0xbe8693e1u, 0x0380b632u, 0x89ca4fdbu };
    int box = s->career && !s->cust_entry && show;
    for (int i = 0; i < 4; i++) if ((o = feng_find(pk, cost[i])) >= 0) feng_anim_hide(a, o, !box);
    if (box) {
        int c, d;
        perf_pkg_cost(s, cat, type, &c, &d);
        {   /* FEPrintf: the label is a format ("10%% Discount:") */
            const char *f = str_get(s, perf_megalow(s) ? 0x66559034u : 0x6da17a25u);
            int k = 0;
            for (; *f && k < (int)sizeof s->perf_cost_txt[0] - 1; f++) if (!(f[0] == '%' && f[1] == '%')) s->perf_cost_txt[0][k++] = *f;
            s->perf_cost_txt[0][k] = 0;
        }
        if (d < 1) snprintf(s->perf_cost_txt[1], sizeof s->perf_cost_txt[1], "%s", str_get(s, 0xa4c02484u));
        else hud_num(s->perf_cost_txt[1], (int)sizeof s->perf_cost_txt[1], d);
        if (c < 1) snprintf(s->perf_cost_txt[2], sizeof s->perf_cost_txt[2], "%s", str_get(s, 0xa4c02484u));
        else hud_num(s->perf_cost_txt[2], (int)sizeof s->perf_cost_txt[2], c);
    }
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
    int top = s->career && perf_megalow(s) ? 2 : 3;   /* 0x562790: CC_MEGALOW stops at level 2 */
    for (int lvl = 1; lvl <= top; lvl++) {
        int idx[4]; unsigned int name[4];
        int n = perf_pkg_parts(s->perf_cat, lvl, idx, name, NULL);
        for (int j = 0; j < n && s->buy_n < BUY_MAX; j++) {
            if (!(s->career ? perf_bought(s, idx[j]) || ((s->career & 4) && calc_unlocked(s, k_perf_ent[s->perf_cat], lvl))
                            : calc_unlocked(s, k_perf_ent[s->perf_cat], lvl)))   /* QR: filter 4 */
                continue;   /* IsPerfPartAvailable */
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
            uint32_t sc = part == QP_DATA && (buy_installed(s, k) || perf_bought(s, s->buy_idx[k])) ? pur : sel ? hi : un;
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
        PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit); memcpy(pp.junk, s->fitted.junk, sizeof pp.junk);
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
    /* PerfCategoryThing::React 0xc407210 -> HandlePerformanceCategory */
    static const unsigned char spk[9] = { 0x10, 0xc, 9, 7, 0xd, 10, 3, 6, 0xb };
    if (s->career && cat >= 0 && cat < 9) career_speech_fe(spk[cat]);
    const FengPackage *pk = s->pk[PK_PERF_PKG];
    s->screen = SC_PERF_PKG;
    s->perf_cat = cat;
    scroller_set(&s->perfpkg, k_perf_pkg, NELEM(k_perf_pkg));
    s->perfpkg.sel = 1;                        /* Setup 0x556e60: SetInitialPos(1), the first package */
    for (int i = 0; i < s->perfpkg.n; i++) {   /* IsPerfPackageAvailable: the locked icon */
        int g = k_perf_pkg[i].group;
        s->perfpkg.locked[i] = g >= 1 && g <= 3 && !perf_unlocked(s, cat, g);
    }
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
            PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit); memcpy(pp.junk, s->fitted.junk, sizeof pp.junk);
            int i = s->buy_idx[s->buy_sel];
            if (perf_part_fitted(&pp, i)) dialog(s, DLG_AUTO, 1, 0xda15d7e5u, DA_NONE, 0, 0);
            else if (s->career && !perf_bought(s, i)) {   /* career: GetPerfPartCost against the wallet */
                if (g_career.cash < perf_part_price(i)) dialog(s, PK_DLG_S, 1, 0xd1ac60beu, DA_NONE, 0, 0);
                else dialog(s, DLG_AUTO, 2, perf_part_worse(&pp, i) ? 0x3949abeeu : 0x44b7167fu, DA_PERF_PART_BUY, 0, 0);
            }
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
        PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit); memcpy(pp.junk, s->fitted.junk, sizeof pp.junk);
        if (s->perfpkg.locked[s->perfpkg.sel]) return;
        if (type == PP_STOCK) {
            if (!perf_cat_level(&pp, s->perf_cat)) dialog(s, DLG_AUTO, 1, 0x89d4e03fu, DA_NONE, 0, 0);
            else dialog(s, DLG_AUTO, 2, 0x5d336f96u, DA_PERF_STOCK, 0, 0);
        } else if (perf_pkg_fitted(&pp, s->perf_cat, type)) dialog(s, DLG_AUTO, 1, 0xb84a37fau, DA_NONE, 0, 0);
        else if (s->career && !perf_pkg_bought(s, s->perf_cat, type)) {   /* career: GetPerfPackageCost against the wallet */
            int c, d;
            perf_pkg_cost(s, s->perf_cat, type, &c, &d);
            if (g_career.cash < c) dialog(s, PK_DLG_S, 1, 0xd1ac60beu, DA_NONE, 0, 0);
            else dialog(s, DLG_AUTO, 2, perf_pkg_worse(&pp, s->perf_cat, type) ? 0xfddd0c23u : 0x94f4c140u, DA_PERF_PKG_BUY, 0, 0);
        }
        else dialog(s, DLG_AUTO, 2, perf_pkg_worse(&pp, s->perf_cat, type) ? 0x0217453fu : 0xf2d7ffabu, DA_PERF_PKG, 0, 0);
    } else if (key == SHOP_KEY_BACK) {
        s->screen = SC_PERFORMANCE;
    }
}
/* the dialog's yes (0x51123592 -> 0x5501e0 / 0xe75749d4): installed on the car */
static void perf_pkg_yes(Shop *s) {
    int type = k_perf_pkg[s->perfpkg.sel].group;
    {
        PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit); memcpy(pp.junk, s->fitted.junk, sizeof pp.junk);
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
    PerfParts pp; perf_clear(&pp); memcpy(pp.fit, s->fitted.perf, sizeof pp.fit); memcpy(pp.junk, s->fitted.junk, sizeof pp.junk);
    perf_fit_part(&pp, s->buy_idx[s->buy_sel]);
    memcpy(s->fitted.perf, pp.fit, sizeof s->fitted.perf);
    memcpy(s->preview.perf, pp.fit, sizeof s->preview.perf);
    request_preview(s); s->debounce = 0.0f;
    printf("shop: performance part %d fitted\n", s->buy_idx[s->buy_sel]);
    buy_update(s);
    perf_pkg_update(s);
}

/* ---------------------------------------------------------------- brands */

/* ChooseBrandsScreen (UI_PerformanceBrandSelect.fng, Setup 0x550330 /
   GC 801742cc). A career buy's yes (0x9f00f07) on a package whose parts
   have brands (0x500160 >= 1), or on a part with brands (part +0xc), pushes
   it: one widget per part with brands (the first 4; package mode: arrows
   only with more than one brand, "Change Selection" 0x6d017bc5 hidden
   under two widgets; the one part: arrows always, no "Change Selection").
   A widget (0x53b960) shows the part's name (part +4), its brand's display
   name and DECAL_ICON_<brand>; left / right (0x521e90) step the brand with
   a wrap, starting at the first. "Change Brand" 0xb34a6c2b shows only on a
   part with two brands or more. Accept (0xe1fde1d1) = SetChosenBrands --
   the brands stored by widget order (0, 1, .. as the parts with brands come,
   not by part slot) -- then BuyPerfPackage / BuyPerfPart and the install. */
#define O_BRAND_CHANGE 0xb34a6c2bu
#define O_BRAND_SELECT 0x6d017bc5u
static GLuint g_anim_tex[FENG_MAX_OBJ];          /* draw_anim_package: image overrides */
static int brand_obj(const FengPackage *pk, int part, int n) {
    if (part == QP_N) { char nm[32]; snprintf(nm, sizeof nm, "OPTION_IMAGE_%d", n); return feng_find(pk, feng_hash_upper(nm)); }
    return qr_pool(pk, part, n);
}
static void brand_scripts(Shop *s, int restart) {
    const FengPackage *pk = s->pk[PK_BRAND];
    FengAnim *a = s->brand_anim;
    if (!pk || !a) return;
    uint32_t hi = feng_hash_upper("Highlight"), un = feng_hash_upper("UnHighlight");
    for (int k = 0; k < s->brand_n; k++)
        for (int part = 0; part <= QP_N; part++) {
            int o = brand_obj(pk, part, k + 1);
            if (o >= 0) feng_anim_set(a, o, k == s->brand_sel ? hi : un, restart);
        }
    int o = feng_find(pk, O_BRAND_CHANGE);
    if (o >= 0 && s->brand_n) feng_anim_hide(a, o, s->brand_w[s->brand_sel].nb < 2);
}
/* 0x52dcc0 per widget, y = row x TITLE_MASTER height: the name at the
   title's left, half the image down; the image centred on DATA_MASTER at its
   top; the data under the image (+1); the arrows just outside DATA_MASTER,
   half the image down; the backing at (+232, +64) */
static void brand_marks(Shop *s) {   /* which objects are widget rows (not pinned; arrows U2_Menu_Arrow) */
    const FengPackage *pk = s->pk[PK_BRAND];
    memset(s->qr_rowobj, 0, sizeof s->qr_rowobj);
    if (!pk) return;
    for (int n = 1; n <= 4; n++)
        for (int part = 0; part <= QP_N; part++) {
            int o = brand_obj(pk, part, n);
            if (o < 0) continue;
            s->qr_rowobj[o] = part == QP_LEFT || part == QP_RIGHT ? 2 : 1;
            for (int c = o + 1; c < pk->nobj; c++)
                if (feng_inside(pk, c, o)) s->qr_rowobj[c] = 1;
        }
}
static void brand_layout(Shop *s) {
    const FengPackage *pk = s->pk[PK_BRAND];
    FengAnim *a = s->brand_anim;
    if (!pk || !a) return;
    RowGeom g; row_geom(pk, &g);
    for (int n = 1; n <= 4; n++) {
        int k = n - 1, used = k < s->brand_n;
        float y = (float)k * g.step;
        int oi = brand_obj(pk, QP_N, n);
        float ih = oi >= 0 ? fabsf(pk->obj[oi].size[1]) : 44.8f;
        for (int part = 0; part <= QP_N; part++) {
            int o = brand_obj(pk, part, n);
            if (o < 0) continue;
            int vis = used && !((part == QP_LEFT || part == QP_RIGHT) && !s->brand_w[k].arrows);
            feng_anim_hide(a, o, !vis);
            if (!vis) continue;
            float aw = fabsf(pk->obj[o].size[0]), ah = fabsf(pk->obj[o].size[1]);
            switch (part) {
            case QP_NAME: feng_anim_pos(a, o, g.tl, g.tt + y + 0.5f * ih); break;
            case QP_DATA: feng_anim_pos(a, o, g.dcx, g.dt + y + ih + 1.0f); break;
            case QP_LEFT: feng_anim_pos(a, o, g.dcx - 0.5f * g.dw - 0.5f * aw, g.tt + y + 0.5f * ih + 0.5f * ah); break;
            case QP_RIGHT: feng_anim_pos(a, o, g.dcx + 0.5f * g.dw + 0.5f * aw, g.tt + y + 0.5f * ih + 0.5f * ah); break;
            case QP_BACK: feng_anim_pos(a, o, g.tl + 232.0f, g.tt + y + 64.0f); break;
            case QP_N: feng_anim_pos(a, o, g.dcx, g.dt + y + 0.5f * ih); break;
            }
        }
    }
    int o;   /* 0x538080: no paging with four widgets at most */
    if ((o = feng_find(pk, O_SCROLL_TAB)) >= 0) feng_anim_hide(a, o, 1);
    if ((o = feng_find(pk, O_SCROLL_L)) >= 0) feng_anim_hide(a, o, 1);
    if ((o = feng_find(pk, O_SCROLL_R)) >= 0) feng_anim_hide(a, o, 1);
    if ((o = feng_find(pk, O_BRAND_SELECT)) >= 0) feng_anim_hide(a, o, s->brand_n < 2);
    brand_scripts(s, 1);
}
/* the package's parts with brands (0x500160: their brand count summed) */
static int brand_fill(Shop *s, int pkg) {
    s->brand_n = 0; s->brand_sel = 0; s->brand_pkg = pkg;
    if (pkg) {
        int idx[4]; unsigned int nm[4]; int pr[4];
        int n = perf_pkg_parts(s->perf_cat, k_perf_pkg[s->perfpkg.sel].group, idx, nm, pr);
        for (int j = 0; j < n && s->brand_n < 4; j++) {
            unsigned int b[8]; int nb = perf_part_brands(idx[j], b, 8);
            if (!nb) continue;
            int k = s->brand_n++;
            s->brand_w[k].part = idx[j]; s->brand_w[k].name = nm[j]; s->brand_w[k].nb = nb;
            s->brand_w[k].cur = 0; s->brand_w[k].arrows = nb > 1;
            memcpy(s->brand_w[k].b, b, sizeof b);
        }
    } else if (s->buy_n) {
        int i = s->buy_idx[s->buy_sel];
        unsigned int b[8]; int nb = perf_part_brands(i, b, 8);
        if (nb) {
            s->brand_n = 1; s->brand_part = i;
            s->brand_w[0].part = i; s->brand_w[0].name = s->buy_name[s->buy_sel]; s->brand_w[0].nb = nb;
            s->brand_w[0].cur = 0; s->brand_w[0].arrows = 1;
            memcpy(s->brand_w[0].b, b, sizeof b);
        }
    }
    return s->brand_n;
}
static int brand_open(Shop *s, int pkg) {
    if (!brand_fill(s, pkg) || !s->pk[PK_BRAND]) return 0;
    if (!s->brand_anim) s->brand_anim = (FengAnim *)malloc(sizeof *s->brand_anim);
    if (!s->brand_anim) return 0;
    feng_anim_init(s->brand_anim, s->pk[PK_BRAND]);
    anim_fadein(s->brand_anim);
    s->brand_on = 1;
    brand_layout(s);
    printf("shop: brand select, %d part(s)\n", s->brand_n);
    return 1;
}
/* BuyPerfPackage 80170fc4 (the chosen brands by slot), then installed */
static void perf_pkg_buy(Shop *s, const uint32_t *chosen) {
    int type = k_perf_pkg[s->perfpkg.sel].group, c, d;
    int idx[4]; unsigned int nm[4]; int pr[4];
    perf_pkg_cost(s, s->perf_cat, type, &c, &d);
    career_spend(c);
    int n = perf_pkg_parts(s->perf_cat, type, idx, nm, pr);
    for (int j = 0; j < n; j++) if (!perf_bought(s, idx[j])) career_buy_perf(idx[j], pr[j], 0, chosen ? chosen[j] : 0);
    printf("career shop: bought performance package %d/%d for $%d (discount $%d), $%d left\n",
           s->perf_cat, type, c, d, g_career.cash);
    perf_pkg_yes(s);
}
/* BuyPerfPart 80170f6c + InstallPerfPart */
static void perf_part_buy(Shop *s, uint32_t brand) {
    int i = s->buy_idx[s->buy_sel], c = perf_part_price(i);
    career_buy_perf(i, c, 1, brand);
    printf("career shop: bought performance part %d for $%d (brand %s), $%d left\n", i, c,
           brand ? perf_brand_str(brand, 1) : "-", g_career.cash);
    perf_part_yes(s);
}
static void brand_input(Shop *s, int key) {
    if (!s->brand_n) { s->brand_on = 0; return; }
    if (key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) {
        s->brand_sel = (s->brand_sel + (key == SHOP_KEY_UP ? -1 : 1) + s->brand_n) % s->brand_n;
        brand_scripts(s, 0);
    } else if (key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) {
        /* 0x521e90: 0x9120409e (left) the next brand, 0xb5971bf1 (right) the one before */
        __typeof__(s->brand_w[0]) *w = &s->brand_w[s->brand_sel];
        if (!w->arrows) return;
        w->cur = (w->cur + (key == SHOP_KEY_LEFT ? 1 : -1) + w->nb) % w->nb;
        const FengPackage *pk = s->pk[PK_BRAND];
        int o = pk ? brand_obj(pk, key == SHOP_KEY_LEFT ? QP_LEFT : QP_RIGHT, s->brand_sel + 1) : -1;
        if (o >= 0) feng_anim_set(s->brand_anim, o, feng_hash_upper("Blink"), 1);
    } else if (key == SHOP_KEY_OK) {
        uint32_t chosen[4] = { 0, 0, 0, 0 };
        for (int k = 0; k < s->brand_n; k++) chosen[k] = s->brand_w[k].b[s->brand_w[k].cur];   /* SetChosenBrands */
        s->brand_on = 0;
        if (s->brand_pkg) perf_pkg_buy(s, chosen);
        else perf_part_buy(s, chosen[0]);
    } else if (key == SHOP_KEY_BACK) s->brand_on = 0;
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
        /* 0x49c690: save prompts turned off, or Autosave turned on, asks first;
           No stays on the screen */
        if (s->opt_save0 && !g_opt.save_on)
            dialog(s, DLG_AUTO, 2, 0x22bc94afu, DA_OPT_LEAVE, 0, 0);   /* SAVELOAD_CONFIRM_NO_SAVEPOINT */
        else if (!s->opt_auto0 && g_opt.autosave)
            dialog(s, DLG_AUTO, 2, 0xf3568b86u, DA_OPT_LEAVE, 0, 0);   /* SAVELOAD_CONFIRM_TURN_AUTOSAVE_ON */
        else opt_leave(s);
    }
}

/* Back to UI_OptionsMain (0x4b6010): with save prompts on, a profile that
   changed (IsDirty 0x52a4c0) or prompts just turned on asks to save
   (EnterMemcard 0x2042, back to the options after) */
static void opt_leave(Shop *s) {
    options_save();
    s->screen = SC_OPT_MAIN;
    int dirty = profile_dirty();
    profile_drop_snapshot();
    if (g_opt.save_on && (dirty || g_opt.save_on != s->opt_save0)) mc_enter(s, 0x2042, SC_OPT_MAIN);
}

/* F2.4: a car select entry's id ("<folder>" stock, "<folder>#<slot>" tuned) */
static void qr_car_id(const ShopCar *c, char *out, size_t cap) {
    if (c->slot >= 0) snprintf(out, cap, "%s#%d", c->folder, c->slot); else snprintf(out, cap, "%s", c->folder);
}
/* the action's car from an id: the folder, and the slot when the Quick Race car select sent it */
static void qr_car_out(const Shop *s, ShopAction *a) {
    const char *h = strchr(s->qr_car_cur, '#');
    snprintf(a->car, sizeof a->car, "%.*s", h ? (int)(h - s->qr_car_cur) : (int)strlen(s->qr_car_cur), s->qr_car_cur);
    a->slot = s->screen != SC_QR_CAR ? -2 : h ? atoi(h + 1) : -1;
}
/* the car select's list under the filter (0x4e22e0 / 0x4eed10, no sort) */
static void qr_build_cars(Shop *s) {
    int fid = k_qr_filters[s->qr_filter].id, n = 0, sel = 0;
    for (int i = 0; i < s->ncars && n < QR_MAX_CARS; i++) {
        /* GetCarFiltered: filter 2 the tuned cars (My Cars), 1 the stock cars, 3 both */
        if (fid == 2 && s->cars[i].slot < 0) continue;
        if (fid == 1 && s->cars[i].slot >= 0) continue;
        /* 0x4e22e0: UnlockSystem::IsCarUnlocked(GetUnlockFilter 1) 0x534180 --
           a stock car the profile's career has not unlocked is left out (a
           customized one, "My Cars", always passes) */
        if (s->cars[i].slot < 0 && !career_qr_car_unlocked(airace_type_of(s->cars[i].folder))) continue;
        char id[48]; qr_car_id(&s->cars[i], id, sizeof id);
        if (!strcmp(id, s->qr_car_cur)) sel = n;
        s->qr_car_list[n++] = (short)i;
    }
    s->qr_ncar = n; s->qr_car_sel = sel;
}

void garage_tt_request(int what, int dir);   /* main.c: the garage turntable */
static void qr_car_moved(Shop *s) {
    if (!s->qr_ncar) return;
    const ShopCar *c = &s->cars[s->qr_car_list[s->qr_car_sel]];
    char id[48]; qr_car_id(c, id, sizeof id);
    if (strcmp(id, s->qr_car_cur)) {
        snprintf(s->qr_car_cur, sizeof s->qr_car_cur, "%s", id);
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
    /* the screen's own SetRideInfo on entry: the car on the platform may be
       UI_Main's DDAY_PLAYER_CAR preset (main.c puts the garage build back) */
    if (s->qr_ncar && !s->qr_car_pending) { s->qr_car_pending = 1; s->qr_car_debounce = 0.0f; }
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
            if (s->qr_type == QR_FREERUN && beyond_enabled()) {
                /* the World Expansion (beyond.h) is on the card: where to drive. Bayview is
                   the first button and the default; Back stays on this screen. */
                dialog_text(s, DLG_AUTO, 2, "Where do you want to drive?", DA_QR_BAYVIEW, STR_YES, STR_NO);
                s->dlg_l1 = "Bayview"; s->dlg_l2 = "World Expansion";
                s->dlg_act2 = DA_QR_BEYOND;
                s->dlg_sel = 0; dlg_focus(s);
            } else if (s->qr_type == QR_FREERUN) { g_beyond_pick = 0; s->set[QR_FREERUN].ev = 4000; qr_enter_cars(s); }
            else { qr_build_tracks(s); s->screen = SC_QR_TRACK; }
        } else if (key == SHOP_KEY_BACK) s->screen = SC_MAIN;
        break;
    case SC_QR_TRACK:   /* the list stops at its ends (the scroll arrows hide there) */
        if ((key == SHOP_KEY_LEFT && s->qr_track.sel > 0) ||
            (key == SHOP_KEY_RIGHT && s->qr_track.sel < s->qr_track.n - 1)) {
            scroller_move(&s->qr_track, key == SHOP_KEY_LEFT ? -1 : 1);
            s->set[s->qr_type].ev = s->qr_track_e[s->qr_track.sel].group;
            qr_map_request(s);
        } else if (key == SHOP_KEY_OK && s->qr_track.n && !s->qr_track_lock[s->qr_track.sel]) qr_enter_options(s);
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
            /* UIQRCarSelect::ScrollCars 800dff44: SendScrollDirectionToGarageScreen(0,
               0x9120409e left / 0xb5971bf1 right), the turntable spins that way
               (the career car lot's ScrollCars sends none) */
            garage_tt_request(0, key == SHOP_KEY_LEFT);
            qr_car_moved(s);
        } else if (key == SHOP_KEY_LTRIG || key == SHOP_KEY_RTRIG) {
            int nf = NELEM(k_qr_filters);
            s->qr_filter = (s->qr_filter + (key == SHOP_KEY_LTRIG ? nf - 1 : 1)) % nf;
            qr_build_cars(s);
            qr_car_moved(s);
        } else if (key == SHOP_KEY_SQ && s->qr_cust && s->qr_ncar &&
                   s->cars[s->qr_car_list[s->qr_car_sel]].slot >= 0) {
            /* 0x20 + 0xc519bfc0 (800de290): a tuned car (not a career / sponsor one) ->
               ShowTwoButtons Yes 0xf770 / No 0x61c, "delete this car?" 0x367161f5, the
               focus on No; Yes 0x51123592 -> DeleteCurrentCar 800e081c */
            dialog(s, DLG_AUTO, 2, 0x367161f5u, DA_QR_DELETE, STR_YES, STR_NO);
            s->dlg_sel = 1;
            dlg_focus(s);
        } else if (key == SHOP_KEY_SQ && !s->qr_cust && s->qr_ncar) {
            /* mode 2 (Quick Race) + 0xc519bfc0 (GC 800de290 / PC 0x4fc1f0): a sponsor or
               career car returns (the port's list has neither); AbleToAddNewTunedCar is
               asked for every car, a tuned one too -- full: ShowOneButton OK 0x639,
               0x91921a8d + MakeFrontendSound(8) (PC; GC plays none there). Else
               SetViewFilter(3) (All Cars), ClearHasBeenModified, BeginCarCustomize(2, ..,
               the car), SetSelectedCar, MakeFrontendSound(8) = table 4 (select) */
            int nt = 0;
            for (int i = 0; i < s->ncars; i++) nt += s->cars[i].slot >= 0;
            if (nt >= 20) {
                dialog(s, DLG_AUTO, 1, 0x91921a8du, DA_NONE, STR_OK, 0);
                audio_fe_play(FE_SND_SELECT);
                break;
            }
            for (int k = 0; k < NELEM(k_qr_filters); k++) if (k_qr_filters[k].id == 3) s->qr_filter = k;
            if (s->qr_car_pending) s->qr_car_debounce = 0.0f;   /* the car on screen first */
            s->cust_go = 1; s->cust_qr = 1;
            audio_fe_play(FE_SND_SELECT); s->snd_done = 1;
        } else if (key == SHOP_KEY_OK && s->qr_ncar) {
            if (s->qr_car_pending) s->qr_car_debounce = 0.0f;   /* the car on screen first */
            if (s->qr_cust && s->cars[s->qr_car_list[s->qr_car_sel]].slot < 0) {
                /* 0x20 + 0x406415e3: a stock car needs a free tuned slot (AbleToAddNewTunedCar
                   8013424c), else ShowOneButton OK 0x639, 0x91921a8d (the garage is full) */
                int nt = 0;
                for (int i = 0; i < s->ncars; i++) nt += s->cars[i].slot >= 0;
                if (nt >= 20) { dialog(s, DLG_AUTO, 1, 0x91921a8du, DA_NONE, STR_OK, 0); break; }
            }
            if (s->qr_cust) s->cust_go = 1;                 /* mode 0x20: 0x552cc0(3) -> the Car Shop */
            else if (g_opt.p[0].ask_trans) {                /* 0x4fc1f0: Ask for Transmission (0x83aa6a) */
                /* 0x497d50 ShowTwoButtons: "Transmission:" 0x30003d27, Manual 0x1b3a2485
                   (0x5f5e3886: +0x15 = 1) / Auto 0x61cb0720 (0x1a2826e1: 0), the focus
                   on the current one, back 0x1fab5998 stays; both send 0xba5b5fce = race */
                dialog(s, DLG_AUTO, 2, 0x30003d27u, DA_TRANS_MANUAL, 0x1b3a2485u, 0x61cb0720u);
                s->dlg_act2 = DA_TRANS_AUTO;
                s->dlg_sel = g_opt.p[0].manual ? 0 : 1;
                dlg_focus(s);
            } else s->qr_go = 1;
        } else if (key == SHOP_KEY_BACK && s->qr_cust) {   /* 0x4b22c0: back to UI_Main */
            s->qr_cust = 0;
            s->screen = SC_MAIN;
        } else if (key == SHOP_KEY_BACK)                    /* 0x4fa470 */
            s->screen = s->qr_type == QR_FREERUN ? SC_QR_MODE : SC_QR_OPTIONS;
        break;
    }
}

/* per frame: the map's rest timer and fade, the option scripts, the car debounce */
static void qr_update(Shop *s, float dt) {
    s->map_t += dt;
    if ((s->screen == SC_QR_TRACK || s->screen == SC_CRIB_WMAP) && s->map_want && s->map_want != s->map_ev) {
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
            s->map_t = 0.0f;                            /* StartZoom 0x4b4b80 */
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


/* ------------------------------------------------------------- profiles */

/* The memcard flow (EnterMemcard 0x4a3100 .. ExitMemcard 0x4a31d0) runs over
   the screen it was entered from: MC_Main's questions and messages
   (MCV_MSG), MC_List (MCV_LIST) or the name entry (MCV_KB). The questions
   are the front end's (DoSaveFlow 0x4e8d30, MCD_* = its dialog types);
   the messages realmc's (MCD_RMC: its Save 0x6fce82 / Load 0x6fc532 /
   Delete 0x6fc73a show them through ShowMessage 0x4c6050, every one
   waiting for a button), their text from memcard/LOCALE_ENGLISH.loc. */
enum { MCV_OFF, MCV_MSG, MCV_LIST, MCV_KB };
enum { MCD_NONE, MCD_LOSE = 1, MCD_CREATE = 2, MCD_INFO = 3, MCD_SAVE = 5, MCD_LOAD = 6, MCD_RMC = 9,
       MCD_PCSAVE = 0x100 /* the port's: a PC save to import (pcsave.c) */ };
/* what a realmc message's answer leads to */
enum { RM_OVERWRITE = 1, RM_SAVED, RM_SAVE_FAIL, RM_LOADED, RM_LOAD_FAIL, RM_DELETE_Q, RM_DELETED, RM_DELETE_FAIL };
#define O_MC_TITLE   0x426c7b4du   /* "Memory Card" -> SAVELOAD_TITLE_PC */
#define O_MC_NAME    0x6042a783u   /* "Enter Name:" (the PC's typed entry) */
#define O_MC_BTN     0xb8a7c6ccu   /* button strings 0..5 (0x4a35c0: 0 / 1 two, 2 one) */
#define O_MC_BTN_G   0xb1000cc6u   /* ... their groups */
#define O_MCL_HEAD   0x48d4fcaeu   /* Create_Button: LOAD / DELETE */
#define O_MCL_BACK   0x001335f0u   /* the back label: BACK, CANCEL at boot */
#define MCL_ROWS 8
static int g_boot_load_done;   /* MC_Background's boot load runs once */

/* realmc string N (1-based) of memcard/LOCALE_<LANGUAGE>.loc (MemoryCard::LoadLocale
   80105848: GetLanguageName, Chinese = ENGLISH -- no LOCALE_CHINESE): LOCL {tag, size,
   0, count, offsets from the tag}, UTF-16 */
static const char *rmc_str_root(const char *dataroot, int id);
static const char *rmc_str(const Shop *s, int id) { return rmc_str_root(s->dataroot, id); }
const char *shop_rmc_string(const char *dataroot, int id) { return rmc_str_root(dataroot, id); }   /* savenote.c */
static const char *rmc_str_root(const char *dataroot, int id) {
    static char out[16][128];
    static int ok;
    if (!ok) {
        ok = 1;
        char path[600];
        snprintf(path, sizeof path, "%s/memcard/LOCALE_%s.loc", dataroot,
                 n2_lang() == 9 ? "ENGLISH" : k_n2_lang_name[n2_lang()]);
        FILE *f = fopen(path, "rb");
        if (!f) {   /* older installs copied LOCALE_ENGLISH.loc only */
            printf("shop: %s not found, English memcard text\n", path);
            snprintf(path, sizeof path, "%s/memcard/LOCALE_ENGLISH.loc", dataroot);
            f = fopen(path, "rb");
        }
        static unsigned char b[4096];
        long n = f ? (long)fread(b, 1, sizeof b, f) : 0;
        if (f) fclose(f);
        if (!n) printf("shop: %s not found\n", path);
        for (long i = 0; i + 16 <= n; i++) {
            if (memcmp(b + i, "LOCL", 4)) continue;
            uint32_t cnt; memcpy(&cnt, b + i + 12, 4);
            for (uint32_t k = 0; k < cnt && k < 16 && i + 20 + 4 * (long)k <= n; k++) {
                uint32_t o; memcpy(&o, b + i + 16 + 4 * k, 4);
                int m = 0;
                for (long q = i + (long)o; q + 1 < n && m < 127; q += 2) {
                    unsigned c = b[q] | b[q + 1] << 8;
                    if (!c) break;
                    out[k][m++] = c == 0xa0 ? ' ' : c < 256 ? (char)c : '?';
                }
                out[k][m] = 0;
            }
            break;
        }
    }
    return id >= 1 && id <= 16 ? out[id - 1] : "";
}
/* 0x6fdaf4: %1 is the profile's name */
static void rmc_text(const Shop *s, int id, const char *name, char *o, int cap) {
    const char *t = rmc_str(s, id), *p = strstr(t, "%1");
    if (p) snprintf(o, (size_t)cap, "%.*s%s%s", (int)(p - t), t, name, p + 2);
    else snprintf(o, (size_t)cap, "%s", t);
}

static void mc_focus(Shop *s) {
    FengAnim *a = s->mc_anim;
    const FengPackage *pk = s->pk[PK_MC];
    if (!a || !pk) return;
    uint32_t hl = feng_hash_upper("Highlight"), un = feng_hash_upper("UnHighlight");
    int msg = s->mc_view == MCV_MSG;
    for (int b = 0; b < 6; b++) {
        int on = msg && (s->mc_nbtn == 2 ? b < 2 : s->mc_nbtn == 1 && b == 2);
        int cur = s->mc_nbtn == 2 ? b == s->mc_sel : b == 2;
        uint32_t h[2] = { O_MC_BTN + (uint32_t)b, O_MC_BTN_G + (uint32_t)b };
        for (int k = 0; k < 2; k++) {
            int o = feng_find(pk, h[k]);
            if (o < 0) continue;
            feng_anim_hide(a, o, !on);
            if (on) feng_anim_set(a, o, cur ? hl : un, 1);
        }
    }
    int o;
    if ((o = feng_find(pk, O_DLG_BLURB)) >= 0) feng_anim_hide(a, o, !msg);
}
/* a question or message in MC_Main: 2 buttons Yes / No (No highlighted,
   0x4a35c0), 1 a single one; 0 a realmc task running */
static void mc_msg(Shop *s, int dlg, const char *text, int nbtn, uint32_t b1, uint32_t b2) {
    snprintf(s->mc_text, sizeof s->mc_text, "%s", text);
    s->mc_dlg = dlg; s->mc_nbtn = nbtn; s->mc_b[0] = b1; s->mc_b[1] = b2;
    s->mc_sel = 1; s->mc_view = MCV_MSG;
    mc_focus(s);
    audio_fe_play(FE_SND_DIALOG); s->snd_done = 1;
    printf("memcard: \"%s\"\n", s->mc_text);
}
static void mc_q(Shop *s, int dlg, uint32_t msg) { mc_msg(s, dlg, str_get(s, msg), 2, STR_YES, STR_NO); }
/* realmc's own: Yes / No are its strings 3 / 4, Continue 5 */
static void mc_rmc(Shop *s, int then, int id, const char *name, int nbtn) {
    char t[256];
    rmc_text(s, id, name, t, sizeof t);
    s->mc_then = then;
    mc_msg(s, MCD_RMC, t, nbtn, 0, 0);
}

/* ExitMemcard: back to the screen the flow came from; a success drops the
   backup (0x5032c0), a cancel restores it (0x5347c0) */
static void career_helper(Shop *s);
static void start_career_enter(Shop *s);
static void crib_enter(Shop *s, int from_main);
static void crib_play(Shop *s);
static void lot_leave(Shop *s);
static void mc_exit(Shop *s, int ok) {
    if (!ok && s->mc_backup) { profile_restore(); s->mc_changed = 1; }
    if (s->mc_backup) profile_drop_snapshot();
    s->mc_backup = 0;
    options_save();
    s->mc_view = MCV_OFF;
    s->screen = s->mc_ret;
    if (s->screen == SC_PROFILE) s->prof.locked[2] = !profile_named();   /* 0x251bf83e: the locks again */
    if (s->mc_changed) { s->mc_changed = 0; s->fe_action = SHOP_ACT_PROFILE; }   /* 0x3a2be557 */
    printf("memcard: done (%s), profile \"%s\"\n", ok ? "success" : "cancel", profile_name());
    /* 0x100 / 0x400: the result to UI_StartCareer, whose 0x461a18ee on a
       named profile is StartCareerHelper (NotificationMessage 800d2150) */
    if (s->screen == SC_START_CAREER) {
        if (ok && (s->mc_flags & 0x500) && profile_named()) career_helper(s);
        else start_career_enter(s);   /* the options follow the profile again */
    }
    /* UICareerCarLot: 0x461a18ee and 0x8867412d both StartCareerFreeRoam */
    if (s->screen == SC_CAREER_LOT) lot_leave(s);
    /* uiCareerCrib: 0x461a18ee / 0x8867412d after CheckMemory's save start gameplay */
    if (s->screen == SC_CRIB && s->crib_play) { s->crib_play = 0; crib_play(s); }
}
/* 0x8867412d */
static void mc_cancel(Shop *s) { mc_exit(s, (s->mc_flags & MCF_CANCEL_OK) && profile_named()); }

static void kb_open(Shop *s);
static void kb_input(Shop *s, int key);
/* realmc suppresses its overwrite question and "Save complete." while
   autosave is on (0x100 / 0x400), the Profile Manager clears that */
static int mc_quiet(const Shop *s) { return g_opt.autosave && s->mc_ret != SC_PROFILE; }
static void mc_write(Shop *s) {
    if (profile_save()) {
        if (mc_quiet(s)) mc_exit(s, 1);
        else { savenote_cancel(); mc_rmc(s, RM_SAVED, 8, profile_name(), 1); }   /* "Save complete." (the dialog says it, no corner note) */
    } else mc_rmc(s, RM_SAVE_FAIL, 9, profile_name(), 1);         /* "Unable to save %1." */
}
/* Save(name) 0x4d9b80 -> realmc 0x6fce82 */
static void mc_save(Shop *s) {
    if (profile_exists(profile_name()) && !mc_quiet(s)) mc_rmc(s, RM_OVERWRITE, 7, profile_name(), 2);
    else mc_write(s);
}
/* DoSaveFlow 0x4e8d30 */
static void mc_flow(Shop *s, int p) {
    int st = p == 0 && !profile_named() ? 2 : p;
    if (st == 2 && profile_named()) st = 4;
    if (st == 4) st = !(s->mc_flags & MCF_FORCE) && g_opt.autosave ? 6 : 5;
    if (st == 5 && !(s->mc_flags & MCF_CONFIRM) && g_opt.autosave) st = 6;
    switch (st) {
    case 1: mc_q(s, MCD_LOSE, 0xd36620feu); break;    /* SAVELOAD_CONFIRM_LOSE_ANY_CHANGES */
    case 2: s->mc_flags |= MCF_ASKED;
            mc_q(s, MCD_CREATE, 0x02b641c0u); break;  /* create a new profile? */
    case 3: kb_open(s); break;
    case 5: {                                         /* SAVELOAD_CONFIRM_SAVE_THE_PROFILE "%s" */
        char f[400], t[512];
        snprintf(f, sizeof f, "%s", str_get(s, 0x2a9ba6f9u));
        char *p2 = strstr(f, "%s");
        if (p2) { *p2 = 0; snprintf(t, sizeof t, "%s%s%s", f, profile_name(), p2 + 2); }
        else snprintf(t, sizeof t, "%s", f);
        mc_msg(s, MCD_SAVE, t, 2, STR_YES, STR_NO);
        break;
    }
    default: mc_save(s); break;                       /* 6 SetAutoSave, 7 Save */
    }
}

static void mc_list_scripts(Shop *s, int restart) {
    FengAnim *a = s->mcl_anim;
    const FengPackage *pk = s->pk[PK_MC_LIST];
    if (!a || !pk) return;
    uint32_t hl = feng_hash_upper("Highlight"), un = feng_hash_upper("UnHighlight");
    static const int parts[3] = { QP_NAME, QP_DATA, QP_BACK };
    for (int r = 0; r < MCL_ROWS; r++) {
        int used = s->mc_ltop + r < s->mc_n, cur = s->mc_ltop + r == s->mc_lsel;
        for (int k = 0; k < 3; k++) {
            int o = qr_pool(pk, parts[k], r + 1);
            if (o < 0) continue;
            feng_anim_hide(a, o, !used);
            if (used) feng_anim_set(a, o, cur ? hl : un, restart);
        }
    }
    int o;   /* the arrows and tab when the list scrolls */
    if ((o = feng_find(pk, 0x444969fdu)) >= 0) feng_anim_hide(a, o, s->mc_ltop == 0);
    if ((o = feng_find(pk, 0x444969feu)) >= 0) feng_anim_hide(a, o, s->mc_ltop + MCL_ROWS >= s->mc_n);
    if ((o = feng_find(pk, O_SCROLL_TAB)) >= 0) feng_anim_hide(a, o, s->mc_n <= MCL_ROWS);
}
static void mc_list_open(Shop *s) {
    s->mc_view = MCV_LIST; s->mc_lsel = 0; s->mc_ltop = 0;
    const FengPackage *pk = s->pk[PK_MC_LIST];
    if (!pk) return;
    if (!s->mcl_anim) s->mcl_anim = (FengAnim *)malloc(sizeof *s->mcl_anim);
    if (!s->mcl_anim) return;
    feng_anim_init(s->mcl_anim, pk);
    anim_fadein(s->mcl_anim);
    mc_list_scripts(s, 1);
}

/* Load 0x4c5f20 -> realmc 0x6fc532 */
static void mc_load(Shop *s, const char *name) {
    char nm[PROFILE_NAME_MAX + 1];
    snprintf(nm, sizeof nm, "%s", name);
    if (profile_load(nm)) { s->mc_changed = 1; mc_rmc(s, RM_LOADED, 10, nm, 1); }   /* "Load complete." */
    else mc_rmc(s, RM_LOAD_FAIL, 11, nm, 1);                                        /* "Unable to load %1." */
}

/* The port's PC save import (no retail flow; pcsave.h): Load in the Profile
   Manager first offers a PC save copied to ux0:data/NFSU2/pcsave/ whose
   name has no profile yet, with the retail SAVELOAD_CONFIRM_LOAD_THE_PROFILE
   question ("%s" the name); Yes imports and loads it ("Load complete."),
   No goes on to the list. */
static char g_pcsave_path[600], g_pcsave_name[32];
static int mc_pcsave_offer(Shop *s) {
    if (!pcsave_find_new(PROFILE_DIR_PATH, g_pcsave_path, sizeof g_pcsave_path, g_pcsave_name)) return 0;
    char f[400], t[512];
    snprintf(f, sizeof f, "%s", str_get(s, 0x976bdaaau));   /* SAVELOAD_CONFIRM_LOAD_THE_PROFILE */
    char *p = strstr(f, "%s");
    if (p) { *p = 0; snprintf(t, sizeof t, "%s%s%s", f, g_pcsave_name, p + 2); }
    else snprintf(t, sizeof t, "%s", f);
    printf("memcard: PC save %s (%s) offered\n", g_pcsave_name, g_pcsave_path);
    mc_msg(s, MCD_PCSAVE, t, 2, STR_YES, STR_NO);
    return 1;
}
static void mc_list_or_none(Shop *s) {
    if (s->mc_n == 0) mc_msg(s, MCD_INFO, str_get(s, 0xab1f0e92u), 1, STR_OK, 0);
    else mc_list_open(s);
}
static void mc_pcsave_answer(Shop *s, int yes) {
    if (!yes) { mc_list_or_none(s); return; }
    PcSaveInfo info;
    if (pcsave_import(g_pcsave_path, s->dataroot, PROFILE_DIR_PATH, &info)) mc_load(s, info.name);
    else mc_rmc(s, RM_LOAD_FAIL, 11, g_pcsave_name, 1);   /* "Unable to load %1." */
}

/* InitComplete 0x4f3570 */
static void mc_enter(Shop *s, int flags, int ret) {
    s->mc_flags = flags; s->mc_op = flags & 0xf0; s->mc_ret = ret;
    s->mc_changed = 0; s->mc_backup = 0;
    const FengPackage *pk = s->pk[PK_MC];
    if (pk) {
        if (!s->mc_anim) s->mc_anim = (FengAnim *)malloc(sizeof *s->mc_anim);
        if (s->mc_anim) {
            feng_anim_init(s->mc_anim, pk);
            anim_fadein(s->mc_anim);
            int o;
            if ((o = feng_find(pk, O_MC_NAME)) >= 0) feng_anim_hide(s->mc_anim, o, 1);
        }
    }
    s->mc_view = MCV_MSG; s->mc_nbtn = 0; s->mc_text[0] = 0; s->mc_dlg = MCD_NONE;
    mc_focus(s);
    printf("memcard: enter 0x%x (profile \"%s\")\n", flags, profile_name());
    switch (s->mc_op) {
    case MCOP_LOAD: case MCOP_DELETE: case MCOP_BOOT:
        s->mc_n = profile_list(s->mc_names, PROFILE_LIST_MAX);
        if (s->mc_op == MCOP_BOOT) {
            if (s->mc_n == 1) mc_load(s, s->mc_names[0]);     /* the one profile: no list */
            else if (s->mc_n == 0) mc_flow(s, 2);
            else mc_list_open(s);
        } else if (s->mc_op == MCOP_LOAD && mc_pcsave_offer(s)) {
        } else mc_list_or_none(s);
        break;
    case MCOP_SAVE_CHECK: mc_flow(s, profile_named() ? 4 : 2); break;
    case MCOP_SAVE_CUR: if (!profile_named()) mc_exit(s, 1); else mc_flow(s, 5); break;
    case MCOP_CREATE: mc_flow(s, profile_named() ? 1 : 2); break;
    default: mc_exit(s, 0); break;
    }
}

/* leaving the Car Shop / a garage change: the save check (0x2042) when
   something changed and save prompts are on */
void shop_save_check(Shop *s) {
    if (!s) return;
    int dirty = profile_dirty();
    profile_drop_snapshot();
    if (g_opt.save_on && dirty) mc_enter(s, 0x2042, s->screen);
}

/* HandleButtonPressed 0x4f3780 (Yes = button 0), and realmc's answers */
static void mc_answer(Shop *s, int yes) {
    switch (s->mc_dlg) {
    case MCD_CREATE: if (yes) kb_open(s); else mc_cancel(s); break;
    case MCD_INFO: mc_cancel(s); break;
    case MCD_SAVE: if (yes) mc_save(s); else mc_cancel(s); break;
    case MCD_LOSE:
        if (!yes) { mc_exit(s, 0); break; }
        profile_snapshot(); s->mc_backup = 1;          /* AllocBackupDB(1), then 0x5346f0 */
        if (s->mc_flags & 0x20000) career_default();   /* CSNew: PlayerCareerState::Default(1) only */
        else profile_default();
        s->mc_changed = 1;
        kb_open(s);
        break;
    case MCD_LOAD: if (yes) mc_load(s, s->mc_names[s->mc_lsel]); else mc_cancel(s); break;
    case MCD_PCSAVE: mc_pcsave_answer(s, yes); break;
    case MCD_RMC:
        switch (s->mc_then) {
        case RM_OVERWRITE:   /* No fails the save (0xdc12af2e): autosave off, ask again */
            if (yes) mc_write(s);
            else { if (s->mc_ret != SC_PROFILE) g_opt.autosave = 0; mc_flow(s, 2); }
            break;
        case RM_SAVE_FAIL: g_opt.autosave = 0; mc_flow(s, 2); break;
        case RM_LOAD_FAIL: case RM_DELETE_FAIL: mc_cancel(s); break;
        case RM_DELETE_Q: {
            if (!yes) { mc_cancel(s); break; }
            char nm[PROFILE_NAME_MAX + 1];
            snprintf(nm, sizeof nm, "%s", s->mc_names[s->mc_lsel]);
            int cur = profile_named() && !strcmp(nm, profile_name());
            if (profile_delete(nm)) {
                if (cur) s->mc_changed = 1;                /* DeleteDone 0x4c6330: unnamed, defaults */
                mc_rmc(s, RM_DELETED, 13, nm, 1);          /* "Saved game deleted." */
            } else mc_rmc(s, RM_DELETE_FAIL, 14, nm, 1);   /* "Unable to delete %1." */
            break;
        }
        default: mc_exit(s, 1); break;                     /* saved / loaded / deleted */
        }
        break;
    default: mc_cancel(s); break;
    }
}

static void mc_input(Shop *s, int key) {
    s->snd_done = 1;
    if (s->mc_view == MCV_KB) { kb_input(s, key); return; }
    if (s->mc_view == MCV_LIST) {
        if (key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) {
            int k = s->mc_lsel + (key == SHOP_KEY_UP ? -1 : 1);
            if (k < 0 || k >= s->mc_n) return;
            s->mc_lsel = k;
            if (k < s->mc_ltop) s->mc_ltop = k;
            if (k >= s->mc_ltop + MCL_ROWS) s->mc_ltop = k - MCL_ROWS + 1;
            mc_list_scripts(s, 0);
            audio_fe_play(key == SHOP_KEY_UP ? FE_SND_UP : FE_SND_DOWN);
        } else if (key == SHOP_KEY_OK && s->mc_n > 0) {   /* DoSelect 0x4bea30 */
            audio_fe_play(FE_SND_SELECT);
            if (s->mc_op == MCOP_DELETE) mc_rmc(s, RM_DELETE_Q, 12, s->mc_names[s->mc_lsel], 2);
            else if (profile_named()) mc_q(s, MCD_LOAD, 0x81b1d73du);
            else mc_load(s, s->mc_names[s->mc_lsel]);
        } else if (key == SHOP_KEY_BACK) {
            audio_fe_play(FE_SND_BACK);
            mc_cancel(s);
        }
        return;
    }
    if (!s->mc_nbtn) return;
    if ((key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) && s->mc_nbtn == 2) {
        int j = key == SHOP_KEY_LEFT ? 0 : 1;
        if (j == s->mc_sel) return;
        s->mc_sel = j;
        mc_focus(s);
        audio_fe_play(key == SHOP_KEY_LEFT ? FE_SND_LEFT : FE_SND_RIGHT);
    } else if (key == SHOP_KEY_OK) {
        audio_fe_play(FE_SND_SELECT);
        mc_answer(s, s->mc_nbtn == 2 && s->mc_sel == 0);
    } else if (key == SHOP_KEY_BACK && s->mc_dlg != MCD_RMC) {   /* realmc's messages want a button */
        audio_fe_play(FE_SND_BACK);
        mc_answer(s, 0);
    }
}
/* the button strings: realmc's from its locale, the front end's by hash */
static const char *mc_btn_text(const Shop *s, int b) {
    if (s->mc_dlg == MCD_RMC) return rmc_str(s, s->mc_nbtn == 1 ? 5 : b == 0 ? 3 : 4);
    return str_get(s, s->mc_nbtn == 1 ? s->mc_b[0] : s->mc_b[b & 1]);
}

/* UI_ProfileManager (0x4c58b0): Create, Load, Save (locked unnamed), Delete */
static void profile_enter(Shop *s) {
    scroller_set(&s->prof, k_profile_menu, NELEM(k_profile_menu));
    s->prof.locked[2] = !profile_named();
    s->screen = SC_PROFILE;
}
static void profile_input(Shop *s, int key) {
    if (key == SHOP_KEY_LEFT) scroller_move(&s->prof, -1);
    else if (key == SHOP_KEY_RIGHT) scroller_move(&s->prof, 1);
    else if (key == SHOP_KEY_OK && !s->prof.locked[s->prof.sel])
        mc_enter(s, (int)k_profile_menu[s->prof.sel].group, SC_PROFILE);
    else if (key == SHOP_KEY_BACK) s->screen = SC_MAIN;   /* 0x911ab364 */
}

/* ---- UI_StartCareer (Setup 800d1f7c, NotificationMessage 800d2150) and
   StartCareerHelper 800d1b30 ---------------------------------------------- */
static void start_career_enter(Shop *s) {
    int n = 0, started = g_career.started;
    if (profile_named()) {
        s->scar_e[n++] = k_start_career[started ? 1 : 0];   /* CSResume */
        if (started) s->scar_e[n++] = k_start_career[2];    /* CSNew */
    } else s->scar_e[n++] = k_start_career[2];              /* CSNew */
    s->scar_e[n++] = k_start_career[3];                     /* CSLoad, always */
    int sel = s->screen == SC_START_CAREER ? s->scar.sel : 0;   /* FEngGetLastButton: first entry */
    scroller_set(&s->scar, s->scar_e, n);
    s->scar.sel = sel < n ? sel : 0; s->scar.scroll = (float)s->scar.sel;
    s->screen = SC_START_CAREER;
}

/* IG_PlayMovie (InGamePlayMovie; NotificationMessage 80183f54): the movie
   full screen, Start / Accept (0xb5af2461, 0x406415e3) or its end (0xc3960eb9)
   go to the next movie in the list; after the last, DoWhatNext. */
static void movie_stop(Shop *s) {
    mplay_close(s->mv);
    s->mv = NULL;
}

static void career_go(Shop *s, int nis) {
    s->career_go = 1 + (nis ? 1 : 0);
    printf("career: StartCareerFreeRoam%s\n", nis ? " (the career start NIS)" : "");
}

/* the end of the list (one movie): DoWhatNext 6 */
static void movie_done(Shop *s, int skipped) {
    printf("career: movie scene01 %s\n", skipped ? "skipped" : "finished");
    movie_stop(s);
    s->screen = SC_START_CAREER;
    career_go(s, career_first_movie_done());
}

/* InGamePlayMovie::LaunchFirstCareerMovie: {"scene01"} (MOVIES/Scene01.vp6) */
static void movie_play_first(Shop *s) {
    char path[600];
    snprintf(path, sizeof path, "%s/MOVIES/Scene01.vp6", s->dataroot);
    MPlay *v = mplay_open(path);
    if (!v) {
        printf("career: %s missing, the movie is skipped\n", path);
        career_go(s, career_first_movie_done());
        return;
    }
    s->mv = v;
    s->screen = SC_MOVIE;
}

static void movie_update(Shop *s, float dt) {
    if (!s->mv || s->screen != SC_MOVIE) return;
    if (mplay_update(s->mv, dt)) movie_done(s, 0);
}

/* StartCareerHelper 800d1b30 */
static void career_helper(Shop *s) {
    int cs = career_start();
#ifdef OPENUG2_SIM_PRE_H
    if (getenv("OPENUG2_SIM_CRIB")) cs = CSTART_CRIB;   /* test aid (simulator): Resume opens the crib */
#endif
    switch (cs) {
    case CSTART_CRIB:   /* FEngSwitchPackages(UI_CareerCrib.fng): the career main menu */
        crib_enter(s, 1);
        break;
    case CSTART_FIRST_MOVIE:
        movie_play_first(s);
        break;
    default:
        career_free_roam_start();
        career_go(s, 0);
        break;
    }
}

/* ---- uiCareerCrib (GC 800d2268..800d2a4c): UI_CareerCrib.fng, an
   IconScrollerMenu over GarageMain (the CRIB01 platform when the file is
   there). Reached from StartCareerHelper once DDAY_EVENT_B is done and from
   a crib shop in free roam (LaunchFrontendShopFromGame, type 0). ---------- */
static void crib_setup(Shop *s, int from_main) {
    int n = 0;
    for (int i = 0; i < (int)NELEM(k_crib); i++) {
        if (k_crib[i].group == CR_WORLD_MAP && g_career.stage <= 1 && !getenv("OPENUG2_SIM_WMAP")) continue;   /* 1 < DAT_803bb288 */
        s->crib_e[n++] = k_crib[i];
    }
    scroller_set(&s->crib, s->crib_e, n);
    s->crib_from_main = from_main;
    s->crib_play = 0;
    s->front_end = 1;
    s->screen = SC_CRIB;
    /* uiCareerCrib ctor: BuildCurrentRideForPlayer -> CarViewer SetRideInfo
       (from free roam the career car is already the car) */
    if (from_main) s->fe_action = SHOP_ACT_CAREER_RIDE;
    s->platform = 4;   /* Setup: ChangePlatform(1, 4) unless CRIB01 is already in */
}
static void crib_enter(Shop *s, int from_main) {
    crib_setup(s, from_main);
    profile_snapshot();   /* 0x35f8620b: AllocBackupDB (IsDirty's reference); FE speech event 0x11: fe_speech_screen */
    printf("crib: %d options (%s)\n", s->crib.n, from_main ? "from the career menu" : "from free roam");
}
static void csel_enter(Shop *s);
static void csp_enter(Shop *s);
static void cwm_enter(Shop *s);
static void crw_enter(Shop *s, int option);
/* 0x451e768e: CheckMemory 800d2308 (EnterMemcard 0x151 when dirty), then
   0x9a3b571d: +0x7e3c = 1, StartCareerFreeRoam */
static void crib_play(Shop *s) {
    profile_drop_snapshot();
    g_career.has_spawn = 1;
    career_go(s, 0);
}
static void crib_dlg(Shop *s, int act) {
    switch (act) {
    case DA_CRIB_PLAY:
        if (profile_dirty()) { s->crib_play = 1; mc_enter(s, 0x151, SC_CRIB); }
        else crib_play(s);
        break;
    case DA_CRIB_MAIN:   /* 0x30ed2368: ChangePlatform(4, 1), UI_Main */
        profile_drop_snapshot();
        s->platform = 1;
        s->crib_from_main = 0;
        s->screen = SC_MAIN;
        s->fe_action = SHOP_ACT_CAREER_QUIT;
        printf("crib: back to the main menu\n");
        break;
    case DA_CRIB_TEST:   /* 0x34dc1bec: DoStartTuning -> BeginCarTuning (the test track) */
        /* FEngSetLastButton, BeginCarTuning(0, current career car, entry 0) (tuning.c, main.c) */
        profile_drop_snapshot();
        s->fe_action = SHOP_ACT_TEST_TRACK;
        printf("crib: Test Track -- BeginCarTuning\n");
        break;
    }
}
static void crib_input(Shop *s, int key) {
    if (key == SHOP_KEY_LEFT) scroller_move(&s->crib, -1);
    else if (key == SHOP_KEY_RIGHT) scroller_move(&s->crib, 1);
    else if (key == SHOP_KEY_OK && !s->crib.locked[s->crib.sel]) {
        switch (s->crib.e[s->crib.sel].group) {
        case CR_PLAY:         /* 0x60b42afb, Accept / Decline */
            dialog(s, DLG_AUTO, 2, 0x60b42afbu, DA_CRIB_PLAY, 0x88579775u, 0xbe503981u);
            break;
        case CR_MAIN:         /* 0xcb5e3178 */
            dialog(s, DLG_AUTO, 2, 0xcb5e3178u, DA_CRIB_MAIN, 0x88579775u, 0xbe503981u);
            break;
        case CR_TEST_TRACK:   /* 0xb10ae56f, 0xf770 / 0x61c */
            dialog(s, DLG_AUTO, 2, 0xb10ae56fu, DA_CRIB_TEST, 0xf770u, 0x61cu);
            break;
        case CR_SAVE:         /* EnterMemcard 0xc41 (0x28c2c1b8 / 0x7d1e4c8c: AllocBackupDB again) */
            mc_enter(s, 0xc41, SC_CRIB);
            break;
        case CR_CAR_SELECT:   /* CCarSel: UI_CareerCarSelect */
            csel_enter(s);
            break;
        case CR_REWARDS:      /* React 0x4af6e0: UI_CribRewardOptionsMain */
            crw_enter(s, -1);
            break;
        case CR_PARTS:        /* CCChangeParts: BeginCarCustomize(1, ..., the career car) */
            s->fe_action = SHOP_ACT_CAREER_PARTS;
            printf("crib: Change Parts\n");
            break;
        case CR_WORLD_MAP:    /* CCViewWorldMap React 0x4af710: UI_CareerWorldMap */
            cwm_enter(s);
            break;
        }
    }
    else if (key == SHOP_KEY_BACK) {   /* 0x911ab364: gameplay, or the main menu when it came from there */
        if (s->crib_from_main) dialog(s, DLG_AUTO, 2, 0xcb5e3178u, DA_CRIB_MAIN, 0x88579775u, 0xbe503981u);
        else dialog(s, DLG_AUTO, 2, 0x60b42afbu, DA_CRIB_PLAY, 0x88579775u, 0xbe503981u);
    }
}

/* ---- UICareerCribCarSelect (GC 800d4b80..800d5d64; BuildCarList PC
   0x4e0160): UI_CareerCarSelect.fng, the career slots in order. A slot that
   is locked (or has no car type) shows "locked" (+0x8f8), an unlocked empty
   one "open for sponsor" (+0x8fc); the rest are the cars. Accept on another
   car asks to switch (CommitSwitchToOwnedCar), then FORWARD -> 0xe1fde1d1:
   back to UI_CareerCrib.fng, which builds the current ride again. -------- */
static void lot_show(Shop *s, int delay);
static void lot_refresh(Shop *s);
static const char *lot_model_name(const Shop *s, int type);
static void lot_format(char *out, size_t cap, const char *fmt, const char *a, const char *b);
/* main.c: the garage turntable (GarageMainScreen); what 0 car, 1 lock, 2 empty, 3 back */
void garage_tt_request(int what, int dir);
int shop_car_pending(const Shop *s) { return s && s->qr_car_pending; }
/* 0xc98356ba, the scroll delay after a scroll (step -1 left / +1 right, 0 none):
   locked (+0x8f8) Show3DLock, open (+0x8fc) SpinPlatforms, else
   SendScrollDirectionToGarageScreen + SetRideInfo; dir = scroll != 0xb5971bf1 */
static void csel_show(Shop *s, int step) {
    if (!s->lot_n) return;
    int k = s->lot[s->lot_sel].kind;
    if (step) garage_tt_request(k == 2 ? 1 : k == 1 ? 2 : 0, step < 0);
    if (k == 0) lot_show(s, step != 0);
    else s->qr_car_pending = 0;   /* R1: a car scrolled past inside the delay is never SetRideInfo'd (800d4d38) */
}
static void csel_enter(Shop *s) {
    memset(s->lot, 0, sizeof s->lot);
    s->lot_n = 0; s->lot_sel = 0; s->lot_bar = 1;
#ifdef OPENUG2_SIM_PRE_H   /* test aid (simulator): OPENUG2_SIM_CAR2=<CARS folder>[:<garage.txt line>] fills the first open slot */
    if (getenv("OPENUG2_SIM_CAR2"))
        for (int i = 0; i < CAREER_NSLOTS; i++) {
            CareerCar *c = career_car_slot(i);
            if (!c || c->locked || c->has) continue;
            char nm[64]; snprintf(nm, sizeof nm, "%s", getenv("OPENUG2_SIM_CAR2"));
            char *cf = strchr(nm, ':'); if (cf) *cf++ = 0;
            for (int t = 0; t < airace_ntypes(); t++)
                if (!strcmp(airace_type_name(t), nm)) {
                    c->type = t; c->has = 1; c->cfg[0] = 0;
                    if (cf) snprintf(c->cfg, sizeof c->cfg, "%s", getenv("OPENUG2_SIM_CAR2") + (cf - nm));
                    printf("sim: OPENUG2_SIM_CAR2 -- %s in slot %d\n", nm, i);
                }
            break;
        }
    if (getenv("OPENUG2_SIM_LOCKLAST")) {   /* test aid: the last slot locked (the LOCK model) */
        CareerCar *c = career_car_slot(CAREER_NSLOTS - 1);
        if (c && !c->has) c->locked = 1;
    }
#endif
    for (int i = 0; i < CAREER_NSLOTS && s->lot_n < QR_MAX_CARS; i++) {
        const CareerCar *c = career_car_slot(i);
        if (!c) continue;
        int k = c->locked ? 2 : !c->has ? 1 : c->type < 0 ? 2 : 0;
        s->lot[s->lot_n].type = k ? -1 : c->type;
        s->lot[s->lot_n].handle = c->handle;
        s->lot[s->lot_n++].kind = k;
    }
    lot_refresh(s);
    s->screen = SC_CRIB_CARSEL;
    csel_show(s, 0);
    printf("crib car select: %d slots\n", s->lot_n);
}
/* 0xe1fde1d1: FEngSwitchPackages(UI_CareerCrib.fng), on the option it left by */
static void crib_return(Shop *s, int option) {
    s->qr_car_pending = 0;
    crib_setup(s, s->crib_from_main);
    for (int i = 0; i < s->crib.n; i++)   /* SetInitialPos(FEngGetLastButton) */
        if (s->crib_e[i].group == option) { s->crib.sel = i; s->crib.scroll = (float)i; }
    s->fe_action = SHOP_ACT_CAREER_RIDE;   /* the ctor's BuildCurrentRideForPlayer */
}
static void csel_back(Shop *s) { garage_tt_request(3, 0); crib_return(s, CR_CAR_SELECT); }

/* ---- CribWorldMapScreen (PC ctor 0x4ee690, NotificationMessage 0x4e0f60;
   GC 800d8fa0 / 800d9c04): UI_CareerWorldMap.fng, an event replay browser
   (E:/re-tools/out/career/c7/WORLDMAP_CRIB_POSTRACE.md). L/R: the completed
   stages (Scrollerina, "Stage %d"); U/D: a vertical ThumbnailScroller of the
   region item and the region's events (BuildEventList 0x4b1360), the
   current one's info (FillScreen) and TRACKMAP 0.3 s after the cursor rests
   (TrackMapScroller, zoomed by SetZoom 0x498630); Accept on an event asks
   to race it. ------------------------------------------------------------ */
#define CWM_MAX 72
/* PC 0x7f4388 [region]: the region item's map and zoom window */
static const struct { float x, y, size; int track; } k_cwm_region[6] = {
    { 0, 0, 512, 4000 }, { 163, 154, 350, 4500 }, { 126, 93, 387, 4501 },
    { 17, 5, 480, 4502 }, { 0, 0, 512, 4503 }, { 0, 0, 512, 4504 } };
#define O_CWM_MAP     0x90fe80f3u
#define O_CWM_ICON    0xa334a7bcu   /* EVENT_ICONS, the pool */
#define O_CWM_MASTER  0x38e39ad7u   /* MINI_MASTER */
static int cwm_tid(const unsigned char *ev) { return (int16_t)(ev[0x18] | ev[0x19] << 8); }
static int cwm_rev(const unsigned char *ev) { return ev[0x1a] & 1; }
static const unsigned char *cwm_rec(const Shop *s, int tid) {   /* TrackInfo by number */
    for (int i = 0; i < s->ntrk; i++) {
        const unsigned char *r = s->trk + (long)i * 0x128;
        if (rec_ev(r) == tid) return r;
    }
    return NULL;
}
static void cwm_map_request(Shop *s) {   /* the map timer (+0x22c): SetTrackMap 0.3 s after the cursor stops */
    const unsigned char *ev = s->cwm.n ? s->cwm_ev[s->cwm.sel] : NULL;
    int want = ev ? cwm_tid(ev) : k_cwm_region[s->cwm_stage >= 1 && s->cwm_stage <= 5 ? s->cwm_stage : 0].track;
    if (want == s->map_want) return;
    s->map_want = want; s->map_wait = 0.3f;
}
/* SetupRegionEvents 0x4e0ce0: the region item, then BuildEventList 0x4b1360 --
   every event of the stage (stage 0 counts as region 1) with a track, not
   DDAY_EVENT_A / _B, completed or not, at most 71; SnapToHead */
static void cwm_build(Shop *s) {
    int n = 0, st = s->cwm_stage;
    s->cwm_ev[n++] = NULL;                                   /* AddRegionItem 0x4e0c30 */
    int all = career_stage_cleared(st) || getenv("OPENUG2_SIM_WMAP");
    for (int i = 0; all && i < career_nevents() && n < 72; i++) {
        const unsigned char *ev = career_event(i);
        int es = (signed char)ev[0x37];
        uint32_t h = n2_u32(ev + 8);
        if (!(es == st || (st == 1 && es == 0))) continue;
        if (!cwm_rec(s, cwm_tid(ev)) || h == 0xdd60e402u || h == 0xdd60e403u) continue;
        s->cwm_ev[n++] = ev;
    }
    s->cwm.e = NULL; s->cwm.n = n; s->cwm.sel = 0; s->cwm.scroll = 0.0f;
    cwm_map_request(s);
}
static void cwm_enter(Shop *s) {
    memset(&s->cwm, 0, sizeof s->cwm);
    s->cwm_nst = 0;                                          /* buildRegions 0x4cbf80 */
    while (s->cwm_nst < 7 && career_stage_cleared(s->cwm_nst + 1)) s->cwm_nst++;
    if (!s->cwm_nst && getenv("OPENUG2_SIM_WMAP")) s->cwm_nst = 1;   /* test aid (simulator): stage 1 shown uncleared */
    s->cwm_stage = 1;
    cwm_build(s);
    s->screen = SC_CRIB_WMAP;
    printf("crib world map: %d stages, %d events in stage 1\n", s->cwm_nst, s->cwm.n - 1);
}
static void cwm_input(Shop *s, int key) {
    if (key == SHOP_KEY_UP && s->cwm.sel > 0) { s->cwm.sel--; cwm_map_request(s); }   /* ScrollEvent 0x4cc140 */
    else if (key == SHOP_KEY_DOWN && s->cwm.sel < s->cwm.n - 1) { s->cwm.sel++; cwm_map_request(s); }
    else if (key == SHOP_KEY_LEFT && s->cwm_stage > 1) { s->cwm_stage--; cwm_build(s); }   /* ScrollRegion 0x4e0e50 */
    else if (key == SHOP_KEY_RIGHT && s->cwm_stage < s->cwm_nst) { s->cwm_stage++; cwm_build(s); }
    else if (key == SHOP_KEY_OK && s->cwm.sel > 0)           /* not on the region item */
        dialog(s, DLG_AUTO, 2, 0xec9488bbu, DA_CWM_RACE, 0xf770u, 0x61cu);
    else if (key == SHOP_KEY_BACK) crib_return(s, CR_WORLD_MAP);
}
/* "%d" in a localized format */
static void cwm_fmt_d(char *o, size_t cap, const char *fmt, int v) {
    const char *p = strstr(fmt, "%d");
    if (!p) { snprintf(o, cap, "%s %d", fmt, v); return; }
    snprintf(o, cap, "%.*s%d%s", (int)(p - fmt), fmt, v, p + 2);
}
/* ---- uiCribReward_Sponsors (GC 800d2ef0..800d3470): UI_RewardsSponsor.fng,
   an IconScroller of each stage's chosen sponsor (DECAL_ICON_<name>) up to
   the current stage, the selected one's header as the post-race sponsor
   screen's (RefreshHeader 800d3278) and its contract in a CTextScroller of
   TERMS_1..10. From the crib (+0x7454 set) BACK goes back to it. --------- */
static const uint32_t k_csp_terms[10] = { 0xfcc58ddau, 0xfcc58ddbu, 0xfcc58ddcu, 0xfcc58dddu, 0xfcc58ddeu,
                                          0xfcc58ddfu, 0xfcc58de0u, 0xfcc58de1u, 0xfcc58de2u, 0x9577494au };
static unsigned csp_u16(const unsigned char *p) { return p[0] | p[1] << 8; }
static const char *csp_name(const Shop *s) { return s->csp_n ? career_string(csp_u16(s->csp[s->csp_sel])) : ""; }
/* the contract body broken at the last space before TERMS_1's width (CTextScroller) */
static void csp_refresh(Shop *s) {
    char b[64];
    s->csp_nl = 0; s->csp_top = 0;
    if (!s->csp_n) return;
    snprintf(b, sizeof b, "SPONSOR_DETAILS_%s", csp_name(s));   /* ContractMessage::GetBody */
    const char *txt = str_get(s, feng_hash(b));
    const FengPackage *pk = s->pk[PK_CRIB_SPONSORS];
    int o = pk ? feng_find(pk, k_csp_terms[0]) : -1;
    if (o < 0 || !txt) return;
    int fi = pk->obj[o].res >= 0 && pk->obj[o].res < pk->nres ? s->font_of[PK_CRIB_SPONSORS][pk->obj[o].res] : -1;
    if (fi < 0 || !s->font[fi].ok) return;
    const FengFont *f = &s->font[fi].f;
    float w = pk->obj[o].wrapw > 0 ? (float)pk->obj[o].wrapw : 300.0f;
    char ln[160]; int len = 0, sp = -1;
    for (const char *q = txt; s->csp_nl < 48; q++) {
        int end = !*q, brk = *q == '\n' || *q == '^';
        if (!end && !brk && len < 159) {
            ln[len++] = *q; ln[len] = 0;
            if (*q == ' ') sp = len - 1;
            if (sp > 0 && feng_text_width(f, ln) > w) {
                ln[sp] = 0;
                snprintf(s->csp_ln[s->csp_nl++], 160, "%s", ln);
                len = (int)strlen(ln + sp + 1);
                memmove(ln, ln + sp + 1, (size_t)len + 1);
                sp = -1;
            }
            continue;
        }
        ln[len] = 0;
        if (len || brk) snprintf(s->csp_ln[s->csp_nl++], 160, "%s", ln);
        len = 0; sp = -1;
        if (end) break;
    }
}
static void csp_enter(Shop *s) {
    s->csp_n = 0; s->csp_sel = 0;   /* SetInitialPos(0) */
    for (int i = 0; i <= g_career.stage && i < 8; i++)
        if (g_career.st[i].chosen[0]) s->csp[s->csp_n++] = g_career.st[i].chosen[0];
#ifdef OPENUG2_SIM_PRE_H   /* test aid (simulator): stage 2's choosable sponsors */
    if (!s->csp_n && getenv("OPENUG2_SIM_SPONSOR"))
        for (int i = 0; i < 3; i++) if (cstage_sponsor_choice(&g_career.st[2], i)) s->csp[s->csp_n++] = cstage_sponsor_choice(&g_career.st[2], i);
#endif
    s->screen = SC_CRIB_SPONSORS;
    csp_refresh(s);
    printf("crib sponsors: %d signed\n", s->csp_n);
}
static void csp_input(Shop *s, int key) {
    if ((key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) && s->csp_n > 1) {
        s->csp_sel = (s->csp_sel + (key == SHOP_KEY_LEFT ? -1 : 1) + s->csp_n) % s->csp_n;
        csp_refresh(s);
    } else if (key == SHOP_KEY_UP && s->csp_top > 0) s->csp_top--;            /* CTextScroller */
    else if (key == SHOP_KEY_DOWN && s->csp_top + 10 < s->csp_nl) s->csp_top++;
    else if (key == SHOP_KEY_BACK) crw_enter(s, 2);   /* 0x911ab364, PC 0x495c40: back to the reward options
                                                         (0x836326 is never set on the PC) */
}
static void csel_input(Shop *s, int key) {
    if ((key == SHOP_KEY_LEFT && s->lot_sel > 0) ||        /* ScrollCars 800d5c90: 0x9120409e / 0xb5971bf1 */
        (key == SHOP_KEY_RIGHT && s->lot_sel < s->lot_n - 1)) {
        int d = key == SHOP_KEY_LEFT ? -1 : 1;
        s->lot_sel += d; s->lot_bar += d;
        csel_show(s, d);
        lot_refresh(s);
    } else if (key == SHOP_KEY_OK && s->lot_n) {           /* 0xc407210 / 0x406415e3 */
        char t[1024];
        const CareerCar *cur = career_current_car();
        int k = s->lot[s->lot_sel].kind;
        const char *now = k == 0 ? lot_model_name(s, s->lot[s->lot_sel].type) : "";
        if (s->qr_car_pending) s->qr_car_debounce = 0.0f;
        if (k == 2) {                                      /* locked */
            lot_format(t, sizeof t, str_get(s, 0x2415be66u), now, NULL);
            dialog_text(s, DLG_AUTO, 1, t, DA_NONE, 0, 0);
        } else if (k == 1) {                               /* open for a sponsor's car */
            lot_format(t, sizeof t, str_get(s, 0xcf3b2d70u), now, NULL);
            dialog_text(s, DLG_AUTO, 1, t, DA_NONE, 0, 0);
        } else if (cur && cur->handle == s->lot[s->lot_sel].handle) {   /* already the current car */
            lot_format(t, sizeof t, str_get(s, 0x7ee84e4eu), now, NULL);
            dialog_text(s, DLG_AUTO, 1, t, DA_NONE, 0, 0);
        } else {
            lot_format(t, sizeof t, str_get(s, 0x1231c38fu), now, cur ? lot_model_name(s, cur->type) : "");
            dialog_text(s, DLG_AUTO, 2, t, DA_CRIB_SWITCH, STR_YES, STR_NO);
        }
    } else if (key == SHOP_KEY_BACK) csel_back(s);         /* 0x911ab364: FORWARD */
}

static void start_career_input(Shop *s, int key) {
    if (key == SHOP_KEY_LEFT) scroller_move(&s->scar, -1);
    else if (key == SHOP_KEY_RIGHT) scroller_move(&s->scar, 1);
    else if (key == SHOP_KEY_OK && !s->scar.locked[s->scar.sel]) {
        switch (s->scar.e[s->scar.sel].group) {
        case CS_RESUME: career_helper(s); break;                       /* CSResume::React */
        case CS_NEW: mc_enter(s, 0x21961, SC_START_CAREER); break;     /* CSNew: create + career Default */
        case CS_LOAD: mc_enter(s, 0xc11, SC_START_CAREER); break;      /* CSLoad: 0x461a18ee / 0xf35d144e */
        }
    }
    else if (key == SHOP_KEY_BACK) s->screen = SC_MAIN;   /* 0x911ab364 */
}

/* ---- UICareerCarLot (GC 800d34bc..800d4a20, PC 0x4ed8a0..): the career Car
   Lot, UI_CareerCarLot.fng; LaunchFrontendShopFromGame 8012721c opens it for
   a shop of type 4 (+0x50). Every car is free: a buy fills the first empty
   unlocked slot (CommitChooseCar), a trade puts the stock car in a slot in
   place of its car (CareerCarTrade). States (CalcNextCarLotState 800d3880):
   1 start -> 2 an empty slot, 3 one slot (trade the career car), 4 more
   slots, all full (trade it, or 5: browse the career cars for the one to
   trade); 6 done: the memcard save (0x141) when the profile changed, then
   StartCareerFreeRoam. ------------------------------------------------------ */
enum { LOT_START = 1, LOT_BUY = 2, LOT_TRADE = 3, LOT_TRADE_OR_BROWSE = 4, LOT_BROWSE = 5, LOT_DONE = 6 };
#define O_LOT_ARROW_1 0x42aa46bcu        /* Scroll_Bar_Arrow_1 (FEScrollBar "Scroll_Bar", 801554b4) */
#define O_LOT_ARROW_2 0x42aa46bdu        /* Scroll_Bar_Arrow_2 */
#define O_LOT_HANDLE  0x95043b2du        /* Scroll_Bar_Handle */
#define O_LOT_BACKING 0x66eaf210u        /* Scroll_Bar_Backing */

/* GetCarModelNameLocalizedString 8011d84c: CAR_NAME_<type> (GC "CAR_NAME_%s%s", suffix "") */
static const char *lot_model_name(const Shop *s, int type) {
    return str_get(s, hash_more(hash_more(0xffffffffu, "CAR_NAME_"), airace_type_name(type)));
}
/* the dialog text with its %s filled in order (FormatMessage) */
static void lot_format(char *out, size_t cap, const char *fmt, const char *a, const char *b) {
    size_t n = 0; int k = 0;
    for (const char *p = fmt; *p && n + 1 < cap; ) {
        if (p[0] == '%' && p[1] == 's') {
            const char *v = k++ == 0 ? a : b;
            for (; v && *v && n + 1 < cap; v++) out[n++] = *v;
            p += 2;
        } else out[n++] = *p++;
    }
    out[n] = 0;
}
static const ShopCar *lot_car_info(const Shop *s, int type) {
    const char *nm = airace_type_name(type);
    for (int i = 0; i < s->ncars; i++) if (!strcmp(s->cars[i].folder, nm)) return &s->cars[i];
    return NULL;
}
/* the car viewer on the current entry (BuildCarList / ScrollCars: CarViewer
   SetRideInfo), by the car select's request; delay: the scroll's 0xc98356ba
   wait before the new car goes on */
static void lot_show(Shop *s, int delay) {
    if (!s->lot_n) return;
    snprintf(s->qr_car_cur, sizeof s->qr_car_cur, "%s", airace_type_name(s->lot[s->lot_sel].type));
    s->qr_car_pending = 1; s->qr_car_debounce = delay ? 0.35f : 0.0f;
}
/* BuildCarList 800d47d4: filter 1 the stock cars (career_lot_cars), 4 the
   career slots that hold a car (an empty or locked slot has no car type and
   fails IsCarAvailableInShop); the first entry current */
static void lot_build(Shop *s, int filter) {
    memset(s->lot, 0, sizeof s->lot);
    s->lot_n = 0; s->lot_sel = 0;
    if (filter == 1) {
        int t[QR_MAX_CARS], n = career_lot_cars(t, QR_MAX_CARS);
        for (int i = 0; i < n; i++) { s->lot[s->lot_n].type = t[i]; s->lot[s->lot_n++].handle = career_stock_handle(t[i]); }
    } else {
        for (int i = 0; i < CAREER_NSLOTS && s->lot_n < QR_MAX_CARS; i++) {
            const CareerCar *c = career_car_slot(i);
            if (!c || !c->has || c->type < 0 || !career_stock_handle(c->type) || !career_car_in_region(c->type) ||
                (career_car_on_disc && !career_car_on_disc(c->type)) || !career_car_unlocked(c->type)) continue;
            s->lot[s->lot_n].type = c->type; s->lot[s->lot_n++].handle = c->handle;
        }
    }
    printf("car lot: %d %s cars\n", s->lot_n, filter == 1 ? "stock" : "career");
    lot_show(s, 0);
}
/* RefreshHeader 800d45f4: the logos and sliders of the current car, the
   scroll bar (Update(1, count, +0x88), the arrows off at the list's ends) */
static void lot_refresh(Shop *s) {
    s->lot_hdr.type = s->lot_n ? s->lot[s->lot_sel].type : -1;
    s->lot_hdr.kind = s->lot_n ? s->lot[s->lot_sel].kind : 0;
    s->lot_hdr.n = s->lot_n; s->lot_hdr.idx = s->lot_bar;
    s->lot_hdr.first = s->lot_sel == 0; s->lot_hdr.last = s->lot_sel >= s->lot_n - 1;
}
static int lot_next(const Shop *s, int st) {                 /* CalcNextCarLotState 800d3880 */
    switch (st) {
    case LOT_START:
        if (s->lot_empty > 0) return LOT_BUY;
        if (s->lot_unlocked == 1) return LOT_TRADE;
        if (s->lot_unlocked > 1) return LOT_TRADE_OR_BROWSE;
        return LOT_START;
    case LOT_TRADE_OR_BROWSE: return LOT_BROWSE;
    case LOT_BUY: case LOT_TRADE: case LOT_BROWSE: case LOT_DONE: return LOT_DONE;
    default: return LOT_START;
    }
}
/* StartCareerFreeRoam from the lot (0x9a3b571d, the memcard's end, 0x51123592 DeallocBackupDB) */
static void lot_leave(Shop *s) {
    s->qr_car_pending = 0;
    profile_drop_snapshot();
    career_go(s, 0);
}
/* EnterCarLotState 800d3680 (FILTERTYPE, shown with "Career Cars" in 4, is
   not in the PC's UI_CareerCarLot) */
static void lot_enter(Shop *s, int st) {
    s->lot_state = st;
    printf("car lot: state %d\n", st);
    if (st == LOT_BROWSE) lot_build(s, 4);
    else if (st == LOT_DONE) {
        if (profile_dirty()) mc_enter(s, 0x141, SC_CAREER_LOT);   /* IsDirty: EnterMemcard 0x141 */
        else lot_leave(s);                                         /* 0x9a3b571d */
    }
}
/* the dialogs' messages (NotificationMessage 800d3ae8) */
static void lot_dlg(Shop *s, int act) {
    const CareerCar *cur = career_current_car();
    uint32_t sel = s->lot_n ? s->lot[s->lot_sel].handle : 0;
    switch (act) {
    case DA_LOT_BUY:                                       /* 0xd05fc3a3 */
        if (s->lot_state == LOT_BUY) {                     /* CommitBuy(FindFirstEmptySlot, the stock car) */
            int slot = career_first_empty_slot();
            career_choose_car(slot < 0 ? 0 : slot, sel);
            printf("car lot: bought %s into slot %d\n", airace_type_name(s->lot[s->lot_sel].type), slot < 0 ? 0 : slot);
            lot_enter(s, lot_next(s, s->lot_state));
        } else if (s->lot_state == LOT_TRADE) {            /* CommitTrade(the career car, the stock car) */
            if (cur) career_car_trade(cur->handle, sel);
            printf("car lot: traded the career car for %s\n", airace_type_name(s->lot[s->lot_sel].type));
            lot_enter(s, lot_next(s, s->lot_state));
        } else if (s->lot_state == LOT_BROWSE) {           /* CommitTrade(this career car, the remembered one) */
            career_car_trade(sel, s->lot_new);
            printf("car lot: traded slot %08x for %08x\n", (unsigned)sel, (unsigned)s->lot_new);
            lot_enter(s, lot_next(s, s->lot_state));       /* ExitCurrentCarLotState 800d3814 */
        }
        break;
    case DA_LOT_TRADE:                                     /* 0x526adefc: trade the career car */
        if (cur) career_car_trade(cur->handle, sel);
        printf("car lot: traded the career car for %s\n", s->lot_n ? airace_type_name(s->lot[s->lot_sel].type) : "-");
        lot_enter(s, LOT_DONE);
        break;
    case DA_LOT_BROWSE:                                    /* 0x75d1e35e: Browse My Other Cars, or cancel */
        s->lot_new = sel;
        lot_enter(s, lot_next(s, s->lot_state));
        break;
    case DA_LOT_EXIT:                                      /* 0x51123592: DeallocBackupDB, StartCareerFreeRoam */
        lot_leave(s);
        break;
    }
}
static void lot_input(Shop *s, int key) {
    if ((key == SHOP_KEY_LEFT && s->lot_sel > 0) ||        /* ScrollCars 800d4a20: 0x9120409e / 0xb5971bf1 */
        (key == SHOP_KEY_RIGHT && s->lot_sel < s->lot_n - 1)) {
        int d = key == SHOP_KEY_LEFT ? -1 : 1;
        s->lot_sel += d; s->lot_bar += d;
        lot_show(s, 1);
        lot_refresh(s);
    } else if (key == SHOP_KEY_OK && s->lot_n) {           /* 0xc407210 / 0x406415e3 */
        char t[1024];
        const CareerCar *cur = career_current_car();
        const char *now = lot_model_name(s, s->lot[s->lot_sel].type);
        const char *mine = cur ? lot_model_name(s, cur->type) : "";
        if (s->qr_car_pending) s->qr_car_debounce = 0.0f;  /* the car on screen first */
        switch (s->lot_state) {
        case LOT_BUY:                                      /* "Do you want this stock %s?" */
            lot_format(t, sizeof t, str_get(s, 0xda8f3e96u), now, NULL);
            dialog_text(s, DLG_AUTO, 2, t, DA_LOT_BUY, STR_YES, STR_NO);
            break;
        case LOT_TRADE:                                    /* "Do you want to trade-in your %s ^for this stock %s?" */
            lot_format(t, sizeof t, str_get(s, 0x85215bc6u), mine, now);
            dialog_text(s, DLG_AUTO, 2, t, DA_LOT_BUY, STR_YES, STR_NO);
            break;
        case LOT_TRADE_OR_BROWSE:                          /* ... "^Or do you want to choose one of your other cars?" */
            lot_format(t, sizeof t, str_get(s, 0x3192f5d1u), mine, now);
            dialog_text(s, DLG_AUTO, 2, t, DA_LOT_TRADE, 0xfb75e78fu, 0x2699cf8eu);   /* Trade this car? / Browse My Other Cars */
            s->dlg_act2 = DA_LOT_BROWSE; s->dlg_back = DA_LOT_BROWSE;
            break;
        case LOT_BROWSE: {                                 /* this career car for the remembered stock car */
            int nt = career_stock_type(s->lot_new);
            lot_format(t, sizeof t, str_get(s, 0x85215bc6u), now, nt >= 0 ? lot_model_name(s, nt) : "");
            dialog_text(s, DLG_AUTO, 2, t, DA_LOT_BUY, STR_YES, STR_NO);
            break;
        }
        default: break;
        }
    } else if (key == SHOP_KEY_BACK) {                     /* 0x911ab364 */
        const CareerCar *c0 = career_car_by_handle(career_slot_handle(0));
        if (c0 && c0->has)                                 /* "Are you sure you wish to exit back to Explore Mode?" */
            dialog(s, DLG_AUTO, 2, 0xd76e0b5au, DA_LOT_EXIT, STR_YES, STR_NO);
        else                                               /* "Rachel needs her car back. ..." */
            dialog(s, DLG_AUTO, 1, 0x272ebebau, DA_NONE, 0, 0);
    }
}

int shop_platform(const Shop *s) { return s && s->platform > 0 ? s->platform : 1; }
/* EAXS GetAmbientZone 0x46b330's front-end branch: the shop's scene (+0x40 up
   to '.', hashed upper case; compared by name here) and record hash (+0x38) */
int shop_fe_ambient_zone(const Shop *s, int career)
{
    if (!career) return 0x1c;                      /* 0x850078 != 1; 1 = the career CONFIRMED (PlayerCareerState::IsCareerFreeRoam 0x500b60: 0x850078 == 1, no race) */
    const unsigned char *p = s ? s->fe_shop : NULL;
    if (!p) return 0x1d;                           /* 0x863444 clear: no shop */
    char nm[17]; int k = 0;
    for (; k < 16 && p[0x40 + k] && p[0x40 + k] != '.'; k++) nm[k] = (char)toupper(p[0x40 + k]);
    nm[k] = 0;
    uint32_t h = (uint32_t)p[0x38] | (uint32_t)p[0x39] << 8 | (uint32_t)p[0x3a] << 16 | (uint32_t)p[0x3b] << 24;
    if (!strcmp(nm, "CARLOT01")) return 0x17;
    if (!strcmp(nm, "CRIB01")) return 0x1d;
    if (!strcmp(nm, "PERFSHOP01")) return h == 0x7fc8d0d0u ? 0x18 : 0x1a;          /* CC_MEGALOW */
    if (!strcmp(nm, "PARTSSHOP01")) return h == 0xb88bd8c1u || h == 0x678847a1u ? 0x17 : 0x19;   /* SH_BODYSHOP(_01) */
    if (!strcmp(nm, "PAINTSHOP01")) return 0x1b;
    if (!strcmp(nm, "AUDIOSHOP01")) return 0x16;
    return 0x1c;
}
#ifdef OPENUG2_SIM_PRE_H
/* test aid (simulator): the C11 career autopilot's next key for the front end -- dialogs on their
   first button, the car lot buys the first car, the crib plays; 0 = leave it alone */
int shop_sim_auto_key(const Shop *s) {
    if (!s) return 0;
    if (s->mc_view != MCV_OFF) return !s->mc_nbtn ? 0 : s->mc_nbtn == 2 && s->mc_sel ? SHOP_KEY_LEFT : SHOP_KEY_OK;
    if (dlg_open(s)) return s->dlg_nbtn == 2 && s->dlg_sel ? SHOP_KEY_LEFT : SHOP_KEY_OK;
    switch (s->screen) {
    case SC_CAREER_LOT: case SC_MOVIE: return SHOP_KEY_OK;
    case SC_MAG_REWARD: return s->mag_shot <= 0 && !s->mag_leave ? SHOP_KEY_OK : 0;   /* Continue -> the save question */
    case SC_CRIB:
        for (int i = 0; i < s->crib.n; i++)
            if (s->crib.e[i].group == CR_PLAY) return i == s->crib.sel ? SHOP_KEY_OK : i < s->crib.sel ? SHOP_KEY_LEFT : SHOP_KEY_RIGHT;
        return 0;
    default: return 0;
    }
}
int shop_sim_screen(const Shop *s) { return s ? s->screen : -1; }
#endif
int shop_on_main_menu(const Shop *s) { return s && s->screen == SC_MAIN; }
/* The boot flow before UI_Main: UG_LS_Splash and MC_Background (0x4c51f0, the
   boot load EnterMemcard 0x121) push no GarageMain.fng and build no ride; the
   UIMain ctor 0x4df140 is the first to (BuildCurrentRideForPlayer from the
   just-loaded profile, then FEngPushPackageSpecial GarageMain.fng). 1 while the
   front end is still before that point, the loaded profile's car included. */
int shop_boot_pending(const Shop *s) {
    if (!s || !s->front_end) return 0;
    return !g_boot_load_done || (s->mc_view != MCV_OFF && s->mc_op == MCOP_BOOT) ||
           s->fe_action == SHOP_ACT_PROFILE;
}

void shop_crib_return(Shop *s, int from_main) {
    if (!s) return;
    s->crib_from_main = from_main;
    crib_return(s, CR_PARTS);
}
/* QuitToWorldMap: UI_CareerCrib again, on its last button (World Map) */
void shop_crib_return_map(Shop *s, int from_main) {
    if (!s) return;
    s->crib_from_main = from_main;
    crib_return(s, CR_WORLD_MAP);
}
int shop_crib_from_main(const Shop *s) { return s && s->crib_from_main; }

/* ---------------------------------------------------------------- the uniques shop */

/* UniquesBrowser (UI_ChooseUniquePart.fng, Setup 8016d2d0): THE_UNIQUES_SHOP
   (type 6), which GetShopWeAreCurrentlyIn gives while a world challenge's
   goal shop is entered (state 4). Its strip: GetUniquesPartsList 8012d688
   over the challenge's +0x15..+0x17 unlockable entities -- a visual one
   (+0x14 == 1) every upgrade level 4 part of MapUnlockableToCarSlotID's
   slot (0x3e: the vinyls of group 0x18; 0x0d: the wheels, one per name of
   the car's rim size), a performance one (2) MapUnlockableToPerfPackage's
   junkman path unless IsUniqueInstalled. Icons / names by slot 8016ce1c /
   8016cea4, by path 8016cff4 / 8016cf28. */
static int uq_ent_slot(int ent) {                                   /* MapUnlockableToCarSlotID 8012ec20 */
    switch (ent) {
    case 0x0b: return 9;    case 0x0c: return 0xc;  case 0x0d: return 0x1d; case 0x0e: return 0xf;
    case 0x0f: return 0xe;  case 0x10: return 0xb;  case 0x11: return 2;    case 0x12: return 1;
    case 0x13: return 0x20; case 0x14: return 7;    case 0x15: return 0x10; case 0x16: return 6;
    case 0x18: return 0x9f; case 0x19: return 0x9a; case 0x1b: return 0x9c; case 0x1e: return 0x22;
    case 0x21: return 0x1f; case 0x23: return 0xa0; case 0x24: return 0xa1;
    }
    return -1;
}
static int uq_ent_path(int ent) {                                   /* MapUnlockableToPerfPackage 8012ee40 */
    static const signed char t[11] = { -1, 3, 7, 5, 2, 1, 0, 8, 6, 4, 9 };
    return ent >= 0 && ent <= 10 ? t[ent] : -1;
}
static uint32_t uq_slot_icon(int slot) {                            /* 8016ce1c */
    switch (slot) {
    case 0xc: return 0xbb034ea6u; case 6: return 0x945e9bc5u; case 9: return 0x028f7092u; case 0x1d: return 0x0294d2a3u;
    }
    return slot >= 0x40 && slot < 0x44 ? 0x1b3a8dd3u : 0x74ce8c0bu;
}
static uint32_t uq_slot_name(int slot) {                            /* 8016cea4 */
    switch (slot) {
    case 0xc: return 0xbb034ea6u; case 6: return 0x945e9bc5u; case 9: return 0x028f7092u; case 0x1d: return 0x0294d2a3u;
    }
    return slot >= 0x40 && slot < 0x44 ? 0xd6d115e4u : 0;
}
static uint32_t uq_path_icon(int p) {                               /* 8016cff4 */
    static const uint32_t t[9] = { 0xea57d2c4u, 0x01a29ffau, 0x9701bde4u, 0xc15c94e6u, 0x06ef789cu,
                                   0x178475e7u, 0xbaa23a28u, 0x502abeedu, 0x06e8e477u };
    return p >= 0 && p < 9 ? t[p] : 0;
}
static uint32_t uq_path_name(int p) {                               /* 8016cf28 */
    static const uint32_t t[9] = { 0x758d5e3cu, 0x73ad2f72u, 0xa7886f38u, 0x026eea5eu, 0xabd0ff14u,
                                   0x9c34c75fu, 0xaa8959adu, 0xa7884765u, 0xabca6aefu };
    return p >= 0 && p < 9 ? t[p] : 0;
}
/* the junkman paths installed (FECarConfig +0x2f4; the session's career car) */
static int uq_add(Shop *s, int slot, int path) {
    if (s->uqs.n >= 64) return -1;
    int i = s->uqs.n++;
    memset(&s->uq[i], 0, sizeof s->uq[i]);
    s->uq[i].slot = slot; s->uq[i].path = path;
    s->uq_e[i].tex = path >= 0 ? uq_path_icon(path) : uq_slot_icon(slot);
    s->uq_e[i].str = path >= 0 ? uq_path_name(path) : uq_slot_name(slot);
    s->uq_e[i].group = slot;
    return i;
}
/* RefreshHeader 8016d0c0: a part with no LANGUAGEHASH its name ("%s %s %d\""
   brand, name and MAX size for wheels) in the option title; with one the
   game prints into 0xb1027477, which this package lacks -- the slot's name stays */
static void uq_name(Shop *s, int i, const unsigned char *r, uint32_t brand) {
    unsigned soff = r[8] | r[9] << 8, aoff = r[10] | r[11] << 8;
    const char *nm = s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "?";
    uint32_t mx = 0, lang = 0;
    if (db_attr(s, aoff, 0x4732da07u, &lang)) return;
    if (s->uq[i].slot == 0x1d) {
        db_attr(s, aoff, 0xeb0101e2u, &mx);
        const char *bn = "";
        for (int k = 0; k < NELEM(k_rims_tuner); k++) if (k_rims_tuner[k].key == brand) bn = str_get(s, k_rims_tuner[k].str);
        for (int k = 0; !bn[0] && k < NELEM(k_rims_suv); k++) if (k_rims_suv[k].key == brand) bn = str_get(s, k_rims_suv[k].str);
        snprintf(s->uq[i].name, sizeof s->uq[i].name, "%s %s %d\"", bn, nm, (int)(signed char)mx);
    } else snprintf(s->uq[i].name, sizeof s->uq[i].name, "%s", nm);
}
static int uq_build(Shop *s) {
    const unsigned char *wc = g_career.wcs.wc;
    s->uqs.n = 0; s->uqs.sel = 0; s->uqs.scroll = 0.0f;
    if (!wc || !s->db || !s->db_parts) return 0;
    int type = (signed char)wc[0x14];
    s->uq_perf = type == 2;
    int rim = g_shop_rec_ok ? (signed char)g_shop_rec[0xdc] : 0, rmax = g_shop_rec_ok ? (signed char)g_shop_rec[0xde] : 0;
    for (int e = 0; e < 3; e++) {
        int ent = wc[0x15 + e];
        if (ent == 0xff || ent == 0) continue;
        if (type == 2) {
            int path = uq_ent_path(ent);
            if (path >= 0 && !s->fitted.junk[path]) uq_add(s, -1, path);
            continue;
        }
        if (type != 1) continue;
        if (ent == 0x3e) {                                           /* the unique vinyls */
            for (int k = 0; k < vinyl_count(); k++) {
                const VinylPart *v = vinyl_at(k);
                if (v->group != 0x18 || v->level != 4) continue;
                int i = uq_add(s, 0x40, -1);
                if (i < 0) break;
                s->uq[i].hash = v->hash; s->uq[i].okey = v->hash; s->uq[i].ent = vinyl_price_row(0x18);
                const char *nm = v->lang ? str_get(s, v->lang) : "";
                snprintf(s->uq[i].name, sizeof s->uq[i].name, "%s", v->lang ? "" : v->name);
                (void)nm;   /* a LANGUAGEHASH name goes to the missing 0xb1027477 */
            }
            continue;
        }
        if (ent == 0x0d) {                                           /* the wheels: one per name */
            int model = db_model(s, "WHEELS"), first = s->uqs.n;
            const unsigned char *kept[64]; int nk = 0;
            for (long p = s->db_parts; model >= 0 && p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
                const unsigned char *r = s->db + p;
                if (!n2_u32(r) && !(r[8] | r[9] << 8)) break;
                if (r[4] != G_WHEEL || r[7] != model || (r[5] >> 5) != 4) continue;
                unsigned aoff = r[10] | r[11] << 8, soff = r[8] | r[9] << 8;
                const char *nm = s->db_str ? (const char *)s->db + s->db_str + soff * 4L : "";
                int at = -1;
                for (int k = 0; k < nk && at < 0; k++)
                    if (!strcasecmp(nm, (const char *)s->db + s->db_str + (kept[k][8] | kept[k][9] << 8) * 4L)) at = k;
                if (at < 0) { if (nk < 64) kept[nk++] = r; continue; }
                uint32_t o_new = 0, o_old = 0, m_new = 0, m_old = 0;
                unsigned ka = kept[at][10] | kept[at][11] << 8;
                db_attr(s, aoff, 0xce7d8db5u, &o_new); db_attr(s, ka, 0xce7d8db5u, &o_old);
                db_attr(s, aoff, 0xeb0101e2u, &m_new); db_attr(s, ka, 0xeb0101e2u, &m_old);
                if ((signed char)o_new == rim && (signed char)o_old <= (signed char)o_new &&
                    (signed char)m_old < (signed char)m_new && (signed char)m_new <= rmax) kept[at] = r;
            }
            for (int k = 0; k < nk; k++) {                           /* Setup: the car's rim size only */
                const unsigned char *r = kept[k];
                unsigned aoff = r[10] | r[11] << 8;
                uint32_t outer = 0, brand = 0;
                db_attr(s, aoff, 0xce7d8db5u, &outer);
                if ((signed char)outer != rim) continue;
                db_attr(s, aoff, 0xEBB03E66u, &brand);
                const char *folder = preset_rim_folder(brand);
                int st, in, out;
                if (!folder || !rim_part_parse(folder, n2_u32(r), &st, &in, &out)) {
                    printf("career uniques: wheel %s has no rim folder here\n", preset_part_name(s, r));
                    continue;
                }
                int i = uq_add(s, 0x1d, -1);
                if (i < 0) break;
                s->uq[i].hash = n2_u32(r); s->uq[i].okey = n2_u32(r); s->uq[i].ent = 0x21;
                snprintf(s->uq[i].folder, sizeof s->uq[i].folder, "%s", folder);
                s->uq[i].value = RIM_VALUE(st, in);
                uq_name(s, i, r, brand);
            }
            (void)first;
            continue;
        }
        int slot = uq_ent_slot(ent), group = preset_slot_group(slot);
        if (slot < 0 || group < 0) continue;
        static const char *const libs[] = { NULL, "SPOILER", "SPOILER_HATCH", "SPOILER_SUV", "EXHAUST", "ROOF" };
        int models[6], nm = 0;
        models[nm++] = db_model(s, s->car);
        for (int l = 1; l <= 5; l++)
            if ((group == G_SPOILER && l <= 3) || (group == G_EXHAUST && l == 4) || (group == G_ROOF && l == 5))
                models[nm++] = db_model(s, libs[l]);
        for (int mi = 0; mi < nm; mi++)
            for (long p = s->db_parts; models[mi] >= 0 && p + 14 <= s->db_parts + s->db_parts_len; p += 14) {
                const unsigned char *r = s->db + p;
                if (!n2_u32(r) && !(r[8] | r[9] << 8)) break;
                int upg = r[5] | r[6] << 8;
                if (r[4] != group || r[7] != models[mi] || (upg >> 5 & 7) != 4) continue;
                int v = part_value_lib(s->car, group, n2_u32(r), mi > 0, upg);
                if (v < 0) { printf("career uniques: %s has no part value on %s\n", preset_part_name(s, r), s->car); continue; }
                int i = uq_add(s, slot, -1);
                if (i < 0) break;
                s->uq[i].hash = n2_u32(r); s->uq[i].okey = n2_u32(r); s->uq[i].value = v; s->uq[i].ent = slot_ent(slot);
                uq_name(s, i, r, 0);
            }
    }
    scroller_set(&s->uqs, s->uq_e, s->uqs.n);
    return s->uqs.n;
}
/* PreviewPart / PreviewJunkmanPart: the shown one on the car */
static void uq_preview(Shop *s) {
    if (!s->uqs.n) return;
    const __typeof__(s->uq[0]) *u = &s->uq[s->uqs.sel];
    if (u->path >= 0) return;                                        /* the junkman parts look like nothing */
    if (u->slot == 0x1d) {
        snprintf(s->prev_folder, sizeof s->prev_folder, "%s", u->folder);
        s->prev_style = u->value; s->rim_pending = 1; s->rim_debounce = 0.35f;
        return;
    }
    N2CarConfig next = s->fitted;
    if (u->slot == 0x40) {                                           /* a unique takes all four layers */
        memset(next.vinyl, 0, sizeof next.vinyl); memset(next.vinyl_col, 0, sizeof next.vinyl_col);
        next.vinyl[0] = u->hash;
    } else body_put(&next, group_slot(preset_slot_group(u->slot)), u->value, 0);
    if (memcmp(&next, &s->preview, sizeof next)) { s->preview = next; request_preview(s); }
}
/* 0xd05fc3a3: InstallPart + BuyCarPart (visual) / InstallJunkmanPart, NotifyUniqueChosen, StartCareerFreeRoam */
static void uq_install(Shop *s) {
    if (!s->uqs.n) return;
    const __typeof__(s->uq[0]) *u = &s->uq[s->uqs.sel];
    if (u->path >= 0) {
        /* InstallJunkmanPart: the spec's path flag, RebuildPhysicsInfo (main: n2_set_perf) */
        s->fitted.junk[u->path] = 1; s->preview.junk[u->path] = 1;
        request_preview(s); s->debounce = 0.0f;
        printf("career uniques: junkman path %d installed\n", u->path);
    } else {
        if (u->slot == 0x1d) {
            memcpy(s->rim_folder, s->prev_folder, sizeof s->rim_folder);
            s->rim_style = s->prev_style;
            if (s->rim_pending) s->rim_debounce = 0.0f;
        } else {
            s->fitted = s->preview;
            if (u->slot == 9) { hood_installed(&s->fitted, 4); s->preview = s->fitted; request_preview(s); }
            if (s->pending) s->debounce = 0.0f;
        }
        career_buy_part(u->ent, u->okey, 0);                         /* level 4: no cost */
        printf("career uniques: %s (slot 0x%x) installed\n", u->name[0] ? u->name : str_get(s, s->uq_e[s->uqs.sel].str), u->slot);
    }
    career_wc_unique_chosen();
    s->uq_done = 1;
    s->exit_req = 1;
}
static void uq_enter(Shop *s) {
    uq_build(s);
    s->screen = SC_UNIQUES;
    printf("career uniques: %d parts (%s)\n", s->uqs.n, s->uq_perf ? "junkman paths" : "visual");
    uq_preview(s);
}
static void uq_input(Shop *s, int key) {
    if (key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) {
        int was = s->uqs.sel;
        scroller_move(&s->uqs, key == SHOP_KEY_LEFT ? -1 : 1);
        if (s->uqs.sel != was) uq_preview(s);
    } else if (key == SHOP_KEY_OK && s->uqs.n) dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_UQ_PICK, 0, 0);
    else if (key == SHOP_KEY_BACK) dialog(s, PK_DLG_S, 2, 0x20d0d219u, DA_UQ_LEAVE, STR_YES, STR_NO);
}
int shop_uniques_done(const Shop *s) { return s && s->uq_done; }

/* the shop's scene (+0x40, "CARLOT01.BIN" ...) through PlatformNameMap; CRIB01 when none */
static int shop_scene_platform(const unsigned char *shop) {
    static const char *const plat[] = { "", "SHOWROOM", "AUDIOSHOP01", "CARLOT01", "CRIB01", "MEGALOSHOP01",
                                        "PAINTSHOP01", "PARTSSHOP01", "PERFSHOP01" };
    int r = 4;
    if (shop) {
        char nm[17]; int k = 0;
        for (; k < 16 && shop[0x40 + k] && shop[0x40 + k] != '.'; k++) nm[k] = (char)toupper(shop[0x40 + k]);
        nm[k] = 0;
        for (int i = 1; i < (int)NELEM(plat); i++) if (!strcmp(nm, plat[i])) r = i;
    }
    return r;
}
int shop_career_customize(Shop *s, int entry, const unsigned char *shop) {
    if (!s) return 0;
    int type = shop ? (signed char)shop[0x50] : 0;
    s->cust_entry = entry;
    /* 8016104c: no Test Track for entry 0 / 1, no paint for entry 1 (the crib) */
    if (entry == 1) scroller_set(&s->top, k_car_shop_crib, (int)NELEM(k_car_shop_crib));
    else scroller_set(&s->top, k_car_shop, CAT_COUNT);
#ifdef OPENUG2_SIM_PRE_H
    if (getenv("OPENUG2_SIM_SHOPFOUND")) career_sim_find_shops();
#endif
    s->cshop = entry ? NULL : shop;
    s->fe_shop = shop;
    s->career = entry ? 2 : 6;                         /* GetPartUnlockFilter: the crib 2, a shop 6 */
    s->platform = shop_scene_platform(shop);           /* GarageMainScreen: the shop's scene, else CRIB01 */
    if (entry) {                                       /* CalcFirstScreen: UI_ChooseCustomizeCategory */
        s->cust_shop = 0;
        s->screen = SC_CAR_SHOP;
        printf("career customize: Change Parts (owned parts only), $%d\n", g_career.cash);
        return 1;
    }
    /* EnterShopFromFreeRoam 8015df74: 3 perf -> 1, 1 paint -> 2, 2 parts -> 0,
       5 ICE -> 3, 6 uniques -> 6; PickScreenForShop 80160498 */
    switch (type) {
    case 2:   /* UI_GenericParts_Browser: SetupBodyShop 80164368 / 0x545b50 snaps the category strip to the
                 last button or Front Bumpers (1) even when it is locked (no lock skip); not browsing parts */
        s->cust_shop = 0; s->body.sel = 0; s->body.scroll = 0.0f; body_locks(s); s->screen = SC_BODY; break;
    case 3: s->cust_shop = 1; car_shop_open(s, CAT_PERFORMANCE); break;                        /* UI_ChoosePerformanceCategory */
    case 1: s->cust_shop = 2; car_shop_open(s, CAT_GRAPHICS); break;                           /* UI_ChoosePaintCategory */
    case 5: s->cust_shop = 3; car_shop_open(s, CAT_SPECIALTIES); break;                        /* UI_ICEMAIN */
    case 6: s->cust_shop = 6; uq_enter(s); break;                                               /* UI_ChooseUniquePart */
    default:
        printf("career customize: shop type %d (UI_ChooseUniquePart) is not built yet\n", type);
        s->career = 0;
        return 0;
    }
    printf("career customize: shop type %d, screen %d, $%d\n", type, s->screen, g_career.cash);
    return 1;
}

/* LaunchFrontendShopFromGame 8012721c for the shop the career is in */
/* ---- UIMagazineReward (GC 801868fc / PC 565750, Notify 80186f18 / PC
   55b260): UI_MagazineReward.fng over UI_MagazineBack.fng, the CarViewer
   car posed for the cover. SelectNextMagazineSetup 80186b28 takes the
   showcase's FEMagazineSetup (hash +0x28; the first when there is none)
   from MAGAZINES_FRONTEND.BIN (chunk 0x30230, after it: car, render and
   magazine setup counts; FECarSetup at +0xc stride 0x38 = hash, anim flags,
   then the garage camera's view: heading, steer, FOV, target, eye;
   FERenderSetup at +0xe0c stride 0x18; FEMagazineSetup at +0x110c stride
   0x68), GenerateMagazineSelection 80185470 picks one of each list, and
   MagazineTextureHandler pack 0 (MAGAZINES_STREAMING_HIGH.bin) holds the
   art. Accept 0x406415e3: LeaveThisScreen 80186ecc -> ReturnFromFrontend-
   Reward 80186754 -> StartCareerFreeRoam (the spot saved on the way in).
   No Back. --------------------------------------------------------- */
static int mag_pick(const unsigned char *m, int cnt_off, int cap) {
    unsigned n = n2_u32(m + cnt_off);
    if (n > (unsigned)cap) n = (unsigned)cap;
    return n >= 2 ? rand() % (int)n : 0;   /* bRandom(n) */
}
int shop_magazine_reward(Shop *s, const unsigned char *sc) {
    if (!s || !sc) return 0;
    char path[600];
    snprintf(path, sizeof path, "%s/FRONTEND/MAGAZINES_FRONTEND.BIN", s->dataroot);
    long len = 0;
    unsigned char *d = n2_read_file(path, &len);
    if (!d || len < 8 + 0x110c || n2_u32(d) != 0x30230u) {
        free(d); printf("magazine: %s unreadable\n", path); return 0;
    }
    const unsigned char *b = d + 8;
    unsigned nc = n2_u32(b), nr = n2_u32(b + 4), nm = n2_u32(b + 8);
    if (nc > 64) nc = 64;
    if (nr > 32) nr = 32;
    while (nm && 8 + 0x110c + (long)nm * 0x68 > len) nm--;
    if (!nm) { free(d); printf("magazine: no magazine setups\n"); return 0; }
    uint32_t want = n2_u32(sc + 0x28);
    const unsigned char *m = b + 0x110c;                      /* GetMagazineSetupByIndex(0) */
    for (unsigned i = 0; i < nm; i++)
        if (n2_u32(b + 0x110c + i * 0x68) == want) m = b + 0x110c + i * 0x68;
    uint32_t sel[6];                                           /* FEMagazineSelection */
    sel[0] = n2_u32(m + 0x20 + 4 * mag_pick(m, 0x08, 3));     /* back */
    sel[1] = n2_u32(m + 0x2c + 4 * mag_pick(m, 0x0c, 2));     /* mast */
    sel[2] = n2_u32(m + 0x34 + 4 * mag_pick(m, 0x10, 5));     /* chick */
    sel[3] = n2_u32(m + 0x48 + 4 * mag_pick(m, 0x14, 3));     /* extra */
    sel[4] = n2_u32(m + 0x54 + 4 * mag_pick(m, 0x18, 3));     /* car setup */
    sel[5] = n2_u32(m + 0x60 + 4 * mag_pick(m, 0x1c, 2));     /* render setup */
#ifdef OPENUG2_SIM_PRE_H   /* test aid (simulator): OPENUG2_SIM_MAGCAR=<hex> forces the FECarSetup */
    if (getenv("OPENUG2_SIM_MAGCAR")) sel[4] = (uint32_t)strtoul(getenv("OPENUG2_SIM_MAGCAR"), NULL, 16);
#endif
    s->mag_cam_ok = 0; s->mag_flags = 0; s->mag_rig = 0; s->mag_env[0] = 0;
    for (unsigned i = 0; i < nc; i++) {                        /* GetCarSetupByHash: CarViewer::SetCameraParams */
        const unsigned char *cs = b + 0xc + i * 0x38;
        if (n2_u32(cs) != sel[4]) continue;
        s->mag_flags = (int)n2_u32(cs + 4);
        /* heading, steer, fov, tgt[3], eye[3], two camera +0xb4/+0xb8 floats
           (0x451200; unused), roll (0x4455a0 -> mover +0xb8, 0x444de0) */
        for (int k = 0; k < 12; k++) memcpy(&s->mag_cam[k], cs + 8 + 4 * k, 4);
        s->mag_cam_ok = 1;
    }
    for (unsigned i = 0; i < nr; i++) {                        /* GetRenderSetupByHash */
        const unsigned char *rs = b + 0xe0c + i * 0x18;
        if (n2_u32(rs) == sel[5]) {   /* SelectNextMagazineSetup: SetShaperLights + FE env map */
            s->mag_rig = n2_u32(rs + 4);
            memcpy(s->mag_env, rs + 8, 15); s->mag_env[15] = 0;
            printf("magazine: render setup %08x: shaper lights %08x, envmap %s\n",
                   (unsigned)sel[5], (unsigned)s->mag_rig, s->mag_env);
        }
    }
    printf("magazine: setup %08x%s (%.24s), back %08x mast %08x chick %08x extra %08x, car %08x%s flags %d\n",
           (unsigned)n2_u32(m), n2_u32(m) == want ? "" : " (first: no match)", (const char *)sc,
           (unsigned)sel[0], (unsigned)sel[1], (unsigned)sel[2], (unsigned)sel[3], (unsigned)sel[4],
           s->mag_cam_ok ? "" : " (missing)", s->mag_flags);
    free(d);
    /* MagazineTextureHandler::LoadTextures: pack 0 */
    glDeleteTextures(4, s->mag_tex); memset(s->mag_tex, 0, sizeof s->mag_tex);
    snprintf(path, sizeof path, "%s/FRONTEND/MAGAZINES_STREAMING_HIGH.bin", s->dataroot);
    uint32_t keys[4]; int slot[4], nk = 0;
    for (int i = 0; i < 4; i++) if (sel[i]) { keys[nk] = sel[i]; slot[nk++] = i; }
    FengImage img[4]; memset(img, 0, sizeof img);
    long plen = 0;   /* its textures are JDLZ pieces: the in-memory reader (2.9 MB) */
    unsigned char *pd = nk ? n2_read_file(path, &plen) : NULL;
    int got = pd ? feng_tpk_load(pd, plen, keys, nk, img) : 0;
    free(pd);
    for (int i = 0; i < nk; i++) {
        if (img[i].rgba) s->mag_tex[slot[i]] = upload_rgba(img[i].w, img[i].h, img[i].rgba);
        feng_image_free(&img[i]);
    }
    printf("magazine: %d of %d cover textures\n", got, nk);
    s->mag_sc = sc;
    s->mag_shot = 0; s->mag_leave = 0;   /* no photo until the save question's yes (0x115bbd44) */
    s->front_end = 1;
    s->screen = SC_MAG_REWARD;
    return 1;
}
int shop_mag_view(const Shop *s, float cam[9]) {
    if (!s || s->screen != SC_MAG_REWARD || !s->mag_cam_ok) return 0;
    memcpy(cam, s->mag_cam, 9 * sizeof(float));
    return 1;
}
int shop_mag_render(const Shop *s, uint32_t *rig, char env[16], float *roll) {
    if (!s || s->screen != SC_MAG_REWARD || !s->mag_cam_ok) return 0;
    *rig = s->mag_rig;
    memcpy(env, s->mag_env, 16);
    *roll = s->mag_cam[11];
    return 1;
}
/* UIMagazineReward 0x55b260: Continue (0x406415e3) asks 0x55b220 -- GenericDialog_SMALL, "Do you
   want to save this magazine?" 0x3312f33 / "...DVD?" 0xe9641a65 (0x838578: 1 magazine 0x53dd30,
   2 DVD 0x53dd70), Yes 0x88579775 -> 0x115bbd44, No 0xbe503981 -> 0x115bbd45. Yes: 0x7fa29c = 0,
   +0x74 = 1; the 0x55b498 tick counts it past 2 and captures (ScreenShot 0x5c3120), then 0x523ef0
   runs memcard op 0xcb1 (the photo into the profile save) whose done messages 0x172176e8 / 0x36fad7
   free the shot and leave (0x54b090: StartCareerFreeRoam). No: 0x54b090 at once, nothing saved. */
static void mag_leave(Shop *s) {
    glDeleteTextures(4, s->mag_tex); memset(s->mag_tex, 0, sizeof s->mag_tex);
    s->mag_cam_ok = 0; s->mag_flags = 0;
    printf("magazine: cover accepted\n");
    s->mag_shot = 0; s->mag_leave = 0;
    career_go(s, 0);
}
static void mag_input(Shop *s, int key) {
    if (key != SHOP_KEY_OK || s->mag_shot > 0 || s->mag_leave) return;   /* 0x406415e3 only */
    int dvd = s->mag_sc && s->mag_sc[0x24] == 4;
    dialog(s, PK_DLG_S, 2, dvd ? 0xe9641a65u : 0x3312f33u, DA_MAG_SAVE, 0x88579775u, 0xbe503981u);
    s->dlg_act2 = DA_MAG_NOSAVE;
}
static void mag_dlg(Shop *s, int act) {
    if (act == DA_MAG_SAVE) { s->mag_shot = 3; printf("magazine: save the cover photo\n"); }   /* +0x74 1 -> 3 */
    else { printf("magazine: cover not saved\n"); mag_leave(s); }
}

/* bStringHash(str, seed): the hash carried on over str */
static uint32_t hash_on(uint32_t h, const char *str) {
    while (*str) h = h * 33u + (unsigned char)*str++;
    return h;
}
/* GetNumShowcaseData(kind) PC 0x510d50 / GC 8018734c: the completed (state
   record +6 == 1) showcases of kind 1 / 3 (magazines) or 2 / 4 (DVDs) */
static int sc_of_mode(const unsigned char *sc, int mode) {
    int k = sc[0x24];
    return mode == 1 ? (k == 1 || k == 3) : (k == 2 || k == 4);
}
static int sc_done(const unsigned char *sc) {
#ifdef OPENUG2_SIM_PRE_H   /* test aid (simulator): every showcase done */
    if (getenv("OPENUG2_SIM_MAGS")) return 1;
#endif
    CareerRecord *r = career_record(n2_u32(sc + 0x20));
    return r && r->flag[CREC_DONE] == 1;
}
static int num_showcases_done(int mode) {
    int n = 0;
    for (int i = 0; i < career_nshowcases(); i++) {
        const unsigned char *sc = career_showcase(i);
        if (sc_of_mode(sc, mode) && sc_done(sc)) n++;
    }
    return n;
}

/* ---- the cover photos (ScreenShot 0x832dac). PC: once the photo is taken
   the screen asks for a screenshot (0x5ce510: a 512x512 A8R8G8B8 surface,
   0x488b50 asks the renderer), 0x5c3120 StretchRects the middle of the back
   buffer into it -- 320 x 432 of 640 x 480, left / right -10 / +3, top /
   bottom -4 / -6 -- and the 1 MB is saved with the profile (memcard op
   0xcb1, file name 0x510af0: the profile name padded to 7 with '_', then the
   showcase's index as 0-9 / A-Z). UIMagazineView loads it back (op 0xcc1)
   into the "SCREENSHOTHASH" texture on MagImage. The port keeps the file
   beside the profile, 512 x 512 RGBA top row first. ---- */
#define COVER_W 512
static int cover_path(int idx, char *out, int cap) {
    char nm[16];
    const char *p = profile_name();
    int n = (int)strlen(p);
    if (n > 7) n = 7;
    memcpy(nm, p, (size_t)n);
    while (n < 7) nm[n++] = '_';
    nm[7] = (char)(idx < 10 ? '0' + idx : 'A' + idx - 10);
    nm[8] = 0;
    return profile_file(nm, out, cap);
}
static int sc_index(const unsigned char *sc) {
    for (int i = 0; i < career_nshowcases(); i++) if (career_showcase(i) == sc) return i;
    return -1;
}
const unsigned char *shop_cover_due(Shop *s) {
    if (!s || s->screen != SC_MAG_REWARD || dlg_open(s) || s->mag_shot <= 0 || --s->mag_shot) return NULL;
    s->mag_leave = 1;   /* the save's done messages leave (0x54b090) -- next update */
    return s->mag_sc;
}
void shop_cover_capture(const unsigned char *sc, int W, int H) {
    char path[400];
    int idx = sc ? sc_index(sc) : -1;
    if (idx < 0 || idx > 35 || W <= 0 || H <= 0 || !cover_path(idx, path, sizeof path)) {
        printf("cover photo: not saved (showcase %d, profile '%s')\n", idx, profile_name());
        return;
    }
    /* PC: 320 x 432 of 640 x 480 (w by W / 640) on a 4:3 back buffer; the
       front end here keeps the cover 4:3 in the screen's height, so both
       sides (and the PC's pixel nudges) scale by H / 480 */
    int w = 320 * H / 480, h = 432 * H / 480;
    int x0 = W / 2 - w / 2 - 10 * H / 480, x1 = W / 2 + w / 2 + 3 * H / 480;     /* D3D rows: top down */
    int y0 = H / 2 - h / 2 - 4 * H / 480, y1 = H / 2 + h / 2 - 6 * H / 480;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    unsigned char *px = (unsigned char *)malloc((size_t)rw * rh * 4);
    unsigned char *out = (unsigned char *)malloc(COVER_W * COVER_W * 4);
    if (!px || !out) { free(px); free(out); return; }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x0, H - y1, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, px);   /* GL rows: bottom up */
    for (int y = 0; y < COVER_W; y++) {                      /* the StretchRect (point) */
        int sy = rh - 1 - y * rh / COVER_W;
        for (int x = 0; x < COVER_W; x++) {
            const unsigned char *q = px + ((size_t)sy * rw + x * rw / COVER_W) * 4;
            unsigned char *d = out + ((size_t)y * COVER_W + x) * 4;
            d[0] = q[0]; d[1] = q[1]; d[2] = q[2]; d[3] = 255;
        }
    }
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(out, 1, COVER_W * COVER_W * 4, f) == COVER_W * COVER_W * 4;
    if (f) fclose(f);
    free(px); free(out);
    printf("cover photo: %s %s (%.24s, %dx%d of %dx%d)\n", ok ? "saved" : "cannot save", path, (const char *)sc,
           rw, rh, W, H);
}

/* ---- uiCribRewardOptionsMain (PC Setup 0x4afb70, NotificationMessage
   0x495b60; GC 800d2c90 / 800d2b24): UI_CribRewardOptionsMain.fng. Show
   Magazines / Show DVDs, locked while GetNumShowcaseData(kind) is 0, and
   Sponsors, locked while none is chosen. The first time in on its first
   option (0x836327), after that on the one it left by. Back: FORWARD, then
   0xe1fde1d1 -> UI_CareerCrib. ---- */
enum { CRW_MAG, CRW_DVD, CRW_SPONSORS };
static const ShopEntry k_crw[3] = {
    { 0xc7993f9au, 0xff293517u, CRW_MAG },        /* CRWShowMagazines 0x4afb00 -> EnterMagazineSelect 0x53ddb0 */
    { 0x7647b08cu, 0x597df2a7u, CRW_DVD },        /* CRWShowDVDs 0x4afb20 -> EnterDvdSelect 0x53ddf0 */
    { 0xe0004602u, 0x0878ffdfu, CRW_SPONSORS },   /* CRWShowSponsors 0x4afb40 -> UI_RewardsSponsor */
};
static void crw_enter(Shop *s, int option) {
    int sel = option >= 0 && s->crw_seen ? option : 0;
    memcpy(s->crw_e, k_crw, sizeof k_crw);
    scroller_set(&s->crw, s->crw_e, 3);
    s->crw.locked[0] = num_showcases_done(1) == 0;
    s->crw.locked[1] = num_showcases_done(2) == 0;
    s->crw.locked[2] = career_num_sponsors_chosen() == 0;
#ifdef OPENUG2_SIM_PRE_H
    if (getenv("OPENUG2_SIM_SPONSOR")) s->crw.locked[2] = 0;
#endif
    s->crw_seen = 1;
    s->crw.sel = sel; s->crw.scroll = (float)sel;
    s->screen = SC_CRIB_REWARDS;
    printf("crib rewards: magazines %d, DVDs %d, sponsors %d\n", num_showcases_done(1), num_showcases_done(2),
           career_num_sponsors_chosen());
}
static void msel_enter(Shop *s, int mode, int keep);
static void crw_input(Shop *s, int key) {
    if (key == SHOP_KEY_LEFT) scroller_move(&s->crw, -1);
    else if (key == SHOP_KEY_RIGHT) scroller_move(&s->crw, 1);
    else if (key == SHOP_KEY_OK && !s->crw.locked[s->crw.sel]) {
        switch (s->crw.e[s->crw.sel].group) {
        case CRW_MAG: msel_enter(s, 1, 0); break;
        case CRW_DVD: msel_enter(s, 2, 0); break;
        case CRW_SPONSORS: csp_enter(s); break;
        }
    } else if (key == SHOP_KEY_BACK) {
        crib_return(s, CR_REWARDS);
    }
}

/* ---- UIMagazineSelect (PC create 0x55b520, Setup 0x550dd0, list 0x523f60,
   Notify 0x54b210; GC 80187494..): UI_MagazineSelect.fng, an IconScroller
   of the completed showcases of the mode -- icon 0xff293517 / 0x597df2a7,
   text bStringHash("_TITLE", +0x28). The header: "Magazines" / "DVD covers",
   "View Magazine" / "View DVD", the cover's _TITLE and _BODY, MagThumb the
   texture +0x28. The first time in on the first one (0x838581). OK
   (MagazineThing::React 0x53de60): the mag setup hash = +0x28, then
   UI_MagazineView. Back: ReturnFromMagazineSelect -> the reward options. ---- */
static void mview_enter(Shop *s, int idx);
static void msel_enter(Shop *s, int mode, int keep) {
    int n = 0;
    for (int i = 0; i < career_nshowcases() && n < 32; i++) {
        const unsigned char *sc = career_showcase(i);
        if (!sc_of_mode(sc, mode) || !sc_done(sc)) continue;
        s->msel_e[n].str = hash_on(n2_u32(sc + 0x28), "_TITLE");
        s->msel_e[n].tex = mode == 1 ? 0xff293517u : 0x597df2a7u;
        s->msel_e[n++].group = i;
    }
    int sel = keep && s->msel_seen ? s->msel.sel : 0;
    scroller_set(&s->msel, s->msel_e, n);
    if (sel >= n) sel = n ? n - 1 : 0;
    s->msel.sel = sel; s->msel.scroll = (float)sel;
    s->msel_mode = mode;
    s->msel_seen = 1;
    s->screen = SC_MAG_SELECT;
    printf("magazine select: %s, %d covers\n", mode == 1 ? "magazines" : "DVDs", n);
}
static void msel_input(Shop *s, int key) {
    if (key == SHOP_KEY_LEFT) scroller_move(&s->msel, -1);
    else if (key == SHOP_KEY_RIGHT) scroller_move(&s->msel, 1);
    else if (key == SHOP_KEY_OK && s->msel.n) mview_enter(s, s->msel_e[s->msel.sel].group);
    else if (key == SHOP_KEY_BACK) {                       /* 0x911ab364: 0x510e10, 0x838581 = 0 */
        s->msel_seen = 0;
        crw_enter(s, s->msel_mode == 1 ? 0 : 1);
    }
}

/* ---- UIMagazineView (PC ctor 0x553be0, Notify 0x565b50): 0x35f8620b makes
   the 320 x 432 "SCREENSHOTHASH" texture and loads the photo (op 0xcc1);
   0x42317a19 (loaded): MagImage shows it; 0x2b46fe08 (failed) and Back go
   back to UI_MagazineSelect. ---- */
static void mview_enter(Shop *s, int idx) {
    char path[400];
    const unsigned char *sc = career_showcase(idx);
    if (s->mview_tex) { glDeleteTextures(1, &s->mview_tex); s->mview_tex = 0; }
    unsigned char *px = NULL;
    long len = 0;
    if (sc && cover_path(idx, path, sizeof path)) px = n2_read_file(path, &len);
    if (!px || len != COVER_W * COVER_W * 4) {             /* 0x2b46fe08 */
        free(px);
        printf("magazine view: %.24s has no photo -- back to the list\n", sc ? (const char *)sc : "?");
        msel_enter(s, s->msel_mode, 1);
        return;
    }
    s->mview_tex = upload_rgba(COVER_W, COVER_W, px);
    free(px);
    s->mview_idx = idx;
    s->screen = SC_MAG_VIEW;
    printf("magazine view: %.24s (%s)\n", (const char *)sc, path);
}
static void mview_input(Shop *s, int key) {
    if (key != SHOP_KEY_BACK) return;
    if (s->mview_tex) { glDeleteTextures(1, &s->mview_tex); s->mview_tex = 0; }   /* 0x488aa0 */
    msel_enter(s, s->msel_mode, 1);
}

int shop_career_shop(Shop *s, const unsigned char *shop) {
    if (!s || !shop) return 0;
    int type = (signed char)shop[0x50];
    if (type == 0 || type > 6) {   /* FEngSwitchPackages(UI_CareerCrib.fng) */
        crib_enter(s, 0);
        return 1;
    }
    if (type != 4) {   /* 0 / >6 UI_CareerCrib.fng, 1..6 PickScreenForShop + EnterShopFromFreeRoam */
        printf("career shop %s: type %d (%s) is not built yet\n", (const char *)shop, type,
               type < 1 || type > 6 ? "UI_CareerCrib" : "the customization shops");
        return 0;
    }
    s->front_end = 1;
    /* the constructor: Setup (BuildCarList(1), InitUnlockedAndEmptySlots,
       RefreshHeader), then the first state */
    s->lot_bar = 1; s->lot_new = 0;
    lot_build(s, 1);
    career_slot_counts(&s->lot_unlocked, &s->lot_empty);
    lot_refresh(s);
    s->screen = SC_CAREER_LOT;
    s->platform = shop_scene_platform(shop);           /* the lot's scene: CARLOT01 */
    s->fe_shop = shop;
    /* 0x35f8620b: CareerSpeech::HandleFESpeechEvent(4) (fe_speech_screen)
       and AllocBackupDB (IsDirty's reference) */
    profile_snapshot();
    printf("car lot: %s, %d slots unlocked, %d empty\n", (const char *)shop, s->lot_unlocked, s->lot_empty);
    lot_enter(s, lot_next(s, LOT_START));
    return 1;
}

/* UI_VirtualKeyboard (FEKeyboard 0x4e3300). The PC types the profile name
   inline on a physical keyboard (FEngBeginTextInput 0x555f80); the pad
   port takes the console path of the same call: the virtual keyboard in
   mode 3 (text input type 6), "NAME" to start, 7 characters, A-Z / 0-9 only
   (caps forced on, shift off), header "Create User Name". The keys are the
   package's KB_SYMBOL / KB_LETTER / KB_CONTROL strings over their _BUTTON
   backings; their focus is code colours (HighlightButton 0x498a80), not
   scripts. */
enum { KBC_BACK, KBC_DONE, KBC_CAPS, KBC_SHIFT, KBC_SPACE };
typedef struct { uint32_t txt, btn; char ch; } KbKey;
/* ch: the mode-3 character (symbols 0x78df8a, letters row 1 where row 2
   allows it), 0 a blank key; the controls KBC_* */
static const KbKey k_kb[] = {
    { 0x97f52421u, 0x9e922abcu, 0 },   { 0x97f52422u, 0x9e922abdu, '1' }, { 0x97f52423u, 0x9e922abeu, '2' },
    { 0x97f52424u, 0x9e922abfu, '3' }, { 0x97f52425u, 0x9e922ac0u, '4' }, { 0x97f52426u, 0x9e922ac1u, '5' },
    { 0x97f52427u, 0x9e922ac2u, '6' }, { 0x97f52428u, 0x9e922ac3u, '7' }, { 0x97f52429u, 0x9e922ac4u, '8' },
    { 0x97f52441u, 0x9e922adcu, '9' }, { 0x97f52442u, 0x9e922addu, '0' }, { 0x97f52443u, 0x9e922adeu, 0 },
    { 0x97f52444u, 0x9e922adfu, 0 },
    { 0x378c455bu, 0xf1a466b6u, 'Q' }, { 0x378c455cu, 0xf1a466b7u, 'W' }, { 0x378c455du, 0xf1a466b8u, 'E' },
    { 0x378c455eu, 0xf1a466b9u, 'R' }, { 0x378c455fu, 0xf1a466bau, 'T' }, { 0x378c4560u, 0xf1a466bbu, 'Y' },
    { 0x378c4561u, 0xf1a466bcu, 'U' }, { 0x378c4562u, 0xf1a466bdu, 'I' }, { 0x378c4563u, 0xf1a466beu, 'O' },
    { 0x378c457bu, 0xf1a466d6u, 'P' }, { 0x378c457cu, 0xf1a466d7u, 0 },   { 0x378c457du, 0xf1a466d8u, 0 },
    { 0x378c457eu, 0xf1a466d9u, 0 },
    { 0x378c457fu, 0xf1a466dau, 'A' }, { 0x378c4580u, 0xf1a466dbu, 'S' }, { 0x378c4581u, 0xf1a466dcu, 'D' },
    { 0x378c4582u, 0xf1a466ddu, 'F' }, { 0x378c4583u, 0xf1a466deu, 'G' }, { 0x378c4584u, 0xf1a466dfu, 'H' },
    { 0x378c459cu, 0xf1a466f7u, 'J' }, { 0x378c459du, 0xf1a466f8u, 'K' }, { 0x378c459eu, 0xf1a466f9u, 'L' },
    { 0x378c459fu, 0xf1a466fau, 0 },   { 0x378c45a0u, 0xf1a466fbu, 0 },
    { 0x378c45a1u, 0xf1a466fcu, 'Z' }, { 0x378c45a2u, 0xf1a466fdu, 'X' }, { 0x378c45a3u, 0xf1a466feu, 'C' },
    { 0x378c45a4u, 0xf1a466ffu, 'V' }, { 0x378c45a5u, 0xf1a46700u, 'B' }, { 0x378c45bdu, 0xf1a46718u, 'N' },
    { 0x378c45beu, 0xf1a46719u, 'M' }, { 0x378c45bfu, 0xf1a4671au, 0 },   { 0x378c45c0u, 0xf1a4671bu, 0 },
    { 0x378c45c1u, 0xf1a4671cu, 0 },
    { 0x261a66ccu, 0xf9f25f07u, KBC_BACK }, { 0x261a66cdu, 0xf9f25f08u, KBC_DONE },
    { 0x261a66ceu, 0xf9f25f09u, KBC_CAPS }, { 0x261a66cfu, 0xf9f25f0au, KBC_SHIFT },
    { 0x261a66d0u, 0xf9f25f0bu, KBC_SPACE },
};
#define KB_N NELEM(k_kb)
#define KB_CTL 47          /* k_kb[47..51]: the controls */
#define KB_FOCUS_MS 900    /* BACK's INIT script focuses it (0x1b3909aa at 900) */
/* FEButtonMap 0x5189e0's geometric pick with the package's own links,
   up / down / left / right (wrap mode 0) */
static const signed char k_kb_nav[52][4] = {
    { -1, 13, 47, 1 }, { 37, 13, 0, 2 }, { 38, 13, 1, 3 }, { 39, 14, 2, 4 }, { 40, 15, 3, 5 }, { 41, 16, 4, 6 },
    { 42, 17, 5, 7 }, { 43, 18, 6, 8 }, { 43, 19, 7, 9 }, { 44, 20, 8, 10 }, { 45, 21, 9, 11 }, { 46, 22, 10, 12 },
    { 46, 23, 11, 47 }, { 2, 26, 25, 14 }, { 3, 27, 13, 15 }, { 4, 28, 14, 16 }, { 5, 29, 15, 17 }, { 6, 30, 16, 18 },
    { 7, 31, 17, 19 }, { 8, 32, 18, 20 }, { 9, 33, 19, 21 }, { 10, 34, 20, 22 }, { 11, 35, 21, 23 }, { 12, 36, 22, 24 },
    { 12, 48, 23, 25 }, { 47, 48, 24, 13 }, { 13, 37, 49, 27 }, { 14, 38, 26, 28 }, { 15, 39, 27, 29 }, { 16, 40, 28, 30 },
    { 17, 41, 29, 31 }, { 18, 42, 30, 32 }, { 19, 43, 31, 33 }, { 20, 44, 32, 34 }, { 21, 45, 33, 35 }, { 22, 46, 34, 36 },
    { 23, 46, 35, 48 }, { 26, 51, 50, 38 }, { 27, 51, 37, 39 }, { 28, 51, 38, 40 }, { 29, 51, 39, 41 }, { 30, 51, 40, 42 },
    { 31, 51, 41, 43 }, { 32, 51, 42, 44 }, { 33, 51, 43, 45 }, { 34, 51, 44, 46 }, { 35, 51, 45, 50 }, { 46, 25, 12, 0 },
    { 25, 46, 36, 49 }, { 13, 50, 48, 26 }, { 49, 51, 46, 37 }, { 42, -1, 40, 44 },
};
#define O_KB_TEXT 0x5dfa6642u   /* the buffer */
static const uint32_t k_kb_hidden[] = {   /* mode 3: the CAPS / SHIFT / SPACE labels, the Caps and Special prompts */
    0x261a66ceu, 0x261a66cfu, 0x261a66d0u, 0x2a7d443du, 0xca67c5a7u, 0x04c913f7u, 0xbf8ec481u,
};
static const uint32_t k_kb_typed[] = {    /* shown once a character is in (0xc98356ba): Delete, DONE */
    0xc6f00a18u, 0x64aff8d3u, 0x261a66cdu,
};
static int kb_cursor(const Shop *s) { return s->kb_fresh ? 0 : (int)strlen(s->kb_text); }
/* HighlightButton / HighlightControl: label white, backing (207,233,242,200);
   idle (50,50,50) on (84,154,192,80) */
static void kb_color(Shop *s, int k, int on) {
    const FengPackage *pk = s->pk[PK_KEYBOARD];
    int o;
    if ((o = feng_find(pk, k_kb[k].txt)) >= 0) feng_anim_color(s->kb_anim, o, on ? 0xffffffffu : 0xff323232u);
    if ((o = feng_find(pk, k_kb[k].btn)) >= 0) feng_anim_color(s->kb_anim, o, on ? 0xc8cfe9f2u : 0x50549ac0u);
}
static void kb_vis(Shop *s) {
    const FengPackage *pk = s->pk[PK_KEYBOARD];
    if (!pk || !s->kb_anim) return;
    int o, typed = kb_cursor(s) > 0;
    for (int i = 0; i < NELEM(k_kb_hidden); i++)
        if ((o = feng_find(pk, k_kb_hidden[i])) >= 0) feng_anim_hide(s->kb_anim, o, 1);
    for (int i = 0; i < NELEM(k_kb_typed); i++)
        if ((o = feng_find(pk, k_kb_typed[i])) >= 0) feng_anim_hide(s->kb_anim, o, !typed);
}
static void kb_open(Shop *s) {
    s->mc_view = MCV_KB;
    snprintf(s->kb_text, sizeof s->kb_text, "%s", str_get(s, 0x818f0385u));   /* LABEL_NAME_CAPS "NAME" */
    s->kb_fresh = 1; s->kb_sel = -1; s->kb_ms = 0;
    mc_focus(s);   /* MC_Main under it: no message, no buttons */
    const FengPackage *pk = s->pk[PK_KEYBOARD];
    if (!pk) return;
    if (!s->kb_anim) s->kb_anim = (FengAnim *)malloc(sizeof *s->kb_anim);
    if (!s->kb_anim) return;
    feng_anim_init(s->kb_anim, pk);
    anim_fadein(s->kb_anim);
    kb_vis(s);
}
static void kb_tick(Shop *s, int ms) {
    if (!s->kb_anim) return;
    feng_anim_tick(s->kb_anim, ms);
    s->kb_ms += ms;
    if (s->kb_sel < 0 && s->kb_ms >= KB_FOCUS_MS) {
        for (int k = 0; k < KB_N; k++) kb_color(s, k, 0);   /* '=' leaves its authored black too */
        s->kb_sel = KB_CTL + KBC_BACK; kb_color(s, s->kb_sel, 1);
    }
}
/* AppendChar 0x4e3390: the first edit drops "NAME", blank keys too */
static void kb_append(Shop *s, char c) {
    if (s->kb_fresh) { s->kb_fresh = 0; s->kb_text[0] = 0; }
    size_t n = strlen(s->kb_text);
    if (c && n < PROFILE_NAME_MAX) { s->kb_text[n] = c; s->kb_text[n + 1] = 0; }
    kb_vis(s);
}
/* AppendBackspace 0x4b51d0 */
static void kb_backspace(Shop *s) {
    size_t n = strlen(s->kb_text);
    if (n && s->kb_fresh) { s->kb_fresh = 0; s->kb_text[0] = 0; }
    else if (n) s->kb_text[n - 1] = 0;
    kb_vis(s);
}
/* done (0xda5b8712, blocked at cursor 0): the name, then DoSaveFlow(5) */
static void kb_done(Shop *s) {
    if (kb_cursor(s) == 0) return;
    audio_fe_play(FE_SND_SELECT);
    /* (port, user 11-14) a new profile starts with autosave on -- the original
       starts it off (0x515aa0) and a freeze lost players' races; a name that
       already has a save keeps its own setting */
    if (!profile_exists(s->kb_text)) { g_opt.autosave = 1; options_save(); printf("memcard: new profile, autosave on%c", 10); }
    profile_set_name(s->kb_text);
    s->mc_view = MCV_MSG;
    mc_focus(s);
    printf("memcard: name \"%s\"\n", s->kb_text);
    mc_flow(s, 5);
}
static void kb_input(Shop *s, int key) {
    if (key == SHOP_KEY_UP || key == SHOP_KEY_DOWN || key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) {
        if (s->kb_sel < 0) return;
        int dir = key == SHOP_KEY_UP ? 0 : key == SHOP_KEY_DOWN ? 1 : key == SHOP_KEY_LEFT ? 2 : 3;
        int k = k_kb_nav[s->kb_sel][dir];
        if (k < 0) return;
        kb_color(s, s->kb_sel, 0); s->kb_sel = k; kb_color(s, k, 1);
        static const int snd[4] = { FE_SND_UP, FE_SND_DOWN, FE_SND_LEFT, FE_SND_RIGHT };
        audio_fe_play(snd[dir]);
    } else if (key == SHOP_KEY_OK) {
        if (s->kb_sel < 0) return;
        if (s->kb_sel < KB_CTL) { audio_fe_play(FE_SND_SELECT); kb_append(s, k_kb[s->kb_sel].ch); }
        else if (k_kb[s->kb_sel].ch == KBC_BACK) { audio_fe_play(FE_SND_BACK); kb_backspace(s); }
        else if (k_kb[s->kb_sel].ch == KBC_DONE) kb_done(s);
        else audio_fe_play(FE_SND_SELECT);   /* CAPS / SHIFT / SPACE: nothing in mode 3 */
    } else if (key == SHOP_KEY_SQ) {        /* BUTTON1 "Backspace": not over "NAME" */
        if (kb_cursor(s) > 0) { audio_fe_play(FE_SND_BACK); kb_backspace(s); }
    } else if (key == SHOP_KEY_BACK) {      /* Dispose(1): cancel 0xc9d30688 */
        audio_fe_play(FE_SND_BACK);
        s->kb_text[0] = 0;
        s->mc_view = MCV_MSG; mc_focus(s);
        if (s->mc_flags & MCF_ASKED) mc_flow(s, 2);
        else if (s->mc_flags & 0x20000) mc_exit(s, 0);   /* the backup back, a cancel */
        else mc_cancel(s);
    }
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
    SIG(s->qr_track.sel); SIG(s->qr_rsel); SIG(s->qr_car_sel); SIG(s->qr_filter); SIG(s->qr_go); SIG(s->qr_cust);
    SIG(s->set); SIG(s->pending); SIG(s->fitted);
    SIG(s->opt_main.sel); SIG(s->opt_rsel); SIG(g_opt);
    SIG(s->ctl_sel); SIG(s->ctl_col); SIG(s->ctl_cap);
    SIG(s->juke_sel); SIG(s->juke_top); SIG(s->juke_grab);
    SIG(s->paintcat.sel); SIG(s->paint_mode); SIG(s->paint_sel); SIG(s->paint_brand);
    SIG(s->vlay.sel); SIG(s->vcat.sel); SIG(s->vmode);
    SIG(s->dloc.sel); SIG(s->dlev);
    SIG(s->scar.sel); SIG(s->career_go); SIG(s->lot_sel); SIG(s->lot_state);
    SIG(s->cwm.sel); SIG(s->cwm_stage);
    /* the icon strips that never set snd_done on a scroll: without them here a
       scroll played nothing (crib menu silent on HW, 11-02) */
    SIG(s->crib.sel); SIG(s->crw.sel); SIG(s->msel.sel); SIG(s->prof.sel); SIG(s->perfpkg.sel);
    SIG(s->cfm.sel); SIG(s->uqs.sel); SIG(s->icem.sel); SIG(s->neonm.sel);
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
#ifdef OPENUG2_SIM_PRE_H   /* test aid (simulator): OPENUG2_FESNDTRACE logs the generic key sounds */
    if (getenv("OPENUG2_FESNDTRACE") && key > 0 && key < (int)sizeof snd)
        printf("fesnd: screen %d key %d -> sound %d\n", (int)s->screen, key, snd[key]);
#endif
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
            if (part_excluded(s, r)) continue;   /* EXCLUDE_* by model */
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
        if (r[4] == group && !part_excluded(s, r) && db_attr(s, aoff, key, &v) && v == val) return r;
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
        if (part_excluded(s, r)) continue;
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
/* GetCarPartCost 801294f4 of a paint setup part: MapCarPartToUnlockable
   8012e8bc (base paint by its brand, vinyls by their group, the rest the
   slot's entity) at the part's level; DoesPaintPartHaveCost 8016fa6c: the
   decal layouts 0x34..0x3e are free, so are the vinyl colours (entity 0) */
static int setup_part_ent(const Shop *s, int slot, uint32_t hash, int *lvl) {
    const unsigned char *r = db_part(s, hash);
    *lvl = r ? ((r[5] | r[6] << 8) >> 5) & 7 : 0;
    if (!r || (slot > 0x33 && slot < 0x3f)) return 0;
    if (slot == 0x3f) {
        uint32_t br = 0;
        db_attr(s, r[10] | r[11] << 8, 0xebb03e66u, &br);
        return br == BR_GLOSS ? 0x25 : br == BR_PEARL ? 0x27 : br == 0x066350f0u ? 0x4e :
               br == 0x0000da27u ? 0x4d : br == 0x0016bdceu ? 0x28 : 0x26;
    }
    if (slot >= 0x40 && slot <= 0x43) { int g = r[5] & 0x1f; return g <= 0x1b ? vinyl_price_row(g) : 0; }
    return slot_ent(slot);
}
static int setup_part_cost(const Shop *s, int slot, uint32_t hash) {
    int lvl, ent = setup_part_ent(s, slot, hash, &lvl);
    return ent_price(s, ent, lvl);   /* a level-0 decal: the level-1 price */
}
/* the setup's parts: what differs from the installed graphics; with `buy`
   BuyCarPart 80129480 each (Spend + owned), InstallPaintSetup 8016f534 */
static int gfx_setup_walk(const Shop *s, int buy) {
    const N2CarConfig *a = &s->fitted, *b = &s->gfx_base;
    int sum = 0, lvl, c;
#define SETUP_PART(sl, h) do { c = setup_part_cost(s, sl, h); sum += c; \
        if (buy) career_buy_part(setup_part_ent(s, sl, h, &lvl), own_key(sl, h), c); } while (0)
    for (int i = 0; i < N2_PAINT_COUNT; i++)
        if (a->paint[i] && a->paint[i] != b->paint[i]) SETUP_PART(k_paint_cat[i].group, a->paint[i]);
    for (int l = 0; l < VINYL_LAYERS; l++)
        if (a->vinyl[l] && a->vinyl[l] != b->vinyl[l]) SETUP_PART(0x40 + l, a->vinyl[l]);
    for (int loc = 0; loc < 7; loc++)
        for (int n = 0; n < 8; n++) {
            const DecalPart *d = a->decal[loc][n] && a->decal[loc][n] != b->decal[loc][n] ? decal_by_name(a->decal[loc][n]) : NULL;
            if (d) SETUP_PART(0x61 + loc * 8 + n, d->hash);
        }
#undef SETUP_PART
    return sum;
}
/* GetPaintSetupPrice 8016fa88 */
static int gfx_setup_price(const Shop *s) { return s->career ? gfx_setup_walk(s, 0) : 0; }
/* 0x547570: the pending list into the installed setup (the career buys it) */
static void gfx_install(Shop *s) {
    if (s->career) {
        int c = gfx_setup_walk(s, 1);
        printf("career shop: graphics setup bought for $%d, $%d left\n", c, g_career.cash);
    }
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
    } else cust_up(s);
}
/* 0x562b90 (Install Setup, class 0x79b6ec): with changes "purchase this
   graphics setup for your car?" (0x7f24787; yes 0xd05fc3a3 = 0x547570),
   else "make a selection to install before checking out!" (0x51e3f647) */
static void gfx_install_ask(Shop *s) {
    /* PSPurchaseSetup::React 8030f4e4: the career's 0xa0c16cab, no cash check */
    if (gfx_changed(s)) dialog(s, PK_DLG_S, 2, s->career ? 0xa0c16cabu : 0x07f24787u, DA_GFX_INSTALL, 0, 0);
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
    case DA_OPT_LEAVE: opt_leave(s); break;
    case DA_LOT_BUY: case DA_LOT_TRADE: case DA_LOT_BROWSE: case DA_LOT_EXIT: lot_dlg(s, act); break;
    case DA_CRIB_PLAY: case DA_CRIB_MAIN: case DA_CRIB_TEST: crib_dlg(s, act); break;
    case DA_MAG_SAVE: case DA_MAG_NOSAVE: mag_dlg(s, act); break;
    case DA_CRIB_SWITCH:   /* 0xd05fc3a3: SwitchCars, FORWARD */
        career_switch_car(s->lot[s->lot_sel].handle);
        printf("crib car select: the career car is now %s (slot %08x)\n",
               airace_type_name(s->lot[s->lot_sel].type), (unsigned)s->lot[s->lot_sel].handle);
        csel_back(s);
        break;
    case DA_PAINT_ADD: paint_add(s); break;
    case DA_SHOP_LEAVE: s->exit_req = 1; break;   /* 0xd05fc3a3: ExitToRoamHelper 8015dd24 */
    case DA_UQ_PICK: uq_install(s); break;
    case DA_CWM_RACE:                           /* 0x51123592: the start request (0x837b6d) */
        s->fe_event = s->cwm.n ? s->cwm_ev[s->cwm.sel] : NULL;
        if (s->fe_event) {
            s->fe_action = SHOP_ACT_CAREER_EVENT;
            printf("crib world map: race %s\n", career_string(csp_u16(s->fe_event)));
        }
        break;
    case DA_UQ_LEAVE:                           /* 0x34dc1bec: NotifyUniqueChosen, StartCareerFreeRoam */
        career_wc_unique_chosen();
        s->uq_done = 1;
        printf("career uniques: left without a part\n");
        s->exit_req = 1;
        break;
    case DA_PART_BUY: {                         /* 0x9f00f07: BuyCarPart + InstallPart */
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        if (q) career_buy_part(q->ent, q->okey, q->price);
        printf("career shop: bought %s for $%d, $%d left\n", q ? q->name : "?", q ? q->price : 0, g_career.cash);
        s->fitted = s->preview;
        if (q && s->part_group == G_HOOD) {    /* InstallPart: UninstallHoodDecals as DA_PART */
            hood_installed(&s->fitted, q->level);
            if (memcmp(&s->fitted, &s->preview, sizeof s->fitted)) { s->preview = s->fitted; request_preview(s); }
        }
        if (s->pending) s->debounce = 0.0f;
        break;
    }
    case DA_RIM_BUY: {   /* 0x9f00f07: BuyCarPart(0x1d, the size part) */
        const ShopPart *q = rim_cur(s);
        if (q) career_buy_part(q->ent, q->okey, q->price);
        printf("career shop: bought rims %s for $%d, $%d left\n", q ? q->name : "?", q ? q->price : 0, g_career.cash);
        memcpy(s->rim_folder, s->prev_folder, sizeof s->rim_folder);
        s->rim_style = s->prev_style;
        if (s->rim_pending) s->rim_debounce = 0.0f;
        s->size_mode = 0;
        break;
    }
    case DA_PART:                               /* install what is shown */
        s->fitted = s->preview;
        if (s->part_group == G_HOOD && s->nparts) {
            hood_installed(&s->fitted, s->parts[s->part_sel].level);
            if (memcmp(&s->fitted, &s->preview, sizeof s->fitted)) { s->preview = s->fitted; request_preview(s); }
        }
        if (s->pending) s->debounce = 0.0f;
        break;
    case DA_RIM:
        memcpy(s->rim_folder, s->prev_folder, sizeof s->rim_folder);
        s->rim_style = s->prev_style;
        if (s->rim_pending) s->rim_debounce = 0.0f;
        s->size_mode = 0;   /* 0x9f00f07 installs, then 0x55fce0(0) leaves the size list */
        break;
    case DA_PERF_PKG: case DA_PERF_STOCK: perf_pkg_yes(s); break;
    case DA_PERF_PKG_BUY:     /* 0x9f00f07: parts with brands -> ChooseBrandsScreen, else 0x5501e0(1) */
        if (!brand_open(s, 1)) perf_pkg_buy(s, NULL);
        break;
    case DA_GEN_BUY: {   /* 0x9f00f07: BuyCarPart, then the install */
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        if (q) career_buy_part(q->ent, q->okey, q->price);
        printf("career shop: bought %s for $%d, $%d left\n", q ? q->name : "?", q ? q->price : 0, g_career.cash);
        if (s->buy_after != DA_GEN_BUY) dlg_act(s, s->buy_after);
        break;
    }
    case DA_PERF_PART_BUY:     /* 0x9f00f07: a part with brands -> ChooseBrandsScreen */
        if (!brand_open(s, 0)) perf_part_buy(s, 0);
        break;
    case DA_PERF_PART: perf_part_yes(s); break;
    case DA_CTL_RESET: options_ctl_preset(&g_opt, 0); break;
    case DA_QR_BAYVIEW: case DA_QR_BEYOND:   /* Free Run: the city or the World Expansion */
        g_beyond_pick = act == DA_QR_BEYOND;
        printf("quick race: Free Run in %s\n", g_beyond_pick ? "the World Expansion" : "Bayview");
        s->set[QR_FREERUN].ev = 4000; qr_enter_cars(s);
        break;
    case DA_QR_DELETE: {   /* 0x51123592: DeleteCurrentCar 800e081c */
        if (!s->qr_ncar) break;
        int sel = s->qr_car_sel, idx = s->qr_car_list[sel], slot = s->cars[idx].slot;
        if (slot < 0) break;
        if (s->qr_ncar >= 2) {   /* ScrollCars to the next car first (the list's last: to the left) */
            int ns = sel == s->qr_ncar - 1 ? sel - 1 : sel + 1;
            qr_car_id(&s->cars[s->qr_car_list[ns]], s->qr_car_cur, sizeof s->qr_car_cur);
            garage_tt_request(0, ns < sel);
        } else s->qr_filter = 0;   /* the only car of the filter: SetViewFilter(3), the list's first */
        memmove(&s->cars[idx], &s->cars[idx + 1], sizeof s->cars[0] * (size_t)(s->ncars - idx - 1));
        s->ncars--;
        qr_build_cars(s);
        s->qr_car_pending = 0;
        qr_car_moved(s);
        if (!s->qr_car_pending && s->qr_ncar) { s->qr_car_pending = 1; s->qr_car_debounce = 0.0f; }
        s->qr_del = slot + 1;
        printf("qr: tuned car %d deleted\n", slot);
        break;
    }
    case DA_TRANS_MANUAL: case DA_TRANS_AUTO:          /* 0x5f5e3886 / 0x1a2826e1, then 0xba5b5fce */
        g_opt.p[0].manual = act == DA_TRANS_MANUAL;
        printf("qr: transmission %s\n", act == DA_TRANS_MANUAL ? "manual" : "auto");
        s->qr_go = 1;
        break;
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
    case DA_GFX_LEAVE: gfx_install(s); cust_up(s); break;
    case DA_GFX_DISCARD: gfx_revert(s, GFX_ALL); cust_up(s); break;
    case DA_GFX_CANCEL: if (gfx_changed(s)) gfx_revert(s, s->gfx_cancel); break;
    default: break;
    }
}
static void ctl_input(Shop *s, int key);
/* ------------------------------------------------------------------ help */

/* Each screen's Help (0x526c40: HelpDialog_SMALL, _MED past 4 lines; the
   screen's HELP_* string, the "Help" button $JOY_EVENT_FENG_HELP$ on its
   help bar): the string of the console screen it ports. */
static uint32_t help_of(const Shop *s) {
    switch (s->mc_view) {   /* the memcard screens over the screen */
    case MCV_LIST: return s->mc_op == MCOP_BOOT ? 0x4754130eu : 0x424d7a17u;   /* HELP_LOAD_PROFILE(_BOOT_UP) */
    case MCV_KB:   return 0xcaf6d39eu;   /* HELP_VIRTUAL_KEYBOARD */
    case MCV_MSG:  return 0;
    default: break;
    }
    switch (s->screen) {
    case SC_MAIN:        return 0xd6dfcf20u;   /* HELP_MAIN_MENU */
    case SC_QR_MODE:     return 0xc6923ecdu;   /* HELP_QR_MODE_SELECT */
    case SC_QR_TRACK:    return 0xcb2afc50u;   /* HELP_QR_TRACK_PRESET */
    case SC_QR_OPTIONS:  return 0x7f2d0c79u;   /* HELP_QR_MODE_OPTIONS */
    case SC_QR_CAR:      return s->qr_cust ? 0xfab1b6bbu : 0x6c6c43feu;   /* HELP_CRIB_CAR_SELECT / HELP_QR_CAR_SELECT */
    case SC_CAREER_LOT:  return 0xc3cabafdu;   /* HELP_CAREER_CARLOT (0x7f80f4) */
    case SC_CRIB_CARSEL: return 0xfab1b6bbu;   /* HELP_CRIB_CAR_SELECT */
    case SC_CRIB_REWARDS: return 0x10965456u;  /* the PC screen table 0x7f7f58 */
    case SC_MAG_SELECT:  return s->msel_mode == 1 ? 0x82f20772u : 0x8a9e0f44u;   /* Setup: +0x28 */
    case SC_OPT_MAIN:    return 0x9323df57u;   /* HELP_OPTIONS_MAIN */
    case SC_OPT:         return 0xed586157u;   /* HELP_OPTIONS_STANDARD_MENU */
    case SC_CTL:         return 0xa33d508au;   /* HELP_CONTROLER_OPTIONS */
    case SC_JUKE:        return 0xd8f5edf7u;   /* HELP_OPTIONS_EATRAX */
    case SC_CREDITS:     return 0x844feb00u;   /* HELP_OPTIONS_CREDITS */
    case SC_CAR_SHOP:    return 0xebf23e40u;   /* HELP_SHOP_SELECT */
    case SC_BODY:        return 0x7a4c4ab2u;   /* HELP_BODY_SHOP_MAIN */
    case SC_PERFORMANCE: return 0xc3a914f6u;   /* HELP_PERFORMANCE_SHOP_MAIN */
    case SC_GRAPHICS:    return 0xa9e93ff5u;   /* HELP_GRAPHICS_SHOP_MAIN */
    case SC_SPECIALTIES: return 0x264a5f10u;   /* HELP_CARSPECIALTIES_SHOP_MAIN */
    case SC_PARTS: case SC_CARBON: return 0x43a05263u;   /* HELP_BODY_SHOP_PART_SELECT */
    case SC_RIM_BRAND:   return 0xebf23e40u;   /* HELP_SHOP_SELECT */
    case SC_RIMS:        return 0x18cdd1fcu;   /* HELP_WHEEL */
    case SC_PAINT:       return 0x0dc884cbu;   /* HELP_GRAPHICS_SHOP_PAINT_SELECT */
    case SC_PERF_PKG:    return 0x8109153du;   /* HELP_PERFORMANCE_SHOP_PACKAGE */
    case SC_VLAYER:      return 0x9da9e93du;   /* HELP_GRAPHICS_SHOP_VINYL_LAYER_SELECT */
    case SC_VCAT: case SC_DLOC: return 0xaade3025u;   /* HELP_GRAPHICS_SHOP_DECAL_SELECT_CATEGORY */
    case SC_VLIST:       return 0x11e168c1u;   /* HELP_GRAPHICS_SHOP_VINYL_SELECT */
    case SC_DPICK:       return 0x73332e68u;   /* HELP_GRAPHICS_SHOP_DECAL_SELECT */
    case SC_TINT: case SC_BULB: return 0xd3e04e45u;   /* HELP_CAR_SPECIALTIES_SUB_LEVELS */
    case SC_SPEC:        return s->spec_slot == 0x22 ? 0x6c4bf67cu   /* HELP_CARSPECIALTIES_SHOP_TRUNKLAYOUT_SELECT */
                              : s->spec_slot > 0x22 && s->spec_slot <= 0x2e ? 0xa93e10e9u   /* HELP_CAR_SPECIALTIES_CHANGE_COMPONENT */
                              : 0xd3e04e45u;   /* HELP_CAR_SPECIALTIES_SUB_LEVELS */
    case SC_NEON:        return 0x71345941u;   /* HELP_CARSPECIALTIES_NEON_SELECT */
    case SC_NEON_LIST:   return 0x846ee660u;   /* HELP_CAR_SPECIALTIES_NEON_SELECT */
    case SC_HUD:         return 0x1b2794c7u;   /* HELP_HUD_SELECT */
    case SC_HUD_COL:     return 0x4e4485feu;   /* HELP_CHANGE_COLORS */
    case SC_ICE:         return s->ice_mode == 4 ? 0xde7acec2u : 0xfd036061u;   /* ..._AUDIOCOMPONENTS_SELECT / ..._TRUNKLAYOUT_OR_COMPONENTS_SELECT */
    default:             return 0;
    }
}
/* What $JOY_EVENT_FENG_BUTTON1$ is on this screen in the port: Square opens
   the split hood / doors and previews the purge, resets the graphics
   category and clears the decals (0xc519bfc0); the jukebox grabs with
   Triangle; the car select's Square is Customize (Quick Race, BeginCarCustomize
   entry 2) or Delete Tuned Car (Customize mode 0x20, 0xefddc126). -1: not
   ported here (Player Select, Switch Players, EA Messenger) -- its prompt and
   help line go. */
static int button1_glyph(const Shop *s) {
    switch (s->screen) {
    case SC_SPEC: return s->spec_slot == 9 || s->spec_slot == 0xa0 || s->spec_slot == 0xa2 ? HUD_PAD_SQUARE : -1;
    case SC_GRAPHICS: case SC_DLOC: case SC_DPICK: return HUD_PAD_SQUARE;
    case SC_JUKE: return HUD_PAD_TRIANGLE;
    case SC_QR_CAR: return HUD_PAD_SQUARE;   /* 0xc519bfc0 (800de290 / 0x4fc1f0) */
    default: return -1;
    }
}
static int pad_token(const Shop *s, const char *q, int *glyph, const char **txt, float *adv);
/* Select: the screen's help text, less the lines of keys it has no use for
   here, in HelpDialog_SMALL / _MED */
static void help_open(Shop *s) {
    uint32_t h = help_of(s);
    const char *src = h ? str_get(s, h) : "";
    if (!*src) return;
    char out[sizeof s->dlg_text]; int n = 0, lines = 0;
    for (const char *q = src; *q; ) {
        const char *e = q;
        while (*e && *e != '^' && *e != '\n') e++;
        int keep = 0, dead = 0;
        for (const char *t = q; t < e; t++) {
            int g; const char *tt; float adv;
            int k = pad_token(s, t, &g, &tt, &adv);
            if (!k) { if (*t != ' ') keep = 1; continue; }
            if (g < 0 && !tt && adv == 0.0f) dead = 1;
            t += k - 1;
        }
        if (keep && !dead && n + (e - q) + 2 < (int)sizeof out) {
            if (n) out[n++] = '^';
            memcpy(out + n, q, (size_t)(e - q)); n += (int)(e - q);
            lines++;
        }
        q = *e ? e + 1 : e;
    }
    out[n] = 0;
    if (!n) return;
    snprintf(s->dlg_text, sizeof s->dlg_text, "%s", out);
    printf("shop: help 0x%08x, %d lines\n", h, lines);
    dialog_text(s, lines > 4 ? PK_HELP_M : PK_HELP_S, 1, s->dlg_text, DA_NONE, 0, 0);
}

static void shop_input_screen(Shop *s, int key) {
    if (dlg_open(s)) { if (key != SHOP_KEY_HELP) dlg_input(s, key); return; }
    if (s->mc_view != MCV_OFF && key != SHOP_KEY_HELP) { mc_input(s, key == SHOP_KEY_TRI ? SHOP_KEY_BACK : key); return; }
    if (key == SHOP_KEY_HELP) { help_open(s); return; }
    if (key == SHOP_KEY_TRI && s->screen != SC_CTL && s->screen != SC_JUKE) key = SHOP_KEY_BACK;   /* Triangle backs out */
    switch (s->screen) {
    case SC_SPLASH:   /* MC_Background (0x4c51f0): the boot load, once */
        if (key == SHOP_KEY_OK) {
            /* ChangeToNextBootFlowScreen: MC_Background runs the boot load before
               UI_Main exists, so no garage or car shows behind it (the UIMain
               ctor 0x4df140 builds them from the loaded profile); ExitMemcard
               goes on to UI_Main (mc_ret) */
            if (!g_boot_load_done) { g_boot_load_done = 1; mc_enter(s, 0x121, SC_MAIN); }
            else s->screen = SC_MAIN;
        }
        break;
    case SC_PROFILE:
        profile_input(s, key);
        break;
    case SC_START_CAREER:
        start_career_input(s, key);
        break;
    case SC_CAREER_LOT:
        lot_input(s, key);
        break;
    case SC_CRIB:
        crib_input(s, key);
        break;
    case SC_CRIB_CARSEL:
        csel_input(s, key);
        break;
    case SC_CRIB_SPONSORS:
        csp_input(s, key);
        break;
    case SC_CRIB_WMAP:
        cwm_input(s, key);
        break;
    case SC_MAG_REWARD:
        mag_input(s, key);
        break;
    case SC_CRIB_REWARDS:
        crw_input(s, key);
        break;
    case SC_MAG_SELECT:
        msel_input(s, key);
        break;
    case SC_MAG_VIEW:
        mview_input(s, key);
        break;
    case SC_MOVIE:   /* IG_PlayMovie: Start / Accept (both SHOP_KEY_OK) skip */
        if (key == SHOP_KEY_OK && s->mv) movie_done(s, 1);
        break;
    case SC_MAIN:
        if (key == SHOP_KEY_LEFT) scroller_move(&s->main, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->main, 1);
        else if (key == SHOP_KEY_OK && !s->main.locked[s->main.sel]) {
            if (k_main_menu[s->main.sel].group == MM_QUICK_RACE) s->screen = SC_QR_MODE;
            else if (k_main_menu[s->main.sel].group == MM_OPTIONS) s->screen = SC_OPT_MAIN;
            else if (k_main_menu[s->main.sel].group == MM_PROFILE) profile_enter(s);
            else if (k_main_menu[s->main.sel].group == MM_CAREER) start_career_enter(s);
            else { s->qr_cust = 1; g_cust_qr = 0; qr_enter_cars(s); }   /* 0x4aed30: mode 0x20, UI_QRCarSelect */
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
        else if (key == SHOP_KEY_OK && !s->top.locked[s->top.sel] && s->top.e[s->top.sel].group == PERF_TEST_TRACK) {
            /* PerfMenuStartTuning (8016104c's IconOption): the changes go to the garage first
               (the EXIT commit, main.c), then BeginCarTuning(.., entry 2) */
            s->tt_req = 1; s->exit_req = 1;
            printf("car shop: Test Track -- BeginCarTuning (entry 2)%c", 10);
        }
        else if (key == SHOP_KEY_OK && !s->top.locked[s->top.sel])
            car_shop_open(s, s->top.e == k_car_shop_crib ? k_car_shop_crib_cat[s->top.sel] : s->top.sel);
        else if (key == SHOP_KEY_BACK) s->exit_req = 1;
        break;
    case SC_BODY:
        if (key == SHOP_KEY_LEFT) scroller_move(&s->body, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->body, 1);
        else if (key == SHOP_KEY_OK && !s->body.locked[s->body.sel]) {
            if (k_body_shop[s->body.sel].group == G_WHEEL) s->screen = SC_RIM_BRAND;
            else if (k_body_shop[s->body.sel].group == G_CARBON) cf_menu(s, 0);
            else open_parts(s);
        }
        else if (key == SHOP_KEY_OK) {   /* 801615f8: a locked category -> MakeFrontendSound(0x13) = table 7 */
            audio_fe_play(FE_SND_WRONG); s->snd_done = 1;
        }
        else if (key == SHOP_KEY_BACK) cust_up(s);
        break;
    case SC_RIM_BRAND:
        if (key == SHOP_KEY_LEFT) scroller_move(&s->rims, -1);
        else if (key == SHOP_KEY_RIGHT) scroller_move(&s->rims, 1);
        else if (key == SHOP_KEY_OK) { build_rims(s, s->rims.sel); s->screen = SC_RIMS; rim_preview(s, 0.0f); }   /* the highlighted rim on the car (11-14 player report) */
        else if (key == SHOP_KEY_BACK) {   /* CalcPrevScreenFromRimBrand 80160c78: UI_GenericParts_Browser */
            /* a new PartSelectionScreen: SetupBodyShop 80164368 with DAT_803d96e4
               (StartBrowsingWheels) snaps the category strip to Rims (0x1d), not
               browsing parts -- s->parts holds the rim list now */
            s->rim_from_parts = 0; s->nparts = 0; s->part_group = -1;
            s->screen = SC_BODY;
        }
        break;
    case SC_RIMS:
        if (s->size_mode) {   /* the size list (0x55fb00): previews each size, OK buys it */
            int n = s->rim_nsizes[s->part_sel];
            if ((key == SHOP_KEY_UP || key == SHOP_KEY_DOWN) && n > 0) {   /* 0x55f740 */
                int k = s->size_sel + (key == SHOP_KEY_UP ? -1 : 1);
                if (k >= 0 && k < n) { s->size_sel = k; rim_preview(s, 0.35f); }
            } else if (key == SHOP_KEY_OK) {
                rim_accept(s);
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
            rim_preview(s, 0.35f);   /* then the new brand's highlighted rim, as Up / Down shows it */
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
            rim_accept(s);
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
        else if (key == SHOP_KEY_OK && s->screen == SC_PERFORMANCE && !s->cat.locked[s->cat.sel] &&
                 s->cat.e[s->cat.sel].group == PERF_TEST_TRACK) {
            /* DoStartTuning 801720f0: BeginCarTuning(0, career car, entry 1) */
            /* the shop's changes go to the career car first (the EXIT commit, main.c) */
            s->tt_req = 1; s->exit_req = 1;
            printf("perf shop: Test Track -- BeginCarTuning (entry 1)%c", 10);
        }
        else if (key == SHOP_KEY_OK && s->screen == SC_PERFORMANCE && !s->cat.locked[s->cat.sel])
            perf_pkg_enter(s, s->cat.e[s->cat.sel].group);
        else if (key == SHOP_KEY_BACK) cust_up(s);
        break;
    case SC_PAINT:
        paint_input(s, key);
        break;
    case SC_UNIQUES:
        uq_input(s, key);
        break;
    case SC_VLAYER: case SC_VCAT: case SC_VLIST:
        vinyl_input(s, key);
        break;
    case SC_DLOC: case SC_DPICK:
        decal_input(s, key);
        break;
    case SC_PERF_PKG:
        if (s->brand_on) brand_input(s, key);
        else perf_pkg_input(s, key);
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
            for (int t = 0; t < 8; t++) {   /* the open lists only */
                scroller_move(&s->cfm, step);
                if (s->cfm.sel < 4 && !s->cfm.locked[s->cfm.sel]) break;
            }
            if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
                s->preview = s->fitted; request_preview(s);
            }
            cf_open_parts(s);
        } else if (key == SHOP_KEY_LEFT || key == SHOP_KEY_RIGHT) {
            /* ScrollHorizontal 801631a4: nothing while browsing parts ([0x8c],
               cleared by StopBrowsingParts 80164240 on BACK) */
        } else if (key == SHOP_KEY_OK && !s->cf_mode && s->body.n && s->body.e[s->body.sel].group == G_WHEEL) {
            /* the Rims category (AddCategoryRims): StartBrowsingWheels 80163638 ->
               CalcNextScreenFromPartsBrowser 80160a64: the rim brands */
            s->rim_from_parts = 1; s->screen = SC_RIM_BRAND;
        } else if (key == SHOP_KEY_OK) {
            /* 0x566490: the part fitted -> "already installed!", else
               "Would you like to install this part now?" */
            const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
            if (q && q->available) {
                if (!memcmp(&s->preview, &s->fitted, sizeof s->preview)) dialog(s, DLG_AUTO, 1, 0xda15d7e5u, DA_NONE, 0, 0);
                else if (part_to_buy(s, q) && g_career.cash < q->price)   /* "not enough cash" */
                    dialog(s, PK_DLG_S, 1, 0xd1ac60beu, DA_NONE, 0, 0);
                else if (part_to_buy(s, q)) dialog(s, DLG_AUTO, 2, 0x44b7167fu, DA_PART_BUY, 0, 0);
                else dialog(s, DLG_AUTO, 2, 0xf5ac2b2au, DA_PART, 0, 0);
            }
        } else if (key == SHOP_KEY_BACK) {
            if (memcmp(&s->preview, &s->fitted, sizeof s->preview)) {
                s->preview = s->fitted; request_preview(s); s->debounce = 0.0f;
            }
            if (s->cf_mode) cf_menu(s, 1);
            else s->screen = SC_BODY;   /* StopBrowsingParts: the category strip (a city shop's exit dialog is BACK there) */
        }
        break;
    }
}

static int rating_shown(const Shop *s);
static void rating_update(Shop *s);
/* the screens' 0x35f8620b HandleFESpeechEvent sends (career only; GC
   caller table SPEECH_GAME 1e).  R1 11-01 gates (GC): PartSelectionScreen
   801615f8 only when gCarCustomizeManager+0x1c (customize category) == 0
   (parts); IceSelectionScreen 801695c4 (UI_ICEMAIN = the Specialties strip,
   PC mode 0x838998) only when +0x24 == 0 (the top level, not the trunk
   audio modes); ChoosePaintScreen 80177658 only when +0x28 (paint category,
   CalcNextScreenFromChoosePaintCategory 801609c8: 0 vinyl layer, 1 decals,
   2 UI_Paint) == 2, i.e. not the vinyl colours */
static void fe_speech_screen(Shop *s) {
    if (s->spk_screen == s->screen + 1) return;
    if (s->spk_screen == SC_CRIB_SPONSORS + 1) career_speech_sponsor_done();   /* HandleSponsorSelectDone */
    s->spk_screen = s->screen + 1;
    if (s->screen == SC_CRIB_SPONSORS) career_speech_sponsor();       /* HandleSponsorSelect */
    if (s->screen == SC_UNIQUES) career_speech_uniques(!s->uq_perf);  /* UniquesBrowser 0x5255d0 / 0x525610 */
    if (!s->career && s->screen != SC_CRIB && s->screen != SC_CAREER_LOT) return;
    int ev = -1;
    switch (s->screen) {
    case SC_CRIB: ev = 0x11; break;          /* uiCareerCrib */
    case SC_CAREER_LOT: ev = 4; break;       /* UICareerCarLot */
    case SC_PARTS: if (s->cust_shop == 0) ev = 2; break;   /* PartSelectionScreen */
    case SC_DLOC: ev = 5; break;             /* ChooseDecalCategory */
    case SC_SPECIALTIES: ev = 0x12; break;   /* IceSelectionScreen, mode 0 */
    case SC_PERFORMANCE: ev = 1; break;      /* ChoosePerfCategory */
    case SC_GRAPHICS: ev = 0xf; break;       /* ChoosePaintShopCategory */
    case SC_VLAYER: ev = 0xe; break;         /* ChooseVinylLayerScreen */
    case SC_PAINT: if (s->vmode < 0) ev = 8; break;   /* ChoosePaintScreen, paint category 2 */
    default: break;
    }
    if (ev >= 0) career_speech_fe(ev);
}
static int g_shop_career_mode;
#define ALT_NAME_T 0.75f                     /* GC 8032b214 / PC 0x79936c */
#define ALT_CASH_T 5.0f                      /* GC 8032b218 / PC 0x799368 */
static void alt_update(Shop *s, float dt) {
    int on = s->career || g_shop_career_mode || s->screen >= SC_CAREER_LOT;
    if (!on) { s->alt_cur = 0; return; }
    int ms = (int)(dt * 1000.0f + 0.5f);
    if (!s->alt_cur || s->screen != s->alt_screen) {   /* a new MenuScreen: SetAlternatingText(name, cash, 0.75, 5.0) */
        s->alt_screen = s->screen; s->alt_cur = 1;
        s->alt_timer = (int)(ALT_NAME_T * 4000.0f + 0.5f);
        s->alt_gen++; s->alt_ms = 0;
        return;
    }
    s->alt_ms += ms;
    if (s->alt_anim && s->alt_anim->p) feng_anim_tick(s->alt_anim, ms);
    s->alt_timer -= (int)(dt * 4000.0f);             /* Timer::CountDown 801a6bdc */
    if (s->alt_timer < 0) s->alt_timer = 0;
    if (s->alt_timer > 0) return;
    s->alt_cur = s->alt_cur == 1 ? 2 : 1;            /* Fade_Out the current, Fade_In the other */
    s->alt_timer = (int)((s->alt_cur == 2 ? ALT_CASH_T : ALT_NAME_T) * 4000.0f + 0.5f);
    s->alt_gen++; s->alt_ms = 0;
}
/* the colour of the name (0xeb406fec) / cash (0xeb3a688a) text: the alpha of
   its FADE_IN / FADE_OUT script; a second package with them in the same
   frame, or one without the scripts, shows the current text only */
static const float *alt_col(Shop *s, const FengPackage *pk, int i, const float *c, float *out) {
    uint32_t h = pk->obj[i].hash;
    if ((h != O_PLAYER && h != O_CASH) || !s->alt_cur) return c;
    memcpy(out, c, 4 * sizeof *out);
    if (!s->alt_anim && (s->alt_anim = (FengAnim *)malloc(sizeof *s->alt_anim)) != NULL) s->alt_anim->p = NULL;
    FengAnim *a = s->alt_anim;
    if (a && (a->p == pk || !s->alt_bound)) {
        if (a->p != pk || s->alt_bgen != s->alt_gen) {
            int fresh = a->p != pk, ok = 1, o;
            if (fresh) feng_anim_init(a, pk);
            if ((o = feng_find(pk, O_PLAYER)) >= 0) ok &= feng_anim_set(a, o, s->alt_cur == 1 ? 0xbcc00f05u : 0x54c20a66u, 1) != 0;
            if ((o = feng_find(pk, O_CASH)) >= 0) ok &= feng_anim_set(a, o, s->alt_cur == 2 ? 0xbcc00f05u : 0x54c20a66u, 1) != 0;
            if (fresh && s->alt_ms > 0) feng_anim_tick(a, s->alt_ms);
            s->alt_scripts = ok; s->alt_bgen = s->alt_gen;
        }
        s->alt_bound = 1;
        if (s->alt_scripts) {
            FengPlace pl; feng_anim_place(a, i, &pl);
            out[3] = pl.hidden ? 0.0f : pl.rgba[3];
            return out;
        }
    }
    if ((h == O_PLAYER) != (s->alt_cur == 1)) out[3] = 0.0f;
    return out;
}
void shop_update(Shop *s, float dt) {
    if (!s) return;
    Scroller *list[] = { &s->top, &s->body, &s->cat, &s->rims, &s->main, &s->qr_mode, &s->qr_track,
                         &s->opt_main, &s->paintcat, &s->perfpkg, &s->vlay, &s->vcat, &s->dloc, &s->prof, &s->scar, &s->cfm,
                         &s->icem, &s->neonm, &s->crib, &s->cwm };
    movie_update(s, dt);
    if (s->screen == SC_MAG_REWARD && s->mag_leave) mag_leave(s);
    s->alt_t += dt;
    alt_update(s, dt);
    s->blink += dt;
    if (rating_shown(s)) rating_update(s);
    {   /* DIM / UNDIM: 100 ms each way */
        float tgt = dlg_open(s) ? 1.0f : 0.0f, st = dt * 10.0f;
        s->dim = s->dim < tgt ? fminf(tgt, s->dim + st) : fmaxf(tgt, s->dim - st);
    }
    qr_update(s, dt);
    fe_speech_screen(s);
    if (s->screen == SC_OPT && s->opt_anim) feng_anim_tick(s->opt_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_CTL && s->ctl_anim) feng_anim_tick(s->ctl_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_JUKE && s->juke_anim) feng_anim_tick(s->juke_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_CREDITS) credits_tick(s, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_PAINT && s->paint_anim) feng_anim_tick(s->paint_anim, (int)(dt * 1000.0f + 0.5f));
    if ((s->screen == SC_HUD || s->screen == SC_HUD_COL) && s->chud_anim) feng_anim_tick(s->chud_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_PERF_PKG && s->perf_anim) feng_anim_tick(s->perf_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_PERF_PKG && s->buy_anim) feng_anim_tick(s->buy_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->screen == SC_PERF_PKG && s->brand_on && s->brand_anim) feng_anim_tick(s->brand_anim, (int)(dt * 1000.0f + 0.5f));
    if (dlg_open(s) && s->dlg_anim) feng_anim_tick(s->dlg_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->mc_view != MCV_OFF && s->mc_anim) feng_anim_tick(s->mc_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->mc_view == MCV_LIST && s->mcl_anim) feng_anim_tick(s->mcl_anim, (int)(dt * 1000.0f + 0.5f));
    if (s->mc_view == MCV_KB) kb_tick(s, (int)(dt * 1000.0f + 0.5f));
    for (int i = 0; i < (int)NELEM(list); i++) {
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
    if (s->fe_action) { a->kind = s->fe_action; a->event = s->fe_event; s->fe_action = 0; s->fe_event = NULL; return 1; }
    if (s->career_go) {                                  /* StartCareerFreeRoam */
        a->kind = SHOP_ACT_CAREER;
        a->career_nis = s->career_go == 2;
        s->career_go = 0;
        return 1;
    }
    if (s->qr_car_pending && s->qr_car_debounce <= 0.0f) {   /* the car select's car on the platform */
        s->qr_car_pending = 0;
        a->kind = SHOP_ACT_QR_CAR;
        qr_car_out(s, a);
        /* UICareerCribCarSelect (800d4d38): SetRideInfo of the slot's own ride -- two career
           cars of one model are two rides (GAPS 255) */
        a->career_handle = (s->screen == SC_CRIB_CARSEL || s->screen == SC_CAREER_LOT) && s->lot_n &&
                           s->lot[s->lot_sel].kind == 0 ? s->lot[s->lot_sel].handle : 0;   /* the lot: a stock car's handle too */
        return 1;
    }
    if (s->qr_del) {                                     /* F2.4: DeleteTunedCar */
        a->kind = SHOP_ACT_DELETE_CAR; a->car[0] = 0; a->slot = s->qr_del - 1;
        s->qr_del = 0;
        return 1;
    }
    if (s->cust_go && !s->qr_car_pending) {             /* Customize: the Car Shop for this car */
        s->cust_go = 0;
        g_qr_filter = s->qr_filter;
        g_cust_qr = s->cust_qr; s->cust_qr = 0;
        if (g_cust_qr) { memcpy(g_qr_set, s->set, sizeof g_qr_set); g_qr_set_ok = 1; g_qr_mode_sel = s->qr_mode.sel; }
        a->kind = SHOP_ACT_CUSTOMIZE;
        qr_car_out(s, a);
        return 1;
    }
    if (s->qr_go && !s->qr_car_pending) {
        s->qr_go = 0;
        memcpy(g_qr_set, s->set, sizeof g_qr_set); g_qr_set_ok = 1;
        g_qr_mode_sel = s->qr_mode.sel; g_qr_filter = s->qr_filter;
        a->kind = SHOP_ACT_QR_GO;
        qr_car_out(s, a);
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

/* A $...$ token of the console FNGs and help strings (0x51c110's button
   font): *glyph a hud_pad_glyph id, or *txt letters drawn in the font, or
   neither (*adv: a spacer's width in font units). The key a prompt names is
   the one the Vita binds: Select / Accept Cross, Cancel Circle, Button1 the
   screen's (button1_glyph), Help the Select button, the d-pad and its
   "alternate" sticks (the left stick moves the highlight, the right one
   turns the car). Returns the token's length, 0 when s is not a token. */
static int button1_glyph(const Shop *s);
static uint32_t help_of(const Shop *s);
static int pad_token(const Shop *s, const char *q, int *glyph, const char **txt, float *adv) {
    *glyph = -1; *txt = NULL; *adv = 0.0f;
    if (*q != '$') return 0;
    const char *e = strchr(q + 1, '$');
    if (!e) return 0;
    int n = (int)(e - q) + 1;
    char t[64];
    if (n >= (int)sizeof t) return 0;
    memcpy(t, q, (size_t)n); t[n] = 0;
    static const struct { const char *tok; int g; const char *txt; } k[] = {
        { "$JOY_EVENT_FENG_SELECT$", HUD_PAD_CROSS }, { "$JOY_EVENT_FENG_ACCEPT$", HUD_PAD_CROSS },
        { "$JOY_EVENT_FENG_CANCEL$", HUD_PAD_CIRCLE }, { "$JOY_EVENT_FENG_BUTTON0$", HUD_PAD_SQUARE },
        { "$JOY_EVENT_FENG_LTRIGGER$", HUD_PAD_LBTN }, { "$JOY_EVENT_FENG_RTRIGGER$", HUD_PAD_RBTN },
        { "$JOY_EVENT_FENG_START$", HUD_PAD_START },
        { "$JOY_EVENT_FENG_HELP$", -1, "SELECT" },
        { "$JOY_EVENT_FENG_PAD_UP$", HUD_PAD_DPAD_UD }, { "$JOY_EVENT_FENG_PAD_DOWN$", HUD_PAD_DPAD_UD },
        { "$JOY_EVENT_FENG_PAD_LEFT$", HUD_PAD_DPAD_LR }, { "$JOY_EVENT_FENG_PAD_RIGHT$", HUD_PAD_DPAD_LR },
        { "$JOY_EVENT_FENG_PAD_UP_ALTERNATE$", HUD_PAD_LSTICK }, { "$JOY_EVENT_FENG_PAD_DOWN_ALTERNATE$", HUD_PAD_LSTICK },
        { "$JOY_EVENT_FENG_PAD_LEFT_ALTERNATE$", HUD_PAD_LSTICK },
        { "$JOY_EVENT_FENG_PAD_RIGHT_ALTERNATE$", HUD_PAD_RSTICK } };
    for (int j = 0; j < NELEM(k); j++)
        if (!strcmp(t, k[j].tok)) { *glyph = k[j].g; *txt = k[j].txt; return n; }
    if (!strcmp(t, "$JOY_EVENT_FENG_BUTTON1$")) { *glyph = button1_glyph(s); return n; }
    if (!strcmp(t, "$ICON_SPACER$")) { *adv = 6.0f; return n; }
    return strncmp(t, "$JOY_EVENT_", 11) ? 0 : n;   /* other pads' buttons: nothing */
}
/* a line's width with its tokens (glyphs one font line square) */
static float rich_width(const Shop *s, const FengFont *f, const char *line, float sc) {
    float w = 0.0f;
    for (const char *q = line; *q; ) {
        int g; const char *tt; float adv;
        int n = pad_token(s, q, &g, &tt, &adv);
        if (!n) { char one[2] = { *q, 0 }; w += feng_text_width(f, one) * sc; q++; continue; }
        w += g >= 0 ? (float)f->line * sc : tt ? feng_text_width(f, tt) * sc : adv * sc;
        q += n;
    }
    return w;
}

static void text_at(Shop *s, const DC *dc, int p, int i, const char *txt, float tx, float ty,
                    float sc, const float *c) {
    const FengPackage *pk = s->pk[p];
    const FengObj *o = &pk->obj[i];
    if (!txt || !*txt || o->res < 0 || o->res >= pk->nres) return;
    int fi = s->font_of[p][o->res];
    if (fi < 0 || !s->font[fi].ok) return;
    const ShopFont *F = &s->font[fi];
    char line[256], wrapped[1024];
    int rich = strchr(txt, '$') != NULL;
    if (o->wrapw > 0 && !rich) { wrap_text(&F->f, txt, (float)o->wrapw, wrapped, (int)sizeof wrapped); txt = wrapped; }
    int nlines = 1;
    for (const char *q = txt; *q; q++) if (*q == '^' || *q == '\n') nlines++;
    /* 0x51c110: lines (leading Sl + the font's line) apart */
    float lh = ((float)o->lead + (float)F->f.line) * sc;
    float y = ty;
    if (o->just & FENG_JUST_VCENTER) y -= 0.5f * lh * nlines;
    else if (o->just & FENG_JUST_BOTTOM) y -= lh * nlines;
    const char *q = txt;
    while (*q) {
        int n = 0;
        while (q[n] && q[n] != '^' && q[n] != '\n' && n < (int)sizeof line - 1) { line[n] = q[n]; n++; }
        line[n] = 0;
        float x = tx, w = rich ? rich_width(s, &F->f, line, sc) : feng_text_width(&F->f, line) * sc;
        if (o->just & FENG_JUST_HCENTER) x -= 0.5f * w;
        else if (o->just & FENG_JUST_RIGHT) x -= w;
        float v[160 * 4 * 5]; int nq = 0;
        const char *tt = NULL;
        float lsc = sc, ly = y;
        {   /* a prompt that is one worded key ("SELECT" for Help): smaller,
               ending where a glyph there would, clear of its label */
            int g; const char *wt; float adv;
            if (rich && pad_token(s, line, &g, &wt, &adv) == n && wt && strlen(wt) > 1) {
                lsc = 0.7f * sc;
                x += (float)F->f.line * sc - feng_text_width(&F->f, wt) * lsc;
                ly = y + 0.3f * (float)F->f.line * sc;
            }
        }
        float sc0 = sc, y0l = y;
        sc = lsc; y = ly;
        for (const char *ch = line; (*ch || (tt && *tt)) && nq < 160; ) {
            char cur;
            if (tt && *tt) cur = *tt++;
            else {
                int g; float adv;
                int k = rich ? pad_token(s, ch, &g, &tt, &adv) : 0;
                if (k) {
                    ch += k;
                    if (g >= 0) {   /* the glyph, a font line square, between the batches */
                        draw_quads(dc, F->tex, v, nq, c, 0); nq = 0;
                        float gs = (float)F->f.line * sc, wc[4] = { 1, 1, 1, c[3] };
                        GLuint gt = hud_pad_glyph(g);
                        if (gt) blit(dc, gt, gs, gs, x + 0.5f * gs, y + 0.5f * gs, 0.0f, wc, 0);
                        x += gs;
                    } else if (!tt) x += adv * sc;
                    continue;
                }
                cur = *ch++;
            }
            const FengGlyph *g = &F->f.g[(unsigned char)cur];
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
        sc = sc0; y = y0l + lh;
        q += n;
        if (*q == '^' || *q == '\n') q++;
    }
}

/* `txt` in the object's font, size and justification, where it is authored. */
static void text(Shop *s, const DC *dc, int p, int i, const char *txt, const float *c) {
    const FengObj *o = &s->pk[p]->obj[i];
    text_at(s, dc, p, i, txt, o->ax + s->text_dx, o->ay + s->text_dy, o->size[0], c);
}

/* Split Hoods / Doors / Nitrous Purge: the second help-bar prompt the parts
   browser shows (0x54fb70 -> 0x547090: UI_ICEMAIN's Button1 icon 0x89782734
   and label 0xa936c3a2, its group 100.7 right of Install's; label
   ICE_OPEN_CLOSE_HOOD 0xeaa1d5df / ICE_OPEN_CLOSE_DOORS 0x3e9431dc /
   0xba0e1020). UI_GenericParts_Browser has only Install, so the pair is
   drawn from its select-token and label objects moved along. */
static void spec_open_prompt(Shop *s, const DC *dc, int p, uint32_t label) {
    const FengPackage *pk = s->pk[p];
    int ti = pk ? feng_find(pk, O_SELECT_TOKEN) : -1, li = pk ? feng_find(pk, 0xa936c3a2u) : -1;
    if (ti < 0 || li < 0) return;
    DC odc = *dc;
    s->text_dx = 100.7f;
    odc.pin = obj_pin(pk, ti, 1);
    text(s, &odc, p, ti, "$JOY_EVENT_FENG_BUTTON1$", s->rgba[p][ti]);
    odc.pin = obj_pin(pk, li, 1);
    text(s, &odc, p, li, str_get(s, label), s->rgba[p][li]);
    s->text_dx = 0.0f;
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


static const char *g_player_txt = "";   /* the profile's name, "No Profile" (set per frame) */
static const char *g_cash_txt = "";     /* career: the wallet ("%$d"), in turn with the name */
static int g_shop_career_mode;
void shop_set_career_mode(int on) { g_shop_career_mode = on; }
static const char *g_paint_total;   /* career: the paint setup's Total Cost (O_TOTAL in 0x9ff84a88), NULL: the screen's own */
static char g_paint_price[24];       /* career: the highlighted colour's price (0x0013c45e) */
static const char *override_text(uint32_t h, const Texts *t) {
    for (int k = 0; k < t->nx; k++) if (t->xh[k] == h) return t->xt[k];
    switch (h) {
    case O_TITLE: return t->title;
    case O_OPTION_A: case O_OPTION_B: return t->option;
    case O_PART_NAME: return t->part ? t->part : "";
    case O_PART_PRICE: return t->price ? t->price : "";
    case O_TOTAL: return g_paint_total ? g_paint_total : t->total ? t->total : "";
    case O_PLAYER: return g_player_txt;
    case O_CASH: return g_cash_txt;
    case 0x0013c45eu: return g_paint_price;
    case 0x47ff4e7cu: return "";   /* "Event Handler" debug string */
    default: return NULL;
    }
}

/* The prompts the packages author as $JOY_EVENT_FENG_...$ tokens draw as
   the Vita's buttons (text_at -> pad_token); EA Messenger has no function
   here, nor its Button1 (button1_glyph). */
static const char *authored_text(const FengObj *o) {
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

/* FEngSetInvisible(PartPrice 0x0380b632) for the next draw_package */
static void price_hide(Shop *s, int hide) {
    if (!hide || s->nov >= (int)NELEM(s->ov)) return;
    ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_PART_PRICE; ov->hide = 1;
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
        if (p == PK_LOADING) odc.pin = hud_wide_loading_tex() ? hud_wide_loading_pin(dc->asp) : -1.0f;
        /* Loading_Controller is one framed 4:3 layout: "LOADING" and its
           circles / box share a corner, the labels the picture */
        if (p == PK_LOADING_CTL) odc.pin = 0.0f;
        /* the cover screens are one 4:3 picture: the U2_magazine_texture side
           masks must meet the cover's edges, not the screen's */
        if (p == PK_MAG_REWARD || p == PK_MAG_BACK || p == PK_MAG_VIEW) odc.pin = 0.0f;
        odc.cover = o->type != FENG_STRING && fabsf(o->size[0]) >= 600.0f && fabsf(o->size[1]) >= 440.0f;
        dc = &odc;
        const float *c = s->rgba[p][i];
        /* the loading backdrop PC_load.tga: the widescreen art over the
           whole screen (hud_wide_loading_tex) */
        if (o->type != FENG_STRING && o->res >= 0 && o->res < pk->nres && odc.cover &&
            hud_is_wide_loading_res(pk->res[o->res].name) && hud_wide_loading_tex()) {
            float v[20];
            put_vert(v,      -1.0f,  1.0f, 0.0f, 0.0f);
            put_vert(v + 5,   1.0f,  1.0f, 1.0f, 0.0f);
            put_vert(v + 10,  1.0f, -1.0f, 1.0f, 1.0f);
            put_vert(v + 15, -1.0f, -1.0f, 0.0f, 1.0f);
            draw_quads(dc, hud_wide_loading_tex(), v, 1, c, 0);
            continue;
        }
        if (p == PK_LOADING || p == PK_LOADING_CTL) {   /* PC_Loading: animated by script in the game */
            uint32_t c0 = p == PK_LOADING ? 0x50d98304u : 0x50d98305u;   /* Loading_Controller's run 05..09 */
            if (o->hash == 0x5b9d84dbu) continue;                    /* "EVENT HANDLER" debug text */
            if (o->hash >= c0 && o->hash <= c0 + 4u) {               /* the five circles, lit in turn */
                float ph = fmodf(s->blink * 4.0f, 7.0f) - (float)(o->hash - c0);
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
            if (s->mc_view != MCV_OFF) continue;   /* the boot load (MC_Background): the splash has gone on */
            float b[4] = { c[0], c[1], c[2], 0.55f + 0.45f * cosf(s->blink * 3.0f) };
            /* user 11-13: "Press START button" (not the PC's "Please press
               START to begin"), centred under the logo of the widescreen
               art (hud_wide_loading_tex: the logo's centre at 37.5% of the
               width, its bottom at 35% of the height) */
            static const char k_press[] = "Press START button";
            int fi = o->res >= 0 && o->res < pk->nres ? s->font_of[p][o->res] : -1;
            if (p == PK_SPLASH && hud_wide_loading_tex() && fi >= 0 && s->font[fi].ok) {
                const FengFont *f = &s->font[fi].f;
                float sc = o->size[0], tw = feng_text_width(f, k_press) * sc;
                float lh = ((float)o->lead + (float)f->line) * sc;
                float cx = (0.375f - 0.5f) * 480.0f * dc->asp, cy = (0.405f - 0.5f) * 480.0f;
                float tx = cx - 0.5f * tw, ty = cy - 0.5f * lh;
                if (o->just & FENG_JUST_HCENTER) tx += 0.5f * tw; else if (o->just & FENG_JUST_RIGHT) tx += tw;
                if (o->just & FENG_JUST_VCENTER) ty += 0.5f * lh; else if (o->just & FENG_JUST_BOTTOM) ty += lh;
                DC pdc = *dc; pdc.pin = 0.0f; pdc.cover = 0;
                text_at(s, &pdc, p, i, k_press, tx, ty, sc, b);
            } else text(s, dc, p, i, k_press, b);
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
            for (int q = o->parent; q >= 0 && !gone; q = pk->obj[q].parent) {
                for (int k = 0; k < s->nov; k++) if (s->ov[k].hide && s->ov[k].hash == pk->obj[q].hash) gone = 1;
                /* the help bar's four arrows (group 0x2bac0cee, on the cash text): INIT runs HIDE, and only
                   the 0x2bac0cee message (0x54e6b0 / 0x553940) touches them -- never shown on a menu */
                if (pk->obj[q].hash == 0x2bac0ceeu) gone = 1;
            }
            if (gone) continue;
        }
        if (o->type == FENG_STRING) {
            if ((o->hash == O_HELP_LABEL || o->hash == O_HELP_TOKEN) && !help_of(s)) continue;
            const char *ov = override_text(o->hash, t);
            float ac[4];
            text(s, dc, p, i, ov ? ov : authored_text(o), alt_col(s, pk, i, c, ac));
            continue;
        }
        if (ov_img && ov_img->tex) {   /* the code's texture (logos, the track map) */
            float b[4] = { c[0], c[1], c[2], c[3] * (ov_img->alpha > 0.0f ? ov_img->alpha : 1.0f) };
            if (ov_img->uvo) {
                blit_uv(dc, ov_img->tex, fabsf(o->size[0]), fabsf(o->size[1]), o->ax, o->ay, o->aangle, ov_img->uv, b, 0);
                continue;
            }
            blit(dc, ov_img->tex, ov_img->geom ? ov_img->w : fabsf(o->size[0]),
                 ov_img->geom ? ov_img->h : fabsf(o->size[1]), ov_img->geom ? ov_img->x : o->ax,
                 ov_img->geom ? ov_img->y : o->ay, o->aangle, b, 0);
            continue;
        }
        /* untextured = code slots; Loading_Controller's res 0 is its U2_Menu_Box panel */
        if (o->res < (p == PK_LOADING_CTL ? 0 : 1) || o->res >= pk->nres) continue;
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

static void cwm_draw(Shop *s, const DC *dc, Texts *t) {
    const FengPackage *pk = s->pk[PK_CRIB_WMAP];
    if (!pk) return;
    const unsigned char *ev = s->cwm.n ? s->cwm_ev[s->cwm.sel] : NULL;
    const unsigned char *r = ev ? cwm_rec(s, cwm_tid(ev)) : NULL;
    static char reg[48], dist[48], best[32];
    static uint32_t xh[9]; static const char *xt[9];
    int nx = 0, type = ev ? (signed char)ev[0xf] : 0;
    cwm_fmt_d(reg, sizeof reg, str_get(s, 0x0a96b270u), s->cwm_stage);   /* "Stage %d" */
    xh[nx] = 0xc8e97c9cu; xt[nx++] = reg;
    /* FillScreen: PlayableEvent 0x496d70 / PlayableRegion 0x496ef0 (blank) */
    uint32_t tstr = type == 0 ? 0x268d063cu : type == 1 ? 0xba613729u : type == 2 ? 0x0fda6758u
                  : type == 4 ? 0x40c91ba7u : type == 5 ? 0x59ecb2c2u : 0;
    float km = r ? (float)(int)n2_u32(r + 0x98) * 0.001f : 0.0f;
    snprintf(dist, sizeof dist, "%.2f %s", km * 0.62137f, str_get(s, STR_MI));   /* 0x83aa1a 0: miles */
    if (ev && type == 5) hud_num(best, sizeof best, hs_best_drift(cwm_tid(ev), cwm_rev(ev)));
    else if (ev) hud_time_str(best, hs_best_time(cwm_tid(ev), cwm_rev(ev), r));
    xh[nx] = 0x4d2f5c61u; xt[nx++] = ev ? str_get(s, 0xed8ca191u) : "";
    xh[nx] = 0x23eae8b9u; xt[nx++] = ev ? str_get(s, career_has_completed(n2_u32(ev + 8)) ? 0xb1ee4ca9u : 0xd11ecd79u) : "";
    xh[nx] = 0x4d2f5c62u; xt[nx++] = ev ? str_get(s, 0x7f904ae8u) : "";
    xh[nx] = 0x23eae8bau; xt[nx++] = ev && tstr ? str_get(s, tstr) : "";
    xh[nx] = 0x4d2f5c63u; xt[nx++] = ev ? str_get(s, 0x7019add7u) : "";
    xh[nx] = 0x23eae8bbu; xt[nx++] = ev ? dist : "";
    xh[nx] = 0x4d2f5c64u; xt[nx++] = !ev ? "" : str_get(s, type == 5 ? 0x4e0a36a0u : type == 1 || type == 4 ? 0xdb0c5efdu : 0x3b4a1ecbu);
    xh[nx] = 0x23eae8bcu; xt[nx++] = ev ? best : "";
    t->xh = xh; t->xt = xt; t->nx = nx;
    s->nov = 0;
    ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_CWM_MAP;
    if (s->map_tex && s->map_ev == s->map_want && s->map_alpha > 0.0f) {
        /* StartZoom 0x4b4b80: no size -> SetZoom(0); snap -> SetZoom(1); else
           SetZoom over 0.5 s (its countdown, so 0 -> 1); SetZoom 0x498630:
           s = sin(p pi/2), the window S = 512 - (512 - size) s at (x s, y s) */
        float zx, zy, zs; int snap = 0;
        if (ev && r) { memcpy(&zx, r + 0x118, 4); memcpy(&zy, r + 0x11c, 4); memcpy(&zs, r + 0x120, 4); snap = (int)n2_u32(r + 0x124) != 0; }
        else { int g = s->cwm_stage >= 1 && s->cwm_stage <= 5 ? s->cwm_stage : 0;
               zx = k_cwm_region[g].x; zy = k_cwm_region[g].y; zs = k_cwm_region[g].size; }
        float p = zs <= 0.0f ? 0.0f : snap ? 1.0f : fminf(s->map_t / 0.5f, 1.0f);
        float sn = sinf(p * 1.5707964f), S = 512.0f - (512.0f - (zs > 0.0f ? zs : 512.0f)) * sn;
        ov->tex = s->map_tex; ov->alpha = s->map_alpha; ov->uvo = 1;
        ov->uv[0] = zx * sn / 512.0f; ov->uv[1] = zy * sn / 512.0f;
        ov->uv[2] = (zx * sn + S) / 512.0f; ov->uv[3] = (zy * sn + S) / 512.0f;
    } else ov->hide = 1;
    /* the pool and MINI_MASTER (the scroller's anchor) are drawn as the nodes below */
    static const uint32_t off[] = { O_CWM_ICON, O_CWM_MASTER, 0x3fbb7a4eu };
    for (int k = 0; k < (int)NELEM(off); k++) { ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = off[k]; ov->hide = 1; }
    /* RefreshHeader 0x4b1590: the region arrows, the list arrows, the side bar */
    uint32_t hide[4]; int nh = 0;
    if (s->cwm_stage <= 1) hide[nh++] = 0x444969fdu;
    if (s->cwm_stage >= s->cwm_nst) hide[nh++] = 0x444969feu;
    if (s->cwm.sel <= 0) hide[nh++] = 0x640f0f22u;
    if (s->cwm.sel >= s->cwm.n - 1) hide[nh++] = 0x640f0f23u;
    for (int k = 0; k < nh; k++) { ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = hide[k]; ov->hide = 1; }
    {   /* FEScrollBar::Update(1, count, index) 80155720, vertical along SideScrollBar's backing */
        int ihd = feng_find(pk, 0xf31e8ed3u), ibk = feng_find(pk, 0x884fba76u), n = s->cwm.n;
        ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0xf31e8ed3u;
        if (n <= 1 || ihd < 0 || ibk < 0) ov->hide = 1;
        else {
            float bh = fabsf(pk->obj[ibk].size[1]), top = pk->obj[ibk].ay - 0.5f * bh;
            float hh = fabsf(pk->obj[ihd].size[1]), f = (float)s->cwm.sel, cy;
            if (f == 0.0f) cy = top + hh * 0.5f;
            else if (f == (float)n - 1.0f) cy = top + bh - hh * 0.5f;
            else cy = f * ((bh - hh) / (float)(n - 1)) + top + hh * 0.5f;
            ov->geom = 1; ov->x = pk->obj[ihd].ax; ov->y = cy;
            ov->w = fabsf(pk->obj[ihd].size[0]); ov->h = hh;
        }
    }
    draw_package(s, dc, PK_CRIB_WMAP, t, 0, NULL, 0);
    s->nov = 0;
    /* ThumbnailScroller (Update 0x538760): vertical, pitch 16 + 20, the
       current node on MINI_MASTER, others 0.8x, the current pulsing; the
       nodes outside the 273 window hidden; all hidden while dimmed (StartDim) */
    int m = feng_find(pk, O_CWM_MASTER);
    if (m < 0 || dlg_open(s)) return;
    const FengObj *mo = &pk->obj[m];
    GLuint tex = mo->res > 0 && mo->res < pk->nres ? s->res_tex[PK_CRIB_WMAP][mo->res] : 0;
    if (!tex) return;
    DC idc = *dc; idc.cover = 0; idc.fade = 0.0f; idc.pin = obj_pin(pk, m, 1);
    float w = fabsf(mo->size[0]), h = fabsf(mo->size[1]);
    for (int i = 0; i < s->cwm.n; i++) {
        float y = mo->ay + ((float)i - s->cwm.scroll) * (h + 20.0f);
        if (fabsf(y - mo->ay) > 273.0f * 0.5f) continue;
        float k = 0.8f;
        if (i == s->cwm.sel) {
            float tri = s->cwm.pulse < 1.0f ? s->cwm.pulse : 2.0f - s->cwm.pulse;
            float a = tri * 255.0f * 1.1f + 127.5f; if (a > 255.0f) a = 255.0f;
            k = 0.97f + a * 0.000117647f;
        }
        blit(&idc, tex, w * k, h * k, mo->ax, y, 0.0f, s->rgba[PK_CRIB_WMAP][m], 0);
    }
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
            if (!tx && (o->hash == O_HELP_LABEL || o->hash == O_HELP_TOKEN) && !help_of(s)) continue;
            if (!tx) { const char *ov = override_text(o->hash, t); tx = ov ? ov : authored_text(o); }
            float ac[4];
            text_at(s, &dc, p, i, tx, pl.x, pl.y, pl.sx, alt_col(s, pk, i, pl.rgba, ac));
            continue;
        }
        GLuint tex = g_anim_tex[i]; int add = 0;
        if (tex) ;
        else if (p == PK_CHUD)   /* custom gauges: the style's art (0x549000) */
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

static void brand_draw(Shop *s, const DC *dc) {
    const FengPackage *pk = s->pk[PK_BRAND];
    if (!pk || !s->brand_anim) return;
    static const char *otxt[FENG_MAX_OBJ];
    memset(otxt, 0, sizeof otxt);
    memset(g_anim_tex, 0, sizeof g_anim_tex);
    for (int k = 0; k < s->brand_n; k++) {   /* 0x53b960 */
        uint32_t b = s->brand_w[k].b[s->brand_w[k].cur];
        int on = brand_obj(pk, QP_NAME, k + 1), od = brand_obj(pk, QP_DATA, k + 1), oi = brand_obj(pk, QP_N, k + 1);
        if (on >= 0) otxt[on] = str_get(s, s->brand_w[k].name);
        if (od >= 0) otxt[od] = perf_brand_str(b, 1);
        if (oi >= 0) {
            char di[48]; snprintf(di, sizeof di, "DECAL_ICON_%s", perf_brand_str(b, 0));
            const ShopTex *it = tex_find(s, feng_tex_key(di));
            if (!it) it = tex_find(s, 0x74ce8c0bu);   /* no icon: 0x74ce8c0b */
            g_anim_tex[oi] = it ? it->tex : 0;
        }
    }
    Texts bt; memset(&bt, 0, sizeof bt);
    s->row_arrow = K_MENU_ARROW;
    brand_marks(s);
    draw_anim_package(s, dc, PK_BRAND, s->brand_anim, &bt, otxt);
    memset(g_anim_tex, 0, sizeof g_anim_tex);
    memset(s->qr_rowobj, 0, sizeof s->qr_rowobj);
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
/* GetNumberOfStars of a configuration (the career car's at the commit: C9 GetCurrentPlayerStarRating) */
float shop_rate(const Shop *s, const N2CarConfig *c, const char *rim_folder, int rim_style) {
    return s && c ? shop_rating(s, c, rim_folder, rim_style) : 0.0f;
}
/* the same for another car than the shop's (a loaded profile's career cars:
   GetCurrentPlayerStarRating works from the car, so the stars never wait for
   a shop commit); the rim size filter of the shop's car record is not used */
float shop_rate_car(Shop *s, const char *car, const N2CarConfig *c, const char *rim_folder, int rim_style) {
    if (!s || !c || !car || !car[0]) return 0.0f;
    char keep[sizeof s->car];
    memcpy(keep, s->car, sizeof keep);
    snprintf(s->car, sizeof s->car, "%s", car);
    int rok = g_shop_rec_ok; g_shop_rec_ok = 0;
    float r = shop_rating(s, c, rim_folder ? rim_folder : "", rim_style);
    g_shop_rec_ok = rok;
    memcpy(s->car, keep, sizeof keep);
    return r;
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

/* the memcard screens: MC_Main (its message and buttons, or just the
   backdrop under the list / keyboard), MC_List, UI_VirtualKeyboard */
static void kb_draw(Shop *s, const DC *dc) {
    const FengPackage *pk = s->pk[PK_KEYBOARD];
    if (!pk || !s->kb_anim) return;
    static const char *otxt[FENG_MAX_OBJ];
    static char ch[KB_CTL][2];
    memset(otxt, 0, sizeof otxt);
    for (int k = 0; k < KB_CTL; k++) {   /* UpdateVisuals 0x4ce500: "%c" of the map, blank keys empty */
        int o = feng_find(pk, k_kb[k].txt);
        if (o < 0) continue;
        ch[k][0] = k_kb[k].ch; ch[k][1] = 0;
        otxt[o] = ch[k];
    }
    static const uint32_t ctl_str[2] = { 0x0e00af3fu, 0x0e0204f4u };   /* KB_BTN_BACK / KB_BTN_DONE */
    int o;
    for (int c = 0; c < 2; c++)
        if ((o = feng_find(pk, k_kb[KB_CTL + c].txt)) >= 0) otxt[o] = str_get(s, ctl_str[c]);
    if ((o = feng_find(pk, O_KB_TEXT)) >= 0) otxt[o] = s->kb_text;
    if ((o = feng_find(pk, O_TITLE)) >= 0) otxt[o] = str_get(s, 0x3de4eff0u);       /* LABEL_CREATE_USER_NAME */
    if ((o = feng_find(pk, 0x64aff8d3u)) >= 0) otxt[o] = str_get(s, 0x95e10f41u);   /* KB_BTN_DELETE */
    Texts t; memset(&t, 0, sizeof t);
    draw_anim_package(s, dc, PK_KEYBOARD, s->kb_anim, &t, otxt);
}
static void mc_draw(Shop *s, const DC *dc0) {
    DC dc = *dc0;
    static const char *otxt[FENG_MAX_OBJ];
    Texts t; memset(&t, 0, sizeof t);
    unsigned char keep[FENG_MAX_OBJ];
    memcpy(keep, s->qr_rowobj, sizeof keep);
    memset(s->qr_rowobj, 0, sizeof s->qr_rowobj);
    const FengPackage *pk = s->pk[PK_MC];
    if (pk && s->mc_anim) {
        memset(otxt, 0, sizeof otxt);
        int o, msg = s->mc_view == MCV_MSG;
        if ((o = feng_find(pk, O_MC_TITLE)) >= 0) otxt[o] = msg ? str_get(s, 0xb19f8b41u) : "";   /* SAVELOAD_TITLE_PC */
        if ((o = feng_find(pk, O_DLG_BLURB)) >= 0) otxt[o] = msg ? s->mc_text : "";
        for (int b = 0; b < 6; b++)
            if ((o = feng_find(pk, O_MC_BTN + (uint32_t)b)) >= 0)
                otxt[o] = msg && (s->mc_nbtn == 2 ? b < 2 : s->mc_nbtn == 1 && b == 2) ? mc_btn_text(s, b) : "";
        draw_anim_package(s, &dc, PK_MC, s->mc_anim, &t, otxt);
    }
    const FengPackage *lp = s->pk[PK_MC_LIST];
    if (s->mc_view == MCV_LIST && lp && s->mcl_anim) {
        memset(otxt, 0, sizeof otxt);
        for (int r = 0; r < MCL_ROWS; r++) {
            int k = s->mc_ltop + r, on = qr_pool(lp, QP_NAME, r + 1), od = qr_pool(lp, QP_DATA, r + 1);
            if (on >= 0) otxt[on] = k < s->mc_n ? s->mc_names[k] : "";
            if (od >= 0) otxt[od] = "";
        }
        int o;
        if ((o = feng_find(lp, O_MC_TITLE)) >= 0) otxt[o] = str_get(s, 0xc09b6bceu);   /* SAVE_LOAD_TITLE */
        if ((o = feng_find(lp, O_MCL_HEAD)) >= 0) otxt[o] = str_get(s, s->mc_op == MCOP_DELETE ? 0x56b00632u : 0x0018ecffu);
        if ((o = feng_find(lp, O_MCL_BACK)) >= 0) otxt[o] = str_get(s, s->mc_op == MCOP_BOOT ? STR_CANCEL : 0x001335f0u);
        draw_anim_package(s, &dc, PK_MC_LIST, s->mcl_anim, &t, otxt);
    }
    if (s->mc_view == MCV_KB) kb_draw(s, &dc);
    memcpy(s->qr_rowobj, keep, sizeof keep);
}

/* the movie letterboxed over black (IG_PlayMovie) */
static void movie_draw(Shop *s, const DC *dc) {
    static const float black[4] = { 0, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 };
    float v[20];
    put_vert(v, -1, -1, 0, 1); put_vert(v + 5, 1, -1, 1, 1); put_vert(v + 10, 1, 1, 1, 0); put_vert(v + 15, -1, 1, 0, 0);
    draw_quads(dc, 0, v, 1, black, 0);
    int mw = 0, mh = 0;
    GLuint mt = mplay_tex(s->mv, &mw, &mh);
    if (!mt || mh <= 0) return;
    float ma = MPLAY_ASPECT, hx = 1.0f, hy = 1.0f;
    if (dc->asp > ma) hx = ma / dc->asp; else hy = dc->asp / ma;
    put_vert(v, -hx, -hy, 0, 1); put_vert(v + 5, hx, -hy, 1, 1); put_vert(v + 10, hx, hy, 1, 0); put_vert(v + 15, -hx, hy, 0, 0);
    draw_quads(dc, mt, v, 1, white, 0);
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
    {   /* the career's Paint Shop screens show the setup's Total Cost */
        static char tot[24];
        int on = s->career && (s->screen == SC_PAINT || s->screen == SC_VLAYER || s->screen == SC_VLIST ||
                               s->screen == SC_DLOC || s->screen == SC_DPICK || s->screen == SC_GRAPHICS);
        if (on) hud_num(tot, (int)sizeof tot, gfx_setup_price(s));
        g_paint_total = on ? tot : NULL;
        g_paint_price[0] = 0;
        if (on && s->screen == SC_PAINT && s->vmode < 0 && s->paint_n && s->paint_mode)
            hud_num(g_paint_price, (int)sizeof g_paint_price, setup_part_cost(s, paint_slot(s), s->paint_hash[s->paint_sel]));
    }
    g_player_txt = profile_named() ? profile_name() : str_get(s, STR_NO_PROFILE);
    g_cash_txt = "";
    s->alt_bound = 0;
    if (s->alt_cur) {   /* SetAlternatingPlayerCash 80151d20 (the lot / crib: career mode by then): both
                           strings set, "%s" the name, "%$d" the wallet (reprinted on each switch and after a
                           buy -- the wallet only changes by a buy here, so it is printed every frame);
                           alt_col fades them in turn, the name 0.75 s, the cash 5.0 s */
        static char cash[24];
        hud_num(cash, (int)sizeof cash, g_career.cash);
        g_cash_txt = cash;
    }
    switch (s->screen) {
    case SC_START_CAREER:
        t.title = str_get(s, STR_CAREER);
        t.option = str_get(s, s->scar.e[s->scar.sel].str);
        draw_package(s, &dc, s->pk[PK_START_CAREER] ? PK_START_CAREER : PK_OPT_MAIN, &t, 0, &s->scar, 0);
        break;
    case SC_MOVIE:
        movie_draw(s, &dc);
        break;
    case SC_CRIB_SPONSORS: {   /* RefreshHeader 800d3278 */
        static char sb[6][64];
        static uint32_t xh[16]; static const char *xt[16];
        int nx = 0;
        const unsigned char *sp = s->csp_n ? s->csp[s->csp_sel] : NULL;
        const char *nm = csp_name(s);
        snprintf(sb[0], sizeof sb[0], "SPONSOR_%s", nm);
        xh[nx] = 0xf13f76b4u; xt[nx++] = sp ? str_get(s, feng_hash(sb[0])) : "";
        for (int i = 0; i < 10; i++) { xh[nx] = k_csp_terms[i]; xt[nx++] = s->csp_top + i < s->csp_nl ? s->csp_ln[s->csp_top + i] : ""; }
        snprintf(sb[1], sizeof sb[1], "%d", sp ? (int)(int16_t)csp_u16(sp + 0xc) : 0);   /* the signing bonus */
        xh[nx] = 0xf25b6bbau; xt[nx++] = sp ? sb[1] : "";
        snprintf(sb[2], sizeof sb[2], "%d", career_url_purse(g_career.stage));            /* GetURLPurse */
        xh[nx] = 0x7d8e1791u; xt[nx++] = sp ? sb[2] : "";
        snprintf(sb[3], sizeof sb[3], "%d", sp ? (int)csp_u16(sp + 2) : 0);               /* the sponsor race purse */
        xh[nx] = 0x195d8d22u; xt[nx++] = sp ? sb[3] : "";
        snprintf(sb[4], sizeof sb[4], "SPONSOR_INFO_%s", nm);
        xh[nx] = 0xf13cef3fu; xt[nx++] = sp ? str_get(s, feng_hash(sb[4])) : "";
        t.xh = xh; t.xt = xt; t.nx = nx;
        s->nov = 0;
        ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0xb04669e3u;
        snprintf(sb[5], sizeof sb[5], "DECAL_ICON_%s", nm);
        const ShopTex *it = sp ? tex_find(s, feng_tex_key(sb[5])) : NULL;
        if (it) ov->tex = it->tex; else ov->hide = 1;
        ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = feng_hash("JOY_EVENT"); ov->hide = 1;
        draw_package(s, &dc, PK_CRIB_SPONSORS, &t, 0, NULL, 0);
        s->nov = 0;
        break;
    }
    case SC_CRIB_WMAP:
        cwm_draw(s, &dc, &t);
        break;
    case SC_MAG_REWARD: {   /* the 0xc98356ba tick: Mast / Chick / Extra once loaded; the PC button "Continue" */
        static uint32_t xh[1]; static const char *xt[1];
        static const uint32_t ob[3] = { 0xb6638240u, 0x82227f6du, 0x824fa72fu };
        xh[0] = 0xb8a7c6cfu; xt[0] = str_get(s, 0xde02cf84u);
        t.xh = xh; t.xt = xt; t.nx = 1;
        s->nov = 0;
        for (int i = 0; i < 3; i++) {
            ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = ob[i];
            if (s->mag_tex[i + 1]) ov->tex = s->mag_tex[i + 1]; else ov->hide = 1;
        }
        if (s->pk[PK_MAG_REWARD]) draw_package(s, &dc, PK_MAG_REWARD, &t, 0, NULL, 0);
        s->nov = 0;
        break;
    }
    case SC_CRIB_REWARDS:
        t.option = str_get(s, s->crw.e[s->crw.sel].str);
        draw_package(s, &dc, PK_CRIB_REWARDS, &t, s->crw.locked[s->crw.sel], &s->crw, 0);
        break;
    case SC_MAG_SELECT: {   /* PopulateDataInfoStuff 0x54b150, the 0xc98356ba tick 0x54b292 */
        static uint32_t xh[4]; static const char *xt[4];
        const unsigned char *sc = s->msel.n ? career_showcase(s->msel_e[s->msel.sel].group) : NULL;
        uint32_t seed = sc ? n2_u32(sc + 0x28) : 0;
        int nx = 0;
        xh[nx] = 0x10cba23u; xt[nx++] = str_get(s, s->msel_mode == 1 ? 0x246536efu : 0xbb095ca1u);
        xh[nx] = 0x9e302291u; xt[nx++] = sc ? str_get(s, hash_on(seed, "_TITLE")) : "";
        xh[nx] = 0x3554deb1u; xt[nx++] = sc ? str_get(s, hash_on(seed, "_BODY")) : "";
        t.xh = xh; t.xt = xt; t.nx = nx;
        t.title = str_get(s, s->msel_mode == 1 ? 0xf7333312u : 0x653b26c4u);
        t.option = sc ? xt[1] : "";
        s->nov = 0;
        ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0xd29141b4u;   /* MagThumb: texture +0x28 */
        const ShopTex *th = sc ? tex_find(s, seed) : NULL;
        if (th) ov->tex = th->tex; else ov->hide = 1;
        draw_package(s, &dc, PK_MAG_SELECT, &t, 0, &s->msel, 0);
        s->nov = 0;
        break;
    }
    case SC_MAG_VIEW: {   /* the 0xc98356ba tick 0x565cc1: MagImage = the loaded photo */
        s->nov = 0;
        ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0xd1cc9c37u;
        if (s->mview_tex) ov->tex = s->mview_tex; else ov->hide = 1;
        draw_package(s, &dc, PK_MAG_VIEW, &t, 0, NULL, 0);
        s->nov = 0;
        break;
    }
    case SC_CRIB:   /* the package's own title */
        t.option = str_get(s, s->crib.e[s->crib.sel].str);
        draw_package(s, &dc, s->pk[PK_CRIB] ? PK_CRIB : PK_OPT_MAIN, &t, s->crib.locked[s->crib.sel], &s->crib, 0);
        break;
    case SC_PROFILE:
        t.title = str_get(s, STR_PROFILE_MANAGER);
        t.option = str_get(s, s->prof.e[s->prof.sel].str);
        draw_package(s, &dc, s->pk[PK_PROFILE] ? PK_PROFILE : PK_OPT_MAIN, &t, 0, &s->prof, 0);
        break;
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
        hud_num(price, (int)sizeof price, q ? q->price : 0);
        hud_num(total, (int)sizeof total, q && (q->hash != s->fitted.neon[slot][0] ||
                 (unsigned)q->value != s->fitted.neon[slot][1]) ? q->price : 0);
        t.price = price; t.total = total;
        s->nov = 0; price_hide(s, !s->career || s->cust_entry == 1);   /* RefreshHeader: PartPrice */
        draw_package(s, &dc, PK_PARTS, &t, 0, &s->neonm, 1);
        s->nov = 0;
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
        hud_num(price, (int)sizeof price, q ? q->price : 0);
        t.price = price;
        if (s->screen == SC_HUD) {   /* the overlay goes while the colours are chosen (0x563d00) */
            s->nov = 0;
            ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0x0937e8f1u; ov->hide = 1;   /* the icon pool */
            price_hide(s, !s->career || s->cust_entry == 1);   /* 0x548d20: PartPrice */
            draw_package(s, &dc, PK_CHUDOV, &t, 0, NULL, 1);
            s->nov = 0;
        }
        break;
    }
    case SC_UNIQUES:
        t.title = str_get(s, 0x9986a6e3u);                           /* RefreshHeader: 0x42adb44c */
        t.option = !s->uqs.n ? "" : s->uq[s->uqs.sel].name[0] ? s->uq[s->uqs.sel].name : str_get(s, s->uq_e[s->uqs.sel].str);
        draw_package(s, &dc, s->pk[PK_UNIQUE] ? PK_UNIQUE : PK_CATEGORY, &t, 0, &s->uqs, 0);
        break;
    case SC_CARBON:
        t.title = str_get(s, STR_BODY_SHOP);
        t.option = s->cfm.n ? str_get(s, s->cfm.e[s->cfm.sel].str) : "";
        draw_package(s, &dc, PK_CATEGORY, &t, s->cfm.locked[s->cfm.sel], &s->cfm, 0);
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
        static char rec[32];   /* FindBestDriftScore 0x502c50 / FindBestLapTime 0x502aa0 */
        int rev = q->dir && r && r[0x86] == 1;
        if (s->qr_type == QR_DRIFT) hud_num(rec, sizeof rec, hs_best_drift(ev, rev));
        else hud_time_str(rec, hs_best_time(ev, rev, r));
        const char *xt[6] = { r ? qr_track_name(s, ev, rev) : "",
                              str_get(s, qr_type_str(s->qr_type)), dist, str_get(s, best),
                              r ? rec : "",
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
        const char *xt[4] = { str_get(s, s->qr_cust ? STR_QR_CUST_TITLE : STR_QR_CAR_TITLE),
                              str_get(s, k_qr_filters[s->qr_filter].str),
                              str_get(s, qr_type_str(s->qr_type)), "" };
        /* RefreshHeader 800df4d0 mode 2: 0xa936c3a2 keeps its package text (Square =
           Customize, BeginCarCustomize entry 2) */
        t.xh = xh; t.xt = xt; t.nx = s->qr_cust ? 4 : 3;
        s->nov = 0;
        {   /* the PC's lone Accept token sits on the Customize prompt, unlabelled */
            ObjOv *h = &s->ov[s->nov++]; memset(h, 0, sizeof *h); h->hash = 0xab9f2841u; h->hide = 1;
        }
        if (s->qr_cust && c && c->slot >= 0) {   /* RefreshHeader 800df4d0 mode 0x20, a tuned car: Delete 0xefddc126 */
            xt[3] = str_get(s, 0xefddc126u);
            ObjOv *h = &s->ov[s->nov++]; memset(h, 0, sizeof *h); h->hash = O_QR_MODE_BOX; h->hide = 1;
        } else if (s->qr_cust) {   /* 0x4b2310 mode 0x20: no race; the tuned-car button (a stock car: hidden) */
            static const uint32_t ch[3] = { O_QR_MODE_BOX, O_QR_CUSTOMIZE, O_QR_CUST_BTN };
            for (int k = 0; k < 3; k++) {
                ObjOv *h = &s->ov[s->nov++]; memset(h, 0, sizeof *h); h->hash = ch[k]; h->hide = 1;
            }
        }
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
    case SC_CAREER_LOT: case SC_CRIB_CARSEL: {   /* UICareerCarLot / UICareerCribCarSelect: RefreshHeader (800d45f4 / 800d5594) */
        int pid = s->screen == SC_CRIB_CARSEL ? PK_CRIB_CARSEL : PK_CAREER_LOT;
        const FengPackage *pk = s->pk[pid];
        if (!pk) break;
        int ty = s->lot_hdr.type;
        const ShopCar *c = ty >= 0 ? lot_car_info(s, ty) : NULL;
        s->nov = 0;
        const ShopTex *ph = tex_find(s, K_GENERIC_PLACEHOLDER);
        char nm[64];
        ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_QR_LOGO;   /* GetCarTypeLogoHash(type, 1) */
        snprintf(nm, sizeof nm, "CARSELECT_MANUFACTURER_%s", c ? c->make : "");
        const ShopTex *lt = c ? tex_find(s, feng_tex_key(nm)) : NULL;
        if (!lt) lt = ph;
        if (lt) ov->tex = lt->tex; else ov->hide = 1;
        ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_QR_BADGE;         /* (type, 0) */
        snprintf(nm, sizeof nm, "SECONDARY_LOGO_%s", ty >= 0 ? airace_type_name(ty) : "");
        const ShopTex *bt = ty >= 0 ? tex_find(s, feng_tex_key(nm)) : NULL;
        if (!bt) bt = ph;
        if (bt) ov->tex = bt->tex; else ov->hide = 1;
        /* UpdateSliders 800d4560: the car select's sliders (46 in from each end) */
        float v3[3] = { c ? c->accel : 0.0f, c ? c->top_speed : 0.0f, c ? c->handling : 0.0f };
        for (int k = 0; k < 3; k++) {
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
        /* FEScrollBar::Update(1, n, idx) 80155720: one car or none hides the
           handle and the arrows; else the handle (shown for idx != 0) at
           SetPosNonResized 80155a30 along the backing, the arrows off at the ends */
        int ihd = feng_find(pk, O_LOT_HANDLE), ibk = feng_find(pk, O_LOT_BACKING);
        int n = s->lot_hdr.n, idx = s->lot_hdr.idx;
        if (n <= 1 || idx == 0 || ihd < 0 || ibk < 0) {
            ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_LOT_HANDLE; ov->hide = 1;
        } else {
            float bw = fabsf(pk->obj[ibk].size[0]), left = pk->obj[ibk].ax - 0.5f * bw;
            float hw = fabsf(pk->obj[ihd].size[0]), f = (float)idx - 1.0f, cx;
            if (f == 0.0f) cx = left + hw * 0.5f;
            else if (f == (float)n - 1.0f) cx = left + bw - hw * 0.5f;
            else cx = f * ((bw - hw) / (float)(n - 1 > 1 ? n - 1 : 1)) + left + hw * 0.5f;
            ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_LOT_HANDLE; ov->geom = 1;
            ov->x = cx; ov->y = pk->obj[ihd].ay;
            ov->w = hw; ov->h = fabsf(pk->obj[ihd].size[1]);
        }
        if (n <= 1 || s->lot_hdr.first) { ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_LOT_ARROW_1; ov->hide = 1; }
        if (n <= 1 || s->lot_hdr.last) { ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = O_LOT_ARROW_2; ov->hide = 1; }
        static char slot_t[64], lock_t[256];
        static const uint32_t csel_h[2] = { 0x6d221aa2u, 0xba5709c8u };   /* SLOT_TITLE, SPONSOR_LOCKED_TEXT */
        static const char *csel_t[2] = { slot_t, lock_t };
        if (pid == PK_CRIB_CARSEL) {
            /* Setup: FILTERTYPE, TRADE_GROUP and JOY_EVENT(141 / 142) off */
            static const uint32_t off[] = { 0xd999b6e7u, 0xd42f677bu, 0x9f24c659u, 0x9f24c67au };
            for (int k = 0; k < (int)NELEM(off); k++) { ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = off[k]; ov->hide = 1; }
            snprintf(slot_t, sizeof slot_t, "%s %d", str_get(s, 0x93475966u), s->lot_hdr.idx);   /* "%s %d" */
            int kind = s->lot_hdr.kind;
            lock_t[0] = 0;
            if (kind) {   /* CARBADGE and BOTTOM_GROUP off, SPONSOR_LOCKED_TEXT on, the logo the slot's art */
                snprintf(lock_t, sizeof lock_t, "%s", str_get(s, kind == 1 ? 0xca89507eu : 0xc9f7f7e5u));
                ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0xf5a9d520u; ov->hide = 1;
                for (int k = 0; k < s->nov; k++) if (s->ov[k].hash == O_QR_BADGE) { s->ov[k].hide = 1; s->ov[k].tex = 0; }
                const ShopTex *st = tex_find(s, kind == 1 ? 0xda665928u : K_TRACK_LOCKED);
                for (int k = 0; k < s->nov; k++)
                    if (s->ov[k].hash == O_QR_LOGO) { if (st) { s->ov[k].tex = st->tex; s->ov[k].hide = 0; } else s->ov[k].hide = 1; }
            } else { ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0xba5709c8u; ov->hide = 1; }
            t.xh = csel_h; t.xt = csel_t; t.nx = 2;
        }
        draw_package(s, &dc, pid, &t, 0, NULL, 0);
        s->nov = 0;
        break;
    }
    case SC_LOADING:
        if (s->ls_ctl && s->pk[PK_LOADING_CTL] && s->ls_pic) {   /* F1.11: SetupControllerConfig's labels, the Vita picture */
            static uint32_t xh[LSC_N + 1]; static const char *xt[LSC_N + 1];
            for (int i = 0; i < LSC_N; i++) { xh[i] = k_lsc_obj[i]; xt[i] = s->ls_lab[i] ? str_get(s, s->ls_lab[i]) : ""; }
            xh[LSC_N] = 0xbe813131u; xt[LSC_N] = "";             /* the tip: DisplayGameTip is a stub */
            t.xh = xh; t.xt = xt; t.nx = LSC_N + 1;
            s->nov = 0;
            ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov);
            ov->hash = 0x922a39c4u; ov->tex = s->ls_pic;
            draw_package(s, &dc, PK_LOADING_CTL, &t, 0, NULL, 0);
            s->nov = 0;
            break;
        }
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
            if (op >= 0) otxt[op] = s->perf_row_price[n] ? str_get(s, s->perf_row_price[n]) : s->perf_row_cash[n];
        }
        if (s->career && !s->cust_entry) {
            static const uint32_t cb[3] = { 0x06e0c9a8u, 0xbe8693e1u, 0x0380b632u };
            for (int i = 0; i < 3; i++) { int o = feng_find(pk, cb[i]); if (o >= 0) otxt[o] = s->perf_cost_txt[i]; }
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
                if (od >= 0) {   /* BuyPerfPartWidget::Draw 8030ef5c */
                    static char bc[BUY_MAX][16];
                    int i = s->buy_idx[k];
                    if (buy_installed(s, k)) otxt[od] = str_get(s, 0xed56be1fu);
                    else if (!s->career) otxt[od] = str_get(s, 0x4287206fu);
                    else if (perf_bought(s, i)) otxt[od] = str_get(s, 0x0371711cu);
                    else { hud_num(bc[k], (int)sizeof bc[k], perf_part_price(i)); otxt[od] = bc[k]; }
                }
            }
            int o;
            if ((o = feng_find(bp, O_BUY_TORQUE)) >= 0) otxt[o] = s->buy_txt[0];
            if ((o = feng_find(bp, O_BUY_POWER)) >= 0) otxt[o] = s->buy_txt[1];
            Texts bt; memset(&bt, 0, sizeof bt);
            buy_marks(s);
            draw_anim_package(s, &dc, PK_PERF_BUY, s->buy_anim, &bt, otxt);
            memset(s->qr_rowobj, 0, sizeof s->qr_rowobj);
        }
        if (s->brand_on) brand_draw(s, &dc);   /* pushed over the package screen (0x55bd60) */
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
        const ShopPart *q = rim_cur(s);
        t.title = str_get(s, STR_RIM_STYLES);
        t.option = s->rims.n ? str_get(s, s->rims.e[s->rims.sel].str) : "";
        t.part = q ? q->name : "";
        static char sized[64];   /* 0x5468a0: "%s %d\"" (0x79c1c4), the name and the INNER_RADIUS of the size list's part */
        if (q && s->size_mode) {
            snprintf(sized, sizeof sized, "%s %d\"", q->name, s->rim_sizes[s->part_sel][s->size_sel]);
            t.part = sized;
        }
        int changed = strcmp(s->prev_folder, s->rim_folder) || s->prev_style != s->rim_style;
        snprintf(price, sizeof price, "%d", q ? q->price : 0);   /* RimsBrowser 80165d20: "%d", not grouped */
        hud_num(total, (int)sizeof total, q && changed ? q->price : 0);
        t.price = price; t.total = total;
        s->nov = 0; price_hide(s, !s->career);                    /* the gamemode only */
        draw_package(s, &dc, s->pk[PK_RIMS] ? PK_RIMS : PK_PARTS, &t, 0, &s->rims, 1);
        s->nov = 0;
        break;
    }
    case SC_VLAYER: {
        t.title = str_get(s, STR_VLAYER_TITLE);
        t.option = str_get(s, s->vlay.e[s->vlay.sel].str);
        /* the arrows and the reorder hint with a vinyl to move (0x5535d0) */
        int movable = s->fitted.vinyl[s->vlay.sel] && vinyl_unique_at(&s->fitted) < 0;
        static const uint32_t hide[] = { O_VL_ARROW_L, O_VL_ARROW_R, O_VL_LTRIG, O_VL_RTRIG, O_VL_REORDER };
        s->nov = 0;
        if (!s->career) {   /* Customize, not the career: no cost (0x545460) */
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
        ObjOv *ov;
        if (!s->career) { ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0x9ff84a88u; ov->hide = 1; }
        draw_package(s, &dc, s->pk[PK_DECAL] ? PK_DECAL : PK_CATEGORY, &t, 0, &s->dloc, 0);
        if (s->screen == SC_DPICK && s->pk[PK_DECALOV]) {
            const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
            Texts ot; memset(&ot, 0, sizeof ot);
            ot.part = q ? q->name : "";
            /* ChooseDecalScreen RefreshHeader (PC 0x546f40 / GC 80168d2c): the
               price is GetCarPartCost of the LAYOUT part of the decal's
               location (MapDecalSlotToDecalLayoutSlot 0x50e5a0: 0x34 + loc,
               no wide-body translation), the paint setup's (0x5214d0) else
               the installed one, "0" with neither; hidden on "No Decal" and
               outside the career. Every layout part is level 0 (HOOD RECT
               MEDIUM / SMALL ..., upgrade 0x100 / 0x101) and SetStockParts
               80244784 installs the level-0 one in every layout slot, so with
               entity 0x4c's level-0 rule it reads the decal level-1 price */
            static char dprice[24];
            int dloc = decal_loc_of(s->dslot);
            const unsigned char *lr = decal_layout_rec(s, dloc, s->fitted.decal_layout[dloc] == 2);
            if (lr) hud_num(dprice, (int)sizeof dprice, ent_price(s, slot_ent(0x34 + dloc), (lr[5] >> 5) & 7));
            else snprintf(dprice, sizeof dprice, "0");
            ot.price = dprice;
            price_hide(s, !s->career || !q || !q->hash);
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
        hud_num(price, (int)sizeof price, q ? q->price : 0);
        hud_num(total, (int)sizeof total, changed ? q->price : 0);
        t.price = price; t.total = total;
        s->nov = 0; price_hide(s, !s->career || s->cust_entry == 1);   /* RefreshHeader: PartPrice */
        draw_package(s, &dc, PK_PARTS, &t, 0, &s->vcat, 1);
        s->nov = 0;
        break;
    }
    case SC_TINT: {
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        t.title = str_get(s, STR_SPECIALTIES);
        t.option = s->cat.n ? str_get(s, s->cat.e[s->cat.sel].str) : "";
        t.part = q ? q->name : "";
        hud_num(price, (int)sizeof price, q ? q->price : 0);
        hud_num(total, (int)sizeof total, q && tint_of(q) != s->fitted.tint ? q->price : 0);
        t.price = price; t.total = total;
        s->nov = 0; price_hide(s, !s->career || s->cust_entry == 1);   /* RefreshHeader: PartPrice */
        draw_package(s, &dc, PK_PARTS, &t, 0, &s->cat, 1);
        s->nov = 0;
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
        hud_num(price, (int)sizeof price, q ? q->price : 0);
        hud_num(total, (int)sizeof total, q && !spec_is_fitted(s, q) ? q->price : 0);
        t.price = price; t.total = total;
        s->nov = 0; price_hide(s, !s->career || s->cust_entry == 1);   /* RefreshHeader: PartPrice */
        draw_package(s, &dc, PK_PARTS, &t, 0, ice ? &s->icem : &s->cat, 1);
        s->nov = 0;
        if (s->nparts > 0 && button1_glyph(s) >= 0)
            spec_open_prompt(s, &dc, PK_PARTS, s->spec_slot == 9 ? 0xeaa1d5dfu :
                             s->spec_slot == 0xa0 ? 0x3e9431dcu : 0xba0e1020u);
        break;
    }
    case SC_BULB: {
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        t.title = str_get(s, STR_SPECIALTIES);
        t.option = s->cat.n ? str_get(s, s->cat.e[s->cat.sel].str) : "";
        t.part = q ? q->name : "";
        hud_num(price, (int)sizeof price, q ? q->price : 0);
        hud_num(total, (int)sizeof total, q && q->hash != s->fitted.bulb ? q->price : 0);
        t.price = price; t.total = total;
        s->nov = 0; price_hide(s, !s->career || s->cust_entry == 1);   /* RefreshHeader: PartPrice */
        draw_package(s, &dc, PK_PARTS, &t, 0, &s->cat, 1);
        s->nov = 0;
        break;
    }
    case SC_PARTS: {
        const ShopPart *q = s->nparts ? &s->parts[s->part_sel] : NULL;
        t.title = str_get(s, STR_BODY_SHOP);
        t.option = s->cf_mode ? str_get(s, s->cfm.e[s->cfm.sel].str) : str_get(s, s->body.e[s->body.sel].str);
        t.part = q ? q->name : "";
        int changed = memcmp(&s->preview, &s->fitted, sizeof s->preview) != 0;
        hud_num(price, (int)sizeof price, q ? q->price : 0);
        hud_num(total, (int)sizeof total, q && changed ? q->price : 0);
        t.price = price; t.total = total;
        s->nov = 0; price_hide(s, !s->career || s->cust_entry == 1);   /* RefreshHeader: PartPrice */
        draw_package(s, &dc, PK_PARTS, &t, q && !q->available, s->cf_mode ? &s->cfm : &s->body, 1);
        s->nov = 0;
        break;
    }
    }
    if (rating_shown(s)) draw_rating(s, &dc);
    if (s->mc_view != MCV_OFF) mc_draw(s, &dc);
    if (dlg_open(s) && s->dlg_anim) {         /* the dialog over the screen */
        const FengPackage *pk = s->pk[s->dlg_pk];
        static const char *otxt[FENG_MAX_OBJ];
        static unsigned char keep[FENG_MAX_OBJ];
        memset(otxt, 0, sizeof otxt);
        int o;
        if ((o = feng_find(pk, O_DLG_BLURB)) >= 0) otxt[o] = s->dlg_text;
        if ((o = feng_find(pk, O_DLG_TEXT)) >= 0) otxt[o] = s->dlg_nbtn == 2 ? (s->dlg_l1 ? s->dlg_l1 : str_get(s, s->dlg_b1)) : "";
        if ((o = feng_find(pk, O_DLG_TEXT + 1)) >= 0) otxt[o] = s->dlg_nbtn == 2 ? (s->dlg_l2 ? s->dlg_l2 : str_get(s, s->dlg_b2)) : "";
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

/* UI_MagazineBack.fng under the car (the 0xc98356ba tick: MagazineBack once
   loaded), drawn into the 3D frame before the car */
void shop_draw_under(Shop *s, const RProg *rp, int width, int height) {
    if (!s || !s->prog || s->screen != SC_MAG_REWARD || !s->pk[PK_MAG_BACK] || width <= 0 || height <= 0) return;
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
    s->nov = 0;
    ObjOv *ov = &s->ov[s->nov++]; memset(ov, 0, sizeof *ov); ov->hash = 0xb65d77fcu;
    if (s->mag_tex[0]) ov->tex = s->mag_tex[0]; else ov->hide = 1;
    draw_package(s, &dc, PK_MAG_BACK, &t, 0, NULL, 0);
    s->nov = 0;
    if (rp) glUseProgram(rp->prog);
    glDepthMask(depth_mask);
    if (depth_on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend_on) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cull_on) glEnable(GL_CULL_FACE);
}

/* the perf shop's Test Track: the EXIT it raised is BeginCarTuning(entry 1) */
int shop_take_tt(Shop *s) { int r = s ? s->tt_req : 0; if (s) s->tt_req = 0; return r; }
/* back from the test track (DoQuitTuning entry 1 -> UI_ChoosePerformanceCategory,
   FEngSetLastButton): the Test Track button selected */
void shop_perf_select_tt(Shop *s) {
    if (!s || s->screen != SC_PERFORMANCE) return;
    for (int i = 0; i < s->cat.n; i++) if (s->cat.e[i].group == PERF_TEST_TRACK) s->cat.sel = i;
}
