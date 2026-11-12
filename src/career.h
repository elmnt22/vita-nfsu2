/* career.h -- the career state (PlayerCareerState, ThePlayerCareer 0x85ad40
 * in SPEED2.EXE v1.2) over the GCareer tables (GLOBALB 0x80034a10, the
 * first container; layouts in re-tools/out/CAREER.md).
 *
 * Step C1: the state, its save, the event pile (BuildListOfEvents 0x527d30),
 * wins / losses, stage clears, sponsors and purses. Nothing here drives the
 * game yet; the world layer (C3), races (C4) and front end (C2, C5..) call in. */
#ifndef OPENUG2_CAREER_H
#define OPENUG2_CAREER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the tables ---------------------------------------------------------- */
enum { CAR_EVENT = 0x88, CAR_SHOP = 0xa0, CAR_SHOWCASE = 0x40, CAR_SMS = 0x14, CAR_STAGE = 0x50,
       CAR_SPONSOR = 0x10, CAR_CHALLENGE = 0x18, CAR_BANK = 0xc, CAR_CARUNLOCK = 0xc };

/* GLOBALB in memory; the number of events (0 = no career data) */
int  career_init(const unsigned char *g, long len);
int  career_nevents(void);
void career_debug_stage(void);                               /* sim autopilot stall report */
const unsigned char *career_event(int i);                    /* GetCareerEventDataByIndex */
const unsigned char *career_event_by_handle(uint32_t h);     /* FindCareerEventDataByHandle */
const char *career_string(unsigned off);                     /* GetCareerStringAtOffset */
const unsigned char *career_stage_data(int stage);           /* FindStageDataByStage 0x500190 */
const unsigned char *career_sponsor_by_hash(uint32_t h);     /* FindSponsorDataByName */
const unsigned char *career_sms_by_hash(uint32_t h);         /* 0x501310 */
int  career_nshowcases(void);
const unsigned char *career_showcase(int i);
int  career_nchallenges(void);
const unsigned char *career_challenge(int i);

/* ---- RuntimeCareerStageInfo (0x104, 8 at PlayerCareerState +0x72d8) ------- */
typedef struct {
    uint32_t world[35];         /* +0x00 world races won (handles) */
    uint32_t sponsor[4];        /* +0x8c sponsor races won */
    uint32_t url[10];           /* +0x9c URL races won */
    uint8_t  xc4[0x18];         /* +0xc4 */
    int32_t  dc;                /* +0xdc (world challenge kind 2 counts it) */
    float    points;            /* +0xe0 sponsor points summed */
    int32_t  npoints;           /* +0xe4 races they came from */
    const unsigned char *chosen[5];   /* +0xe8 SponsorData; [0] the chosen sponsor */
    uint8_t  cleared;           /* +0xfc */
    const unsigned char *stage; /* +0x100 StageData */
} CareerStage;

/* a state record (+0x7b00, 0x180 of them): a handle and its flags */
typedef struct { uint32_t handle; uint8_t flag[4]; } CareerRecord;
enum { CREC_ENTERED = 0, CREC_DONE = 2 };    /* flag[] index: +4 entered/seen, +6 completion */
enum { CDONE_WON = 1, CDONE_LOST = 4 };      /* eUG2EventCompletionStates at +6 */

/* the EventPile (+0x7148): 32 world challenges, 64 events, per trigger-zone counts */
typedef struct {
    const unsigned char *challenge[32];
    const unsigned char *event[64];
    uint8_t zone_count[16];
} CareerPile;

/* SMSMessageQueue (+0x5d20; save 0x5014f0, load 0x514e80): 0x100 slots, C8 runs it */
typedef struct {
    const unsigned char *msg;         /* +0 SMSMessageData (saved as bStringHash of its name) */
    uint8_t  b[4];                    /* +4..+7 */
    uint32_t u[3];                    /* +8, +0xc, +0x10 */
} CareerSmsSlot;
typedef struct {
    CareerSmsSlot slot[0x100];
    uint32_t u1400, u1404;
    uint8_t  b140c;
} CareerSmsQueue;

typedef struct {
    int32_t  cash;                    /* +0x7134 CareerWallet */
    uint8_t  crib_in_event;           /* +0x8868 a crib entered with an event current (GC +0x7f58) */
    uint32_t u872c;
    uint8_t  b873e, b873f;
    int32_t  stage;                   /* +0x7af8 */
    int32_t  nrec;                    /* +0x7afc */
    CareerRecord rec[0x180];          /* +0x7b00 */
    uint8_t  started, b873d;          /* +0x873c the first career movie has run (GC +0x7e28, 0x803bbecc) */
    uint32_t career_hash;             /* +0x8740 the career car's FEPlayerCarDB handle (GetCurrentCareerCar;
                                         Default: FEHashUpper("CAREER_0")) */
    uint8_t  has_spawn;               /* +0x8744 SaveSpawnPosition 801266dc / GetRespawnParameters 8012765c */
    float    spawn[4];                /* +0x8750 x, y, z (+ pad) */
    uint16_t spawn_heading;           /* +0x8760 */
    float    f8764;                   /* -1 */
    uint8_t  a8768[0x100];
    uint8_t  a8869[0x10];             /* Default: all 1 */
    CareerSmsQueue sms;               /* +0x5d20 */
    uint8_t  speech[0x190];           /* +0x887c CareerSpeech (C10) */
    uint32_t current_event;           /* +0x8738 -> +8, the event being raced */
    CareerStage st[8];                /* +0x72d8 */
    CareerPile pile;                  /* +0x7148 */
    struct {                          /* Stage0StateManager (GC +0x6824), not saved */
        int state;                    /* 0 none, 1..8 the stage-0 intro, 9 done */
        uint32_t t0;                  /* ms */
        int read_msg, sms_dismissed;
    } s0;
    uint32_t engaged_preset;          /* the engaged event's preset ride (+0x7e1c -> +0x2c), C3 */
    int      have_suv_car;            /* 0x803bbed0 && current car IsSUV */
    int      has_car;
    const unsigned char *shop;        /* +0x7e20 the shop we are in (NotifyEnterShopFromFreeRoam;
                                         GetShopWeAreCurrentlyIn), not saved */
    const unsigned char *engaged_event;   /* +0x7e1c the event NotifyEnterEvent engaged (its race), not saved */
    struct {                          /* WorldChallengeState (+0xc00, GC 0x803b4ca4), not saved */
        int failed;                   /* +0 */
        float timer;                  /* +4 the seconds left */
        const unsigned char *wc;      /* +8 the WorldChallengeData running (IsActive) */
        uint16_t heading;             /* +0x118 the car's at the start (the retry's) */
        int state;                    /* +0x11c CWC_* */
    } wcs;
    float career_time;                /* +0x7e54 the career time (s, free roam; not saved) */
} CareerState;
/* WorldChallengeState +0x11c */
enum { CWC_NONE, CWC_RUNNING, CWC_AT_SHOP, CWC_AT_SHOWCASE, CWC_IN_SHOP, CWC_DONE, CWC_FAILED,
       CWC_FAIL_ACCEPTED, CWC_RETRY };

extern CareerState g_career;

void career_default(void);                                    /* Default 0x532830 */
CareerRecord *career_record(uint32_t h);                      /* GetStateRecord 0x5003b0 */
CareerRecord *career_add_record(uint32_t h);                  /* AddStateRecord 0x512f10 */
int  career_has_completed(uint32_t h);                        /* HasCompletedEvent 0x500b80 */
int  career_completed_career(void);                           /* HasCompletedCareer */
int  career_all_events_done(int stage);                       /* AllEventsInStageCompleted */
int  career_stage_cleared(int stage);                         /* HasCompletedStage */

int  cstage_world_done(const CareerStage *s);
int  cstage_url_done(const CareerStage *s);
int  cstage_sponsor_done(const CareerStage *s);
int  cstage_can_do_url(const CareerStage *s, const unsigned char *ev);
int  cstage_need_sponsor(const CareerStage *s);               /* NeedToAskForSponsor */
const unsigned char *cstage_sponsor_choice(const CareerStage *s, int i);
float cstage_avg_points(const CareerStage *s);
float cstage_ai_level(const CareerStage *s);                  /* GetAIPerformanceMatchingParameter */
int  cstage_num_sponsors_to_choose(const CareerStage *s, float avg);
int  career_num_sponsors_chosen(void);
/* GetCurrentPlayerStarRating 80126420 = StarGazerGuide::GetNumberOfStars of the
   career car (the visual rating from part reps): with the shop rating, C6/C9 */

/* the event pile for the current stage (BuildListOfEvents) */
void career_build_events(void);
int  career_event_in_pile(const unsigned char *ev);           /* IsEventEnterable */
int  career_event_on_radar(const unsigned char *ev);          /* IsEventVisibleOnRadar */
const unsigned char *career_random_hidden_event(void);

/* results: margin = the margin of victory (EventInProgressState, C4); won
   with the reward still claimable. CommitWinEvent 0x541420 / CommitLoseEvent */
void career_commit_win(uint32_t h, int reward, float margin);
void career_commit_lose(uint32_t h);
void career_choose_sponsor(const unsigned char *sponsor);     /* NotifyChooseSponsor */
float career_rep_for_margin(float margin, const unsigned char *ev);   /* 0x512b50 */

int  career_event_purse(const unsigned char *ev);             /* GetEventPurse 0x500c30 */
int  career_url_purse(int stage);
int  career_random_encounter_purse(int stage);
int  career_star_rating_purse(int stage);

/* ---- starting a career (C2; CAREER.md "C2: starting a career") --------- */
#define CAREER_DDAY_A 0xdd60e402u     /* enter CC_CAR_LOT_1 in the DDAY_PLAYER_CAR preset */
#define CAREER_DDAY_B 0xdd60e403u     /* enter CRIB_1: stage 0's final event */
enum { CSTART_CRIB, CSTART_FIRST_MOVIE, CSTART_FREE_ROAM };
int  career_start(void);                                      /* StartCareerHelper 800d1b30 */
int  career_first_movie_done(void);                           /* DoWhatNext 6 + StartCareerFreeRoam: 1 = the start NIS */
int  career_free_roam_start(void);                            /* StartCareerFreeRoam (after a CSTART_FREE_ROAM) */
void career_enter_shop_event(const unsigned char *ev);        /* NotifyEnterShopEvent 80125ff4 */
int  career_event_requires_shop(const unsigned char *ev, uint32_t shop_handle);   /* 80121b30 */
const unsigned char *career_shop_by_handle(uint32_t h);
const unsigned char *career_shop_of_type(int type);           /* the first ShopDataDesc of a type (test aid) */
/* NotifyEnterShopFromFreeRoam 80126db0 with the player's position (SaveSpawnPosition) */
void career_enter_shop(const unsigned char *shop, const float pos[3], uint16_t heading);
uint32_t career_gps_shop_event(const char *name);            /* the same, only while the current shop event is name */
uint32_t career_gps_shop(void);                               /* MaybeTargetGPSForShopEvent: the shop zone hash, 0 none */
uint32_t career_ride_preset(void);                            /* BuildRideForPlayer 8012731c: 0 = the career car */
int  career_respawn(float pos[3], uint16_t *heading);         /* GetRespawnParameters */

/* Stage0StateManager 80130374..: the stage-0 intro in free roam */
enum { CS0_INTRO = 1, CS0_SMS = 2, CS0_MINIMAP = 4, CS0_CARLOT = 8, CS0_RELINQUISH = 16 };
int  career_first_time(void);                                 /* IsFirstTimeInCareer 80126108 */
void career_tutorial_completed(void);                         /* SetTutorialCompleted */
void career_stage0_begin(int shop_nis);                       /* NotifyBeginStage0Intro */
int  career_stage0_update(uint32_t now_ms, int speech_done);  /* Update 80130680: CS0_* actions for the caller */
int  career_stage0_skip(void);                                /* RequestSkip: CS0_RELINQUISH or 0 */
void career_stage0_read_message(void);
void career_stage0_sms_dismissed(void);
int  career_stage0_has_control(void);
int  career_stage0_show(int what);                            /* 0 telephone, 1 engage icon, 2 minimap, 3 car lot, 4 gauges */

/* ---- CareerSpeech (+0x887c, 0x190 bytes; PC Default 0x531800, layout in
   CAREER.md "CareerSpeech timers"): the story's timed speech events. C2 ports
   the timer queue and the events that drive the career (stage-0 races become
   visible 15 s into DDAY_EVENT_A); the voice itself is C10, behind the hook. */
enum { CSPK_CAREER, CSPK_IE, CSPK_IA, CSPK_TIMED, CSPK_DVD, CSPK_MAG, CSPK_SMS,
       CSPK_OUTRUN, CSPK_FES, CSPK_FESDYNO,   /* SpeechCmdOutrun(OR, Char), FES(Screen, Char), FESDyno(Dyno, Char) */
       CSPK_PURGE };                          /* UG2_SpeechParser::PurgeSpeech (before a purging SpeechCmd) */
/* a speech request: SpeechCmdCareer(Type_CR_Event a, Type_CharId b),
   SpeechCmdIE(Type_Locations a, CharId b), SpeechCmdIA(a, b),
   SpeechCmdTimed(Type_TimeProgress a), SpeechCmdDVD/Mag(name a, status b);
   CSPK_SMS = SMSMessageQueue::AddExternalMessage(a) (C8) */
extern void (*career_speech_hook)(int kind, uint32_t a, uint32_t b);
/* UpdateTimerEvents 0x54bd..: dt in ms of career time; speech_done = no line
   playing (IsSpeechDone: true with no speech manager) */
void career_speech_tick(uint32_t dt_ms, int speech_done);
int  career_speech_queued(int ev);                            /* IsTimerEventQueued */
/* the CareerSpeech handlers the game calls (PC layout, SPEECH_GAME.md 1b-1g) */
void career_speech_stage0_intro(void);                        /* Stage0Intro: Career(0x8000, 0x20) */
void career_speech_real(uint32_t ms);                         /* HandleTick: the RealTimer (FE too) */
/* HandleNeighbourhood: due on a change (stage > 1, 300 s); say for the hidden event's one */
int career_speech_hood_due(uint32_t hood, int speech_done);
void career_speech_hood_say(uint32_t hood);
const char *career_neighbourhood_name(uint32_t zone_hash);   /* GetNeighbourhoodNameInfo */
int  career_speech_fe(int ev);                                /* HandleFESpeechEvent 0x5243d0 (career only) */
void career_speech_engage(const unsigned char *ev, const unsigned char *showcase);   /* HandleEngageRequest 0x5251a0 */
void career_speech_decline(void);                             /* HandleEngageDecline 0x511280 */
void career_speech_outrun_start(void);                        /* HandleOutrunStart 0x511000 */
void career_speech_outrun_end(int won);                       /* HandleOutrunEnd 0x5110c0 */
void career_speech_sponsor(void);                             /* HandleSponsorSelect 0x525510 */
void career_speech_sponsor_done(void);                        /* HandleSponsorSelectDone */
void career_speech_uniques(int visual);                       /* UniquesBrowser: ev 8 / 9 (0x5255d0 / 0x525610) */
void career_speech_showcase_unlock(const unsigned char *sc);  /* HandleShowcaseUnlock 0x5253f0 */
void career_speech_dvd_stars(int stars);                      /* HandleDVDStars: +0x19 */
void career_speech_showcase_mode(void);                        /* HandleShowcaseMode 0x525850: once, +0xa6 */

/* ---- the world layer (C3; CAREER.md "C3: the world layer") --------------
   The free roam's trigger zones are Paths chunk 0x3414a records (racepath_zone);
   the career knows them by name hash (+0x30) and type: 13 events / showcases /
   world challenge starts, 14 shops, 15, 18 SMS help, 19 banks. */
typedef struct { uint32_t hash; int type; } CareerWorldZone;
/* UnlockedTriggerZone (0x18): the zone and what it holds (one of) */
typedef struct {
    uint32_t zone;                     /* +0 the TrackPathZone (its name hash; 0 = empty slot) */
    const unsigned char *event, *shop, *showcase, *wc;
    int pile;                          /* +0x14 added from the event pile (1) */
} CareerZone;
/* the region's zones (once per world load; copied) */
void career_set_world_zones(const CareerWorldZone *z, int n);
void career_recalc_zones(void);                               /* RecalcUnlockedZones 8012924c */
const CareerZone *career_unlocked_zone(int i);                /* the manager's 64 slots; NULL empty */
int  career_zone_on_radar(const CareerZone *z);               /* IsVisibleOnRadar 80128ad4 */
int  career_zone_data(uint32_t zone, CareerZone *out);        /* GetTrackPathZoneData 801240bc: 0 none */
int  career_zone_enterable(const CareerZone *z);              /* IsEnterable 80123bd0 */
int  career_shop_enterable(const unsigned char *shop);        /* IsShopEnterable 801243b8 */
int  career_shop_on_radar(const unsigned char *shop);         /* IsShopVisibleOnRadar 80124484 */
int  career_shop_unlocked(const unsigned char *shop);         /* ShopDataDesc::IsShopUnlocked 80122044 */
int  career_been_to_shop(const unsigned char *shop);          /* HasBeenToShopBefore 80124440 */
/* Player::UpdateCurrentTrackPathZone 80218c44 (career free roam): zones =
   the type 13..15 zones holding the car in FindZone order (type, then file
   order). Returns CZONE_* for the HUD (FEngHud::Enter/ExitTriggerZone) */
/* CZONE_AUTO: NotifyEnterShopTriggerArea at a world challenge's goal shop --
   its engage dialog opens by itself (SetEngagingDialog + the engage requested) */
enum { CZONE_EXIT = 1, CZONE_ENTER = 2, CZONE_AUTO = 4 };
int  career_player_zones(const uint32_t *zones, int n);
const CareerZone *career_current_zone(void);                  /* +0x7dec.. (NotifyEnterTriggerZone) */
int  career_can_engage(void);                                 /* CanWeEngage 801244d4 */
void career_engage_requested(void);                           /* NotifyEngageMechanicRequested 8012660c */
int  career_engaging_dialog(void);                            /* IsEngagingDialog (+0x6820) */
void career_set_engaging_dialog(int on);                      /* SetEngagingDialog */
const CareerZone *career_engaged(void);                       /* +0x7df0.. the engaged zone; NULL none */
const CareerZone *career_left_zone(void);                     /* the zone the last CZONE_EXIT left */
/* the zone's EventIndicatorType (FEngHud::Enter/ExitTriggerZone 801136d4):
   a shop by its type 0xb..0x10 (else 1), an event by race type 1..5, URL 7,
   SUV 8; a showcase 0x11, a world challenge 0x12; 0 none */
int  career_zone_indicator(const CareerZone *z);
/* the engage dialogs' text (CAREER.md "C4e"): label hashes for English.bin.
   body: CareerEventData::GetEngageDialogBody 80121b88, rnd(n) = bRandom
   0..n-1 (a stage's random message); from: GetEngageDialogFrom 80121d54,
   opponent 0's name or (NULL, *label) a label; race_type: Subject's type
   name / Race Type (RaceEngageDialog::Setup, PC 0x4b53c0) */
uint32_t career_engage_body(const unsigned char *ev, int (*rnd)(int n));
const char *career_engage_from(const unsigned char *ev, uint32_t *label);
void career_engage_race_type(const unsigned char *ev, uint32_t *subject, uint32_t *type);
uint32_t career_shop_header(const unsigned char *shop);        /* ShopDataDesc::GetEngageDialogHeader 80122158 */
uint32_t career_shop_body(const unsigned char *shop, int (*has_label)(uint32_t));   /* 80122214 */
uint32_t career_shop_accept(const unsigned char *shop);        /* EngageShopDialog::Setup: ACCEPT_BUTTON's label */
/* NotifyEnterEvent 80126318 (RaceEngageDialog accept, LaunchEngagedRace):
   pos / heading the car's (kept, has_spawn 0); NULL keeps the spawn */
void career_enter_event(const unsigned char *ev, const float pos[3], uint16_t heading);
int  career_post_race_free_roam(const unsigned char *ev, int stage_before, char *out, int n);   /* NotifyEnterFreeRoamFromPostRace 80126918 */
void career_decline_event(const unsigned char *ev);           /* NotifyDeclineEvent 801265e0 */
void career_decline_shop(const unsigned char *shop);          /* NotifyDeclineEnterShopFromFreeRoam 80126f48 (empty) */
/* NotifyInBankTriggerZone 801262b8 (a type 19 zone holding the car, 0 none):
   the cash picked up (FEngHud::ShowPickedUpBank), 0 none */
int  career_bank_zone(uint32_t zone);

/* ---- world challenges (WorldChallengeState, CAREER.md "C5w") --------------- */
int  career_wc_active(void);                                  /* IsActive */
void career_wc_enter(const unsigned char *wc, uint16_t heading);   /* NotifyEnter 8012d284 (the engage accept) */
/* Tick 8012d61c: 1 when the time ran out now (NotifyFailedMission: the
   engage dialog opens by itself -- SetEngagingDialog, RC message 0x11) */
int  career_wc_tick(float dt);
int  career_wc_countdown(void);                               /* ShouldShowCountdownTimer: state 1 */
int  career_wc_goal_shop(const unsigned char *shop);          /* IsGoalGetToShop 8012cf9c */
/* EventEngageDialog::Setup's texts (GetEngageChallengeDialogBody 8012dafc /
   Subject 8012dc44): the body's label (one of its generic lines by rnd) and
   the time limit in seconds; the subject's label (0 none) */
uint32_t career_wc_body(const unsigned char *wc, int (*rnd)(int n), int *seconds);
uint32_t career_wc_subject(const unsigned char *wc);
void career_wc_failure_accepted(void);                        /* NotifyFailureAccepted 8012d4b4 */
/* NotifyFailureRejected 8012d4fc: state 8; the start zone's hash for the
   car's ResetToPosition (with wcs.heading), 0 none */
uint32_t career_wc_failure_rejected(void);
void career_wc_reset_post_failure(void);                      /* ResetPostFailure 800e7864's (7 / 8) */
void career_wc_unique_chosen(void);                           /* NotifyUniqueChosen: state 5 + NotifySucceedMission */
void career_wc_exit_to_frontend(void);                        /* NotifyExitingToFrontend */
uint32_t career_wc_start_zone(const unsigned char *wc);       /* GetStartAtTriggerName's hash */
const unsigned char *career_uniques_shop(void);               /* GetUniquesShop: THE_UNIQUES_SHOP */
/* GetShopWeAreCurrentlyIn 80126638: the uniques shop in state 4, else the entered shop */
const unsigned char *career_shop_now(void);
int  career_bank_claimable(uint32_t zone);                    /* RecalcBankTriggers: the zone shows a pickup */

/* ---- the career cars (FEPlayerCarDB, GC 0x803bc6c4 / PC 0x83ad90; CAREER.md
   "C6: the career car store"). 5 FECareerCar slots, handles
   FEngHashString("CAREER_SLOT_%d"); a slot is locked until
   CommitUnlockCareerCarSlot (Default, each sponsor), then empty until a car
   is chosen in the Car Lot. A chosen car is the stock car (FillFromStockCar);
   its customization (FECarConfig + OwnedPartInventory in the original) is
   kept here as a garage.txt car line, "" = stock. The original saves the DB
   in the UserProfile before the career block (PC 0x52a280: 0x5166b0 then
   0x514530); the port saves it in its own profile block (career_cars_save). */
#define CAREER_NSLOTS 5
#define CAREER_CFG_MAX 2048
#define CAREER_OWN_MAX 1024
typedef struct {
    uint32_t handle;                  /* +4 FEngHashString("CAREER_SLOT_%d") */
    uint8_t  locked;                  /* +0x7f4 (PC u8) */
    uint8_t  has;                     /* +0x7f8 (PC +0x7f5) a car is in the slot */
    uint32_t stock;                   /* the FEStockCar it came from: bStringHash("STOCK_<type>") */
    int      type;                    /* its car type (PresetCarSlot +0x20), -1 none */
    char     cfg[CAREER_CFG_MAX];     /* the customization, a garage.txt car line ("" = stock) */
    /* OwnedPartInventory (+0x434, Default 80135a28): GetWordToSetBitFor
       80135610 / OwnsCarPart 801358bc own every part by its own bit (the
       NewGetPartIndex position: regions for the roof, hood, spoiler, mirrors,
       rims / spinners and 0xa2, else bit idx of the entity's word); the port
       keys each part by its name hash (rims: the exact size part). own_ent:
       the entity words that are != 0 (AnyOwned 80135778). own_legacy: a save
       of before the per-part keys (CCAR v2/v3) owned whole entities -- what
       it had bought stays owned (career_owns_part), new buys are per part.
       The perf parts are bits (+0x15c), the brands a list (+0x2ac). */
    uint8_t  own_ent[12];             /* entity bits, 1..95: any part of it bought */
    uint8_t  own_legacy[12];          /* entity bits from an old save: every part of it owned */
    uint16_t nown;
    uint32_t own[CAREER_OWN_MAX];     /* the bought parts' name hashes */
    uint8_t  own_perf[16];            /* +0x15c ePhysicsPerformanceParts bits */
    int32_t  spent;                   /* +0x7f0 the cash put into the car */
    float    stars;                   /* its GetNumberOfStars, kept when a shop commits it */
    /* PresetCarSlot +0x348: 5 x CustomPhysicsTunings (0x29 bytes: types 0-22,
       ECU 9 pts, turbo 9 pts), one per eTuningSetting; byte 100 = stock
       (DefaultCustomTuningSpecs 80135004). The crib's Test Track (tuning.c). */
    uint8_t  tune[5][0x29];
    /* OwnedPartInventory +0x2ac: the brands of the bought perf parts, 0x44
       slots filled in order, 0 ends (BuyPerfPart 80135930 / OwnsBrand 801359e8) */
    uint32_t brand[0x44];
} CareerCar;
/* HasArtworkOnDisc 80244120 (CARS\<type>\GEOMETRY.BIN on disc): set by the
   game; NULL = every car is there */
extern int (*career_car_on_disc)(int type);
void career_cars_default(void);                               /* DefaultCareerCars 801340a4 */
void career_unlock_car_slot(void);                            /* CommitUnlockCareerCarSlot 801265b4 */
CareerCar *career_car_slot(int i);                            /* NULL past the slots */
CareerCar *career_car_by_handle(uint32_t h);                  /* GetCareerCarByHandle */
const CareerCar *career_current_car(void);                    /* GetCurrentCareerCar 801273a8: NULL none */
void career_switch_car(uint32_t handle);                      /* CommitSwitchToOwnedCar 801272a8: +0x8740 */
const char *career_car_name(const CareerCar *c);              /* the car type name (CARS\<name>), "" none */
uint32_t career_slot_handle(int slot);                        /* FEngHashString("CAREER_SLOT_%d") */
int  career_slot_counts(int *unlocked, int *empty);           /* InitUnlockedAndEmptySlots: unlocked count */
int  career_first_empty_slot(void);                           /* FindFirstEmptySlot: -1 none */
/* the stock cars (DefaultStockCars PC 0x516510): handle / type */
uint32_t career_stock_handle(int type);                       /* 0 = not a stock car */
int  career_stock_type(uint32_t stock);                       /* -1 none */
int  career_car_in_region(int type);                          /* IsCarAvailableInCurrentBuildRegion 8012eff8 (US) */
int  career_car_unlocked(int type);                           /* BuildUnlockedCareerCarList 801273d8 */
/* Quick Race (UnlockSystem filter 1, the profile's career): a stock car type
   (CareerUnlocker::IsCarUnlocked || check_quick_race_unlock, PC 0x534180) and
   a race record number (IsTrackUnlockedForQuickRace, PC 0x5143c0) */
int  career_qr_car_unlocked(int type);
int  career_qr_track_unlocked(int race);
/* UICareerCarLot::BuildCarList(1): the stock cars the Car Lot shows, in car
   type order (SortCarList); returns the count */
int  career_lot_cars(int *types, int max);
void career_choose_car(int slot, uint32_t stock);             /* CommitChooseCar 801272b8 */
void career_car_trade(uint32_t old_handle, uint32_t stock);   /* CareerCarTrade 80133ff4 */
void career_car_set_cfg(uint32_t handle, const char *cfg);    /* the career shops' commit (C7) */
void career_car_set_stars(uint32_t handle, float stars);     /* its rating at that commit (C9) */
/* PlayerCareerStatistics (0x835c00), filled by PlayerCareerState::FillStatistics
   0x528a60 when the Career Status screen opens (STATUS.md section 5) */
typedef struct {
    int   cash, races, wins;            /* +0x00 +0x04 +0x08 */
    float win_pct;                      /* +0x0c wins / races */
    int   circuit, sprint, drag, drift, streetx, url;   /* +0x10.. +0x24 */
    int   mags_won, mags_total;         /* +0x28 / +0x2c */
    int   sponsors;                     /* +0x30 stages with a sponsor signed */
    short shops[8][2];                  /* +0x34 hidden shops {found, total} per stage */
    float parts;                        /* +0x54 parts unlocked fraction */
    float completion;                   /* +0x68 events won / events */
    int   stage;                        /* +0x6c */
    int   world_won, world_req, sponsor_won, sponsor_req, url_won, url_req;   /* +0x70..+0x84 */
    int   dvds_won, dvds_total;         /* +0x88 / +0x8c */
    int   sdvd_won, sdvd_total;         /* +0x90 / +0x94 the current stage's */
    float stage_completion;             /* +0x98 */
    struct { float stars[4]; uint8_t won[4]; int ndvd, nmag; } st[8];   /* +0x9c + s*0x28 */
    float rep[8], avg[8];               /* +0x1dc / +0x1fc */
} CareerStats;
void career_fill_statistics(CareerStats *s);                  /* FillStatistics 0x528a60 */
/* PlayerCareerState::BuyCarPart 80129480 / BuyPerfPart 80129730 on the
   current career car: the wallet spent (Spend 8012a010: floors at 0), the
   inventory and the car's +0x7f0. key: the part's name hash; 0 = a slot
   that is never owned (0x3f, 0x44..0x47, 0x49: charged, nothing recorded). */
int  career_owns_part(int entity, uint32_t key);              /* OwnsCarPart 801358bc */
#ifdef OPENUG2_SIM_PRE_H
void career_sim_find_shops(void);   /* test aid: OPENUG2_SIM_SHOPFOUND */
#endif
int  career_any_owned(int entity);                            /* AnyOwned 80135778 (entity words) */
void career_buy_part(int entity, uint32_t key, int cost);
int  career_owns_perf(int part);                              /* OwnsPerfPart 801359a8 */
void career_buy_perf(int part, int cost, int charge, uint32_t brand);   /* BuyPerfPart 80129730 */
/* CareerUnlocker::IsDecalBrandUnlocked 8012f94c: a brand that a perf package
   part has is open only when an owned career car bought it */
int  career_decal_brand_unlocked(uint32_t brand);
void career_spend(int cost);                                  /* CareerWallet::Spend */
long career_cars_save(unsigned char *out, long cap);
int  career_cars_load(const unsigned char *p, long n);

/* the save (PlayerCareerState::SaveToBuffer 0x514530 order) */
long career_save(unsigned char *out, long cap);               /* bytes written, or the size with out NULL */
int  career_load(const unsigned char *p, long n);

/* ---- SMSMessageQueue (C8; re-tools/out/career/c8/SMS.md) ---------------- */
int  career_nsms_data(void);
const unsigned char *career_sms_data(int i);                  /* SMSMessageData (0x14) */
CareerSmsSlot *career_sms_add(const unsigned char *d);        /* AddMessage 8012c000; NULL queued / full */
CareerSmsSlot *career_sms_add_external(uint32_t name_hash);   /* AddExternalMessage 8012bd30 */
void career_sms_add_wc(const unsigned char *wc);              /* AddWorldChallengeMessage 8012bd88 */
int  career_sms_count(void);
CareerSmsSlot *career_sms_slot(int i);
int  career_sms_unread(void);                                 /* +0x1404 */
CareerSmsSlot *career_sms_the_one_unread(void);               /* GetTheOneUnreadMessage */
void career_sms_read(CareerSmsSlot *m);                       /* NotifyReadMessage (+ Stage0's) */
void career_sms_delete(CareerSmsSlot *m);                     /* NotifyDeleteMessage */
void career_sms_sortable_all(void);                           /* MarkAllReadMessagesSortable */
void career_sms_summoned(void);                               /* SummonMessageViewer: pending cleared */
/* GetSubject / GetBody: the text's language hash; *arg != 0 makes it a
   printf format of the language string *arg (0xffffffff: an empty one) */
uint32_t career_sms_subject(const CareerSmsSlot *m, uint32_t *arg);
uint32_t career_sms_body(const CareerSmsSlot *m, uint32_t *arg);
void career_sms_complete_event(const unsigned char *ev);      /* NotifyCompleteEvent (CommitWinEvent's) */
void career_sms_enter_shop(const unsigned char *shop);        /* NotifyEnterShop (career_enter_shop's) */
void career_sms_enter_zone(uint32_t zone);                    /* NotifyEnterTriggerZone (kind 7) */
void career_sms_dvd_cover(uint32_t n);                        /* NotifyDVDCoverElegible 8012c754 */

/* ShowcaseState (C9a, re-tools/out/career/c9/SHOWCASE.md): ShowcaseData
   (0x40) +0 name, +0x20 handle, +0x24 kind (1 in-game magazine, 2 in-game
   DVD, 3 / 4 the front end's), +0x25 stage, +0x26 base cash, +0x28 the
   magazine setup, +0x2c the zone, +0x35 needed for the stage, +0x3c the
   DVD's stars; its record +4 2 unlocked, +5 bit0 the auto engage shown,
   +6 1 done */
const unsigned char *career_showcase_by_handle(uint32_t h);   /* FindShowcaseDataByHandle 801224d4 */
int   career_showcase_enterable(const unsigned char *sc);     /* IsEnterable 8012b124 */
void  career_showcase_unlock(const unsigned char *sc);        /* NotifyUnlockedShowcase 8012b0d8 */
float career_star_rating(void);                               /* GetCurrentPlayerStarRating 0x528410 */
void  career_recalc_dvd_unlocks(void);                        /* RecalcDVDUnlocks 0x5331c0 */
int   career_showcase_reward(const unsigned char *sc, int *star);   /* 0x541210: base + star, paid */
void  career_showcase_completed(const unsigned char *sc);
void  career_enter_fe_magazine(const float pos[3], uint16_t heading);   /* NotifyEnterFEMagazineFromFreeRoam 801263dc */    /* NotifyShowcasePictureTaken + NotifyShowcaseCompleted */
void career_begin_getaway(void);                              /* PlayerCareerState::NotifyBeginGetAway */
void career_encounter_finished(int won);                      /* NotifyRandomEncounterFinished 80126f84 */
int  career_sms_help_zone(uint32_t zone);                     /* NotifyInSMSAutoTriggerZone: 1 = a tip queued */
int  career_help_zone_active(uint32_t zone);                  /* CanHelpTriggerZoneBeActivated */
int  career_sms_tick(float dt, int roam, int cond);           /* 1 = DoAutopopup */

#ifdef __cplusplus
}
#endif
#endif
