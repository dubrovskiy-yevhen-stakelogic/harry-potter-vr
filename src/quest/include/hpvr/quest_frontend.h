#pragma once
#include "hpvr/hp1_gesture.h"
#include "hpvr/quest_vr_settings.h"
#include "hpvr/quest_demo.h"
#include "hpvr/quest_maps.h"
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
namespace hpvr::quest {
struct FrontTexture { std::string name; std::vector<std::uint8_t> rgba; };
struct StoryPage {
    std::array<std::uint32_t,4> tiles{};
    std::string dialogue_name, subtitle;
    wand::Hp1MpegSound voice;
};
struct FrontAssets {
    struct BumpSpeech { std::int32_t actor_reference{}; std::vector<std::string> lines; };
    struct MusicCue {
        std::int32_t actor_reference{};
        std::string tag;
        int music_index=-1;
        unsigned volume_percent=100;
    };
    unsigned map_id=0,level_music_index=3;
    std::vector<MusicCue> music_cues;
    std::vector<BumpSpeech> bump_speech;
    std::vector<FrontTexture> textures;
    std::array<std::uint32_t,6> menu{}, paper{};
    std::array<std::uint32_t,2> logo{};
    std::array<std::uint32_t,6> book{}, folio{}, folio_secret{}, report{};
    std::array<std::uint32_t,3> tabs{};
    std::array<std::uint32_t,4> bean_pile{};
    std::uint32_t health_full{},health_empty{},bean_counter{},bean_badge{},card_badge{};
    float health_top=0,health_bottom=1; // Alpha bounds of owned lightning art.
    std::uint32_t missing_big{},missing_small{},arrow_left{},arrow_right{};
    std::uint32_t font{}, white{}, smoke{},card_face{};
    std::vector<StoryPage> story;
    std::vector<wand::Hp1MpegSound> music;
    std::vector<wand::Hp1MpegSound> gameplay_audio; // Ron, basic cast, twins lesson and pickup.
    wand::Hp1PcmSound frog_pickup;
    wand::Hp1MpegSound card_pickup;
    std::string level_objective;
    std::string error;
};
bool LoadFrontAssets(const std::filesystem::path& root, FrontAssets* out,unsigned map_id=0);
std::string AudioCacheName(const wand::Hp1MpegSound& source, bool stereo=false);
struct ProgressSave {
    unsigned health=100,lesson_passes=0;
    bool frog_taken=false,card_awarded=false,card_taken=false;
    bool peeves_first_hit=false;
    unsigned peeves_phase=0; // 0 waiting, 1 encounter, 2 departure scene, 3 complete.
    bool twins_departed=false;
    bool filch_seen=false;
    unsigned filch_resume_stage=0;
    std::uint64_t generation=0;
    // 0: narrated book, 1: opening cutscene, 2: free movement after opening.
    unsigned phase=0, page=0, quest_stage=0;
    std::array<float,3> player{0,0.815F,0};
    float yaw=0;
    std::array<std::array<float,4>,11> cast{}; // Opening cast, then Filch, Draco, Crabbe, Goyle, Hermione, Quirrell.
    std::vector<std::int32_t> collected_beans; // Stable owned actor references, sorted.
    std::array<float,2> doors{};
    unsigned map_id=kIntroductionMapId; // Stable QuestMapDescriptor ID; actor references are map-local.
    unsigned banked_beans=0; // Beans collected on completed maps.
    std::vector<std::int32_t> activated_events; // Sorted unique map-local one-shot references.
    unsigned challenge_stars=0;
    std::string graph_state; // Up to 16 KiB of event-reducer state, never executable script.
    std::string world_state; // Up to 32 KiB of map-local actor and mover state.
};
bool TutorialRewardReady(const ProgressSave& progress);
void ApplyTutorialDamage(ProgressSave& progress);
bool ApplyFirstPeevesContact(ProgressSave& progress);
struct RewardApproach {
    bool ready=false, armed=false;
    bool Update(const ProgressSave& progress,float distance,bool visible);
};
bool ReadProgress(const std::filesystem::path& directory,unsigned slot,ProgressSave* out);
bool WriteProgress(const std::filesystem::path& directory,unsigned slot,ProgressSave* inout);
enum class FrontScreen { Main, Slots, Slot, Replace, Story, Game, Pause, Stub, Cards, Report, Objective, Vr, Debug, Welcome, DemoEnd, Levels, LevelSlots, LevelStart, Controls };
enum class FrontAction { None, NewGame, Continue, StoryDone, SaveMenu, SkipScene, Resume, BeginLevel, OpenCommunity, StartSelectedLevel };
inline constexpr unsigned kVrTurningRow=9;
inline constexpr unsigned kVrTurnSpeedRow=10;
inline constexpr unsigned kVrControlsRow=11;
inline constexpr unsigned kVrMenuRowCount=13;
inline constexpr unsigned kControlsPageCount=5;
inline constexpr float VrMenuRowY(unsigned row){return 110.0F+20.0F*float(row);}
struct FrontQuad {
    float x=0,y=0,w=0,h=0,u=0,v=0,uw=1,vh=1;
    std::uint32_t texture=0, tint=0xffffff;
};
class QuestFrontEnd {
public:
    FrontAssets assets;
    std::filesystem::path saves;
    FrontScreen screen=FrontScreen::Main;
    FrontScreen paused=FrontScreen::Game;
    unsigned selection=0, slot=0, page=0;
    unsigned selected_map=0;
    std::vector<int> refresh_rates;
    unsigned card_page=0;
    unsigned controls_page=0; // Help navigation must not change the story page.
    float page_time=0;
    bool page_voice_started=false;
    std::array<bool,3> occupied{};
    ProgressSave progress;
    VrSettings vr;
    FrontScreen vr_return=FrontScreen::Game;
    unsigned vr_return_selection=0;
    bool vr_save_failed=false;
    bool debug_pinned=false; // Transient overlay, never written to a save.
    void ToggleVrMenu();
    void ShowDemoNotice(bool finished);
    std::vector<FrontQuad> VrValueQuads(int value,bool scale) const;
    std::vector<FrontQuad> VrRefreshQuads(int hz) const;
    std::vector<FrontQuad> VrTurningQuads(bool smooth) const;
    std::vector<FrontQuad> VrTurnSpeedQuads(int degrees) const;
    std::vector<FrontQuad> VrVoiceStatusQuads(unsigned status) const;
    std::vector<FrontQuad> VoiceAimQuads(unsigned status) const;
    std::string message;
    void RefreshSlots();
    FrontAction Input(float move_y,bool confirm,bool back,float move_x=0);
    FrontAction TickStory(float seconds,float voice_duration);
    void BeginStory(unsigned first_page);
    void BeginGame();
    bool Save();
    bool Visible() const {return screen!=FrontScreen::Game;}
    bool VrPanel() const {return screen==FrontScreen::Vr||screen==FrontScreen::Debug||screen==FrontScreen::Controls;}
    bool DemoNotice() const {return screen==FrontScreen::Welcome||screen==FrontScreen::DemoEnd;}
    bool FloatingPanel() const {return VrPanel()||DemoNotice();}
    bool WorldVisible() const {return screen==FrontScreen::Game||DemoNotice()||(VrPanel()&&(vr_return==FrontScreen::Game||vr_return==FrontScreen::Welcome||vr_return==FrontScreen::DemoEnd));}
    bool PausesWorld() const {return !(screen==FrontScreen::Game||(VrPanel()&&vr_return==FrontScreen::Game));}
    bool PausesAudio() const {
        const auto underlying=VrPanel()?vr_return:screen;
        return underlying==FrontScreen::Pause||underlying==FrontScreen::Cards||underlying==FrontScreen::Report;
    }
    std::vector<FrontQuad> Quads() const;
    std::vector<FrontQuad> BeanCounterQuads(unsigned count) const;
    std::vector<FrontQuad> ChallengeStarQuads(unsigned count,bool report=false) const;
    std::vector<FrontQuad> BroomLabelQuads() const;
    // Fields: 0 hoop hits, 1 remaining seconds, 2 stage. Independently cached.
    std::vector<FrontQuad> BroomNumberQuads(unsigned value,unsigned field) const;
    std::vector<FrontQuad> HudQuads(unsigned count,bool show_beans) const;
    std::vector<FrontQuad> LessonQuads(unsigned passes,bool ready) const;
    std::string DrawKey() const;
private:
    bool confirm_down_=false,back_down_=false,stick_down_=false;
    bool horizontal_down_=false;
};
} // namespace hpvr::quest
